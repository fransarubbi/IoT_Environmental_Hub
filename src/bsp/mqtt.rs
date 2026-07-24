//! Módulo MQTT.
//! Abstrae la implementación específica del hardware detrás de traits.

use crate::app::{
    message::domain::MAX_MSGPACK_BUFFER_SIZE, pool::pool::CORE_DATA_POOL,
    system_settings::domain::SystemSettings,
};
use crate::svc::mqtt::Mqtt;

use async_channel::{Receiver, Sender};
use esp_idf_svc::mqtt::client::{
    Details, EspMqttClient, EventPayload, MqttClientConfiguration, QoS,
};
use esp_idf_svc::tls::X509;
use heapless::{String, Vec};
use log::{debug, error, info, warn};

use std::{
    ffi::CStr,
    sync::{Arc, Mutex, RwLock},
    time::Duration,
};

pub enum MqttData {
    Connected,
    Disconnected,
    PubAck { msg_id: u16, return_code: u8 },
    InMessage(usize),
    OutMessage(usize),
    SubscribeInitial,
    SubscribeOperational,
    Stop,
    FirmwareRestart,
}

/// Estructura que contiene el mensaje binario y el tópico donde se recibió.
pub struct IncomingMessage {
    pub topic: String<75>,
    pub payload: Vec<u8, MAX_MSGPACK_BUFFER_SIZE>,
}

/// Estado de la recepción para ensamblar fragmentos.
struct RxState {
    topic: String<75>,
    buffer: Vec<u8, MAX_MSGPACK_BUFFER_SIZE>,
}

/// Estructura concreta para ESP-IDF
pub struct EspIdfMqttManager {
    client: EspMqttClient<'static>, // El cliente nativo de esp-idf-svc
    settings: Arc<RwLock<SystemSettings>>,
    receiver: Receiver<MqttData>,
    sender: Sender<MqttData>,
    free_idx_tx: Sender<usize>,
}

impl EspIdfMqttManager {
    /// Constructor. Recibe canales para inyectar eventos al resto del sistema.
    pub fn new(
        sender: Sender<MqttData>,
        receiver: Receiver<MqttData>,
        settings: Arc<RwLock<SystemSettings>>,
        hub_cert: &'static [u8],
        hub_key: &'static [u8],
        root_ca: &'static [u8],
        free_idx_tx: Sender<usize>,
        free_idx_rx: Receiver<usize>,
    ) -> Result<Self, String<50>> {
        let id: &'static str = Box::leak(
            settings
                .read()
                .unwrap()
                .mac_addr()
                .to_string()
                .into_boxed_str(),
        );
        let uri: &'static str = Box::leak(
            settings
                .read()
                .unwrap()
                .mqtt_uri()
                .to_string()
                .into_boxed_str(),
        );

        // Convertimos los slices de bytes nulo-terminados a CStr sin reservar memoria
        let root_ca_cstr = CStr::from_bytes_with_nul(root_ca)
            .map_err(|_| String::<50>::try_from("root_ca sin null").unwrap_or_default())?;
        let hub_cert_cstr = CStr::from_bytes_with_nul(hub_cert)
            .map_err(|_| String::<50>::try_from("hub_cert sin null").unwrap_or_default())?;
        let hub_key_cstr = CStr::from_bytes_with_nul(hub_key)
            .map_err(|_| String::<50>::try_from("hub_key sin null").unwrap_or_default())?;

        let config = MqttClientConfiguration {
            client_id: Some(id),
            keep_alive_interval: Some(Duration::from_secs(60)),
            network_timeout: Duration::from_secs(30),
            crt_bundle_attach: None,
            server_certificate: Some(X509::pem(root_ca_cstr)),
            client_certificate: Some(X509::pem(hub_cert_cstr)),
            private_key: Some(X509::pem(hub_key_cstr)),
            ..Default::default()
        };

        let rx_state = Arc::new(Mutex::new(RxState {
            topic: String::new(),
            buffer: Vec::new(),
        }));

        let free_tx_cb = free_idx_tx.clone();
        let sender_to_closure = sender.clone();
        let free_rx = free_idx_rx.clone();

        let client = EspMqttClient::new_cb(uri, &config, move |event| {
            // Hacemos match directamente sobre EventPayload
            match event.payload() {
                EventPayload::Connected(_) => {
                    info!("conectado al broker MQTT");
                    match sender_to_closure.try_send(MqttData::Connected) {
                        Ok(_) => {}
                        Err(e) => error!("no se pudo enviar MqttData por el canal. {e}"),
                    }
                }

                EventPayload::Disconnected => {
                    warn!("desconectado del broker");
                    if let Err(e) = sender_to_closure.try_send(MqttData::Disconnected) {
                        error!("no se pudo enviar MqttData por el canal. {e}");
                    }
                }

                EventPayload::Published(msg_id) => {
                    debug!("puback recibido para msg_id: {msg_id}");
                    if let Err(e) = sender_to_closure.try_send(MqttData::PubAck {
                        msg_id: msg_id as u16,
                        return_code: 0,
                    }) {
                        error!("no se pudo enviar MqttData por el canal.{e}");
                    }
                }

                EventPayload::Received {
                    id: _,
                    topic,
                    data,
                    details,
                } => {
                    // Tomamos control del estado para reensamblaje
                    let mut state = rx_state.lock().unwrap();

                    match details {
                        // Caso 1: El mensaje es pequeño y entró completo en un solo paquete
                        Details::Complete => {
                            let topic_str = topic.unwrap_or("");
                            let mut topic_hl = String::<75>::new();
                            let _ = topic_hl.push_str(topic_str);
                            let mut payload_hl = Vec::<u8, MAX_MSGPACK_BUFFER_SIZE>::new();
                            let _ = payload_hl.extend_from_slice(data);

                            // Pedimos un índice libre al canal de llaves. Si no hay, esto espera (await).
                            let idx = match free_rx.try_recv() {
                                Ok(i) => i,
                                Err(_) => {
                                    error!("pool lleno. Descartando paquete MQTT entrante.");
                                    return;
                                }
                            };

                            {
                                let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                                slot.incoming = Some(IncomingMessage {
                                    topic: topic_hl,
                                    payload: payload_hl,
                                });
                            }

                            if let Err(e) = sender_to_closure.try_send(MqttData::InMessage(idx)) {
                                error!("cola llena, mensaje descartado. {e}");
                                {
                                    CORE_DATA_POOL[idx].lock().unwrap().incoming = None;
                                }
                                free_tx_cb.try_send(idx).unwrap();
                            }
                        }

                        // Caso 2: El mensaje es grande. Este es el primer fragmento.
                        Details::InitialChunk(_chunk_info) => {
                            // En el primer fragmento, el tópico siempre viene. Lo guardamos.
                            state.topic.clear();
                            let _ = state.topic.push_str(topic.unwrap_or(""));

                            // Limpiamos la basura de mensajes anteriores por seguridad
                            state.buffer.clear();

                            // Guardamos los primeros bytes
                            let _ = state.buffer.extend_from_slice(data);
                        }

                        // Caso 3: Es un fragmento intermedio o el último fragmento.
                        Details::SubsequentChunk(chunk_info) => {
                            // En SubsequentChunk, el 'topic' viene como None para ahorrar ancho de banda.
                            // Por eso usamos el que guardamos previamente en state.topic.
                            let _ = state.buffer.extend_from_slice(data);

                            // Comprobamos si con este fragmento ya alcanzamos el tamaño total esperado
                            if state.buffer.len() == chunk_info.total_data_size {
                                let idx = match free_rx.try_recv() {
                                    Ok(i) => i,
                                    Err(_) => {
                                        error!("pool lleno. Descartando paquete MQTT entrante.");
                                        return;
                                    }
                                };
                                {
                                    let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                                    slot.incoming = Some(IncomingMessage {
                                        topic: state.topic.clone(),
                                        payload: std::mem::take(&mut state.buffer),
                                    });
                                }
                                if let Err(e) = sender_to_closure.try_send(MqttData::InMessage(idx))
                                {
                                    error!("cola llena, mensaje descartado. {e}");
                                }
                            }
                        }
                    }
                }
                EventPayload::Error(e) => {
                    error!("error en cliente MQTT: {:?}", e);
                    if let Err(e) = sender_to_closure.try_send(MqttData::Disconnected) {
                        error!("no se pudo enviar MqttData por el canal. {e}");
                    }
                }
                _ => {}
            }
        })
        .map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("error MQTT: {}", e));
            s
        })?;

        Ok(Self {
            client,
            settings,
            receiver,
            sender,
            free_idx_tx,
        })
    }

    pub async fn run(mut self) {
        while let Ok(cmd) = self.receiver.recv().await {
            match cmd {
                MqttData::OutMessage(idx) => {
                    {
                        let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                        match &slot.serialized {
                            Some(msg) => {
                                let qos = msg.get_qos();
                                match self.publish(
                                    &msg.get_topic(),
                                    &msg.get_payload(),
                                    match_qos(qos),
                                    msg.get_retain(),
                                ) {
                                    Ok(_) => {
                                        let settings_arc = Arc::clone(&self.settings);
                                        let cfg = settings_arc.read().unwrap();
                                        if msg.get_topic() == cfg.topic_hub_firmware_ok().topic {
                                            if let Err(e) =
                                                self.sender.try_send(MqttData::FirmwareRestart)
                                            {
                                                error!("cola llena, mensaje descartado. {e}");
                                            }
                                        }
                                    }
                                    Err(e) => {
                                        error!(
                                            "fallo al publicar mensaje en {}: {}",
                                            msg.get_topic(),
                                            e
                                        );
                                    }
                                }
                            }
                            _ => info!("no se recibió un mensaje valido en OutMessage"),
                        }
                        slot.serialized = None;
                    }
                    self.free_idx_tx.try_send(idx).unwrap();
                }
                MqttData::SubscribeInitial => {
                    debug!("MQTT. Subscribiendo a los topicos iniciales.");
                    self.subscribe_initial_topics();
                }
                MqttData::SubscribeOperational => {
                    debug!("MQTT. Subscribiendo a todos los topicos.");
                    self.subscribe_all_topics();
                }
                MqttData::Stop => {
                    info!("MQTT. Recibido comando STOP. Apagando cliente...");
                    break;
                }
                _ => {}
            }
        }
        warn!("Bucle MQTT terminado (Canal rx_commands cerrado)");
    }
}

