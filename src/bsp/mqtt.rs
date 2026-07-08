//! Módulo MQTT.
//! Abstrae la implementación específica del hardware detrás de traits.

use esp_idf_svc::tls::X509;
use crate::app::message::domain::SerializedMessage;
use crate::svc::mqtt::Mqtt;
use async_channel::{Sender, Receiver};
use esp_idf_svc::mqtt::client::{
    EspMqttClient, EventPayload, MqttClientConfiguration, QoS, Details 
};
use log::{debug, error, info, warn};
use std::sync::{Arc, Mutex, RwLock};
use crate::app::system_settings::domain::SystemSettings;
use std::time::Duration;
use std::ffi::CString; // Necesario para compatibilidad con C


pub enum MqttData {
    Connected,
    Disconnected,
    PubAck { msg_id: u16, return_code: u8 },
    InMessage(IncomingMessage),
    OutMessage(SerializedMessage)
}

/// Estructura que contiene el mensaje binario y el tópico donde se recibió.
pub struct IncomingMessage {
    pub topic: String,
    pub payload: Vec<u8>,
}

/// Estado de la recepción para ensamblar fragmentos.
struct RxState {
    topic: String,
    buffer: Vec<u8>,
}

/// Estructura concreta para ESP-IDF
pub struct EspIdfMqttManager {
    client: EspMqttClient<'static>,  // El cliente nativo de esp-idf-svc
    settings: Arc<RwLock<SystemSettings>>,
    receiver: Receiver<MqttData>
}