/// Implementación del trait público para el manager específico de ESP-IDF
impl Mqtt for EspIdfMqttManager {
    fn publish(
        &mut self,
        topic: &str,
        payload: &[u8],
        qos: QoS,
        retain: bool,
    ) -> Result<u16, String<100>> {
        self.client
            .publish(topic, qos, retain, payload)
            .map(|id| id as u16)
            .map_err(|e| {
                let mut s = String::<100>::new();
                let _ = core::fmt::write(&mut s, format_args!("error publicando: {}", e));
                s
            })
    }

    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<u16, String<50>> {
        self.client
            .subscribe(topic, qos)
            .map(|id| id as u16)
            .map_err(|e| {
                let mut s = String::<50>::new();
                let _ = core::fmt::write(&mut s, format_args!("error suscribiendo: {}", e));
                s
            })
    }

    fn subscribe_initial_topics(&mut self) {
        let settings_arc = Arc::clone(&self.settings);
        let cfg = settings_arc.read().unwrap();
        let _ = self.subscribe(
            &cfg.topic_linkage_ack().topic,
            match_qos(cfg.topic_linkage_ack().qos),
        );
    }

    fn subscribe_all_topics(&mut self) {
        let settings_arc = Arc::clone(&self.settings);
        let cfg = settings_arc.read().unwrap();

        let _ = self.subscribe(
            &cfg.topic_edge_state().topic,
            match_qos(cfg.topic_edge_state().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_edge_phase().topic,
            match_qos(cfg.topic_edge_phase().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_edge_handshake().topic,
            match_qos(cfg.topic_edge_handshake().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_heartbeat().topic,
            match_qos(cfg.topic_heartbeat().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_new_firmware().topic,
            match_qos(cfg.topic_new_firmware().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_new_settings().topic,
            match_qos(cfg.topic_new_settings().qos),
        );
        let _ = self.subscribe(
            &cfg.topic_edge_setting_ok().topic,
            match_qos(cfg.topic_edge_setting_ok().qos),
        );
    }
}

fn match_qos(qos: u8) -> QoS {
    match qos {
        0 => QoS::AtMostOnce,
        1 => QoS::AtLeastOnce,
        2 => QoS::ExactlyOnce,
        _ => QoS::AtMostOnce,
    }
}