impl EspIdfMqttManager {
    /// Constructor. Recibe canales para inyectar eventos al resto del sistema.
    pub fn new(
        sender: Sender<MqttData>,
        receiver: Receiver<MqttData>,
        settings: Arc<RwLock<SystemSettings>>,
        hub_cert: &[u8],
        hub_key: &[u8],
        root_ca: &[u8]
    ) -> Result<Self, String> {
        
        // Convertimos los bytes crudos a CString para asegurar que terminan en Null (\0)
        // Esto previene fallos de segmentación (segfaults) en el motor de C.
        let root_ca_cstr: &'static std::ffi::CStr = Box::leak(
            CString::new(root_ca).map_err(|_| "error root_ca")?.into_boxed_c_str()
        );
        let hub_cert_cstr: &'static std::ffi::CStr = Box::leak(
            CString::new(hub_cert).map_err(|_| "error hub_cert")?.into_boxed_c_str()
        );
        let hub_key_cstr: &'static std::ffi::CStr = Box::leak(
            CString::new(hub_key).map_err(|_| "error hub_key")?.into_boxed_c_str()
        );

        let id = settings.read().unwrap().mac_addr().to_string();
        let uri = settings.read().unwrap().mqtt_uri().to_string();

        let config = MqttClientConfiguration {
            client_id: Some(&id),
            keep_alive_interval: Some(Duration::from_secs(60)),
            network_timeout: Duration::from_secs(30),
            crt_bundle_attach: None,
            server_certificate: Some(X509::pem(&root_ca_cstr)),
            client_certificate: Some(X509::pem(&hub_cert_cstr)),
            private_key: Some(X509::pem(&hub_key_cstr)),
            ..Default::default()
        };

        let rx_state = Arc::new(Mutex::new(RxState {
            topic: String::new(),
            buffer: Vec::new(),
        }));

        // 3. Manejamos el Result mapeando el error de EspError a String
        let client = EspMqttClient::new_cb(&uri, &config, move |event| {
            
            // 4. Hacemos match directamente sobre EventPayload
            match event.payload() { 
                EventPayload::Connected(_) => {
                    info!("conectado al broker MQTT");
                    match sender.try_send(MqttData::Connected) {
                        Ok(_) => {} 
                        Err(e) => error!("no se pudo enviar MqttData por el canal. {e}"),
                    }
                }

                EventPayload::Disconnected => {
                    warn!("desconectado del broker");
                    match sender.try_send(MqttData::Disconnected) {
                        Ok(_) => {} 
                        Err(e) => error!("no se pudo enviar MqttData por el canal. {e}"),
                    }
                }

                EventPayload::Published(msg_id) => {
                    debug!("puback recibido para msg_id: {msg_id}");
                    match sender.try_send(MqttData::PubAck {
                        msg_id: msg_id as u16, 
                        return_code: 0,
                    }) {
                        Ok(_) => {} 
                        Err(e) => error!("no se pudo enviar MqttData por el canal.{e}"),
                    }
                }

                EventPayload::Received { id: _, topic, data, details } => {
                    // Tomamos control del estado para reensamblaje
                    let mut state = rx_state.lock().unwrap();

                    match details {
                        // CASO 1: El mensaje es pequeño y entró completo en un solo paquete
                        Details::Complete => {
                            let msg = IncomingMessage {
                                // topic siempre viene garantizado (es Some) en Complete
                                topic: topic.unwrap_or("").to_string(), 
                                payload: data.to_vec(),
                            };
                            if let Err(e) = sender.try_send(MqttData::InMessage(msg)) {
                                error!("cola llena, mensaje descartado. {e}");
                            }
                        }

                        // CASO 2: El mensaje es grande. Este es el primer fragmento.
                        Details::InitialChunk(_chunk_info) => {
                            // En el primer fragmento, el tópico siempre viene. Lo guardamos.
                            state.topic = topic.unwrap_or("").to_string();
                            
                            // Limpiamos la basura de mensajes anteriores por seguridad
                            state.buffer.clear();
                            
                            // Guardamos los primeros bytes
                            state.buffer.extend_from_slice(data);
                        }

                        // CASO 3: Es un fragmento intermedio o el último fragmento.
                        Details::SubsequentChunk(chunk_info) => {
                            // En SubsequentChunk, el 'topic' viene como None para ahorrar ancho de banda.
                            // Por eso usamos el que guardamos previamente en state.topic.
                            state.buffer.extend_from_slice(data);

                            // Comprobamos si con este fragmento ya alcanzamos el tamaño total esperado
                            if state.buffer.len() == chunk_info.total_data_size {
                                
                                let msg = IncomingMessage {
                                    topic: state.topic.clone(),
                                    // std::mem::take extrae los bytes del buffer y deja el vector 
                                    // original vacío pero listo para ser reutilizado.
                                    payload: std::mem::take(&mut state.buffer), 
                                };
                                
                                if let Err(e) = sender.try_send(MqttData::InMessage(msg)) {
                                    error!("Cola llena, mensaje fragmentado descartado. {e}");
                                }
                            }
                        }
                    }
                }
                EventPayload::Error(e) => { 
                    error!("error en cliente MQTT: {:?}", e);
                }
                _ => {}
            }
        }).map_err(|e| format!("error creando cliente MQTT: {}", e))?; // <-- Añadido punto y coma y mapeo de error

        // 5. Devolvemos también los settings
        Ok(Self { client, settings, receiver })
    }

    pub async fn run(mut self) {
        while let Ok(cmd) = self.receiver.recv().await {
            match cmd {
                // Si otro servicio manda un mensaje, lo publicamos a través del cliente C
                MqttData::OutMessage(msg) => {
                    if let Err(e) = self.publish(&msg.get_topic(), &msg.get_payload(), match_qos(msg.get_qos()), msg.get_retain()) {
                        error!("Fallo al publicar mensaje en {}: {}", msg.get_topic(), e);
                    }
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
    ) -> Result<u16, String> {
        self.client
            .publish(topic, qos, retain, payload)
            .map(|id| id as u16)
            .map_err(|e| format!("error publicando: {}", e))
    }

    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<u16, String> {
        self.client
            .subscribe(topic, qos)
            .map(|id| id as u16)
            .map_err(|e| format!("error suscribiendo: {}", e))
    }

    fn enable_subscriptions(&mut self) {
        let settings_arc = Arc::clone(&self.settings);
        let cfg = settings_arc.read().unwrap();

        let _ = self.subscribe(&cfg.topic_edge_state_balance().topic, match_qos(cfg.topic_edge_state_balance().qos));
        let _ = self.subscribe(&cfg.topic_edge_state_normal().topic, match_qos(cfg.topic_edge_state_normal().qos));
        let _ = self.subscribe(&cfg.topic_edge_state_safe().topic, match_qos(cfg.topic_edge_state_safe().qos));
        let _ = self.subscribe(&cfg.topic_edge_phase().topic, match_qos(cfg.topic_edge_phase().qos));
        let _ = self.subscribe(&cfg.topic_edge_handshake().topic, match_qos(cfg.topic_edge_handshake().qos));
        let _ = self.subscribe(&cfg.topic_heartbeat().topic, match_qos(cfg.topic_heartbeat().qos));
        let _ = self.subscribe(&cfg.topic_new_firmware().topic, match_qos(cfg.topic_new_firmware().qos));
        let _ = self.subscribe(&cfg.topic_new_settings().topic, match_qos(cfg.topic_new_settings().qos));
        let _ = self.subscribe(&cfg.topic_edge_setting_ok().topic, match_qos(cfg.topic_edge_setting_ok().qos));
        let _ = self.subscribe(&cfg.topic_linkage_ack().topic, match_qos(cfg.topic_linkage_ack().qos));
    }
}

fn match_qos(qos: u8) -> QoS {
    match qos {
        0 => QoS::AtMostOnce,
        1 => QoS::AtLeastOnce,
        2 => QoS::ExactlyOnce,
        _ => QoS::AtMostOnce
    }
}