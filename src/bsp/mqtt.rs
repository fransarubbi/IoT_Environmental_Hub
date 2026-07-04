//! Módulo MQTT.
//! Abstrae la implementación específica del hardware detrás de traits.

use crate::bsp::mqtt::Mqtt;
use async_channel::Sender;
use esp_idf_svc::mqtt::client::{
    EspError, EspMqttClient, Event as MqttEvent, MqttClientConfiguration, MqttProtocolVersion, QoS,
};
use log::{debug, error, info, warn};
use std::sync::{Arc, Mutex};
use std::time::Duration;

#[derive(Debug)]
pub enum MqttData {
    Connected,
    Disconnected,
    PubAck { msg_id: u16, return_code: u8 },
    Message(IncomingMessage),
}

/// Estructura que contiene el mensaje binario y el tópico donde se recibió.
#[derive(Debug)]
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
    // El cliente nativo de esp-idf-svc
    client: EspMqttClient<'static>,
}

impl EspIdfMqttManager {
    /// Constructor. Recibe canales para inyectar eventos al resto del sistema.
    pub fn new(
        uri: &str,
        client_id: &str,
        data_tx: Sender<MqttData>,
        hub_cert: &[u8],
        hub_key: &[u8],
        root_ca: &[u8],
    ) -> Result<Self, String> {
        // Configuración
        let config = MqttClientConfiguration {
            client_id: Some(client_id),
            protocol_version: MqttProtocolVersion::V5,
            keep_alive: Duration::from_secs(60),
            network_timeout: Duration::from_secs(30),
            disable_auto_reconnect: false,
            crt_bundle_attach: None,
            server_certificate: Some(root_ca),
            client_certificate: Some(hub_cert),
            private_key: Some(hub_key),
            ..Default::default()
        };

        // Estado local para reensamblar fragmentos dentro del callback
        let rx_state = Arc::new(Mutex::new(RxState {
            topic: String::new(),
            buffer: Vec::new(),
        }));

        // Inicializamos el cliente con su Callback de eventos
        // Este closure se ejecuta en un hilo de fondo de FreeRTOS por ESP-IDF
        let client = EspMqttClient::new_cb(uri, &config, move |event| {
            match event {
                Ok(MqttEvent::Connected(_)) => {
                    info!("conectado al broker MQTT");
                    // Enviar evento de conectado (no bloqueante)
                    match data_tx.try_send(MqttData::Connected) {
                        Ok => {}
                        Err => error!("no se pudo enviar MqttData por el canal."),
                    }
                }

                Ok(MqttEvent::Disconnected) => {
                    warn!("desconectado del broker");
                    match data_tx.try_send(MqttData::Disconnected) {
                        Ok => {}
                        Err => error!("no se pudo enviar MqttData por el canal."),
                    }
                }

                Ok(MqttEvent::Published(msg_id)) => {
                    debug!("puback recibido para msg_id: {msg_id}");
                    match data_tx.try_send(MqttData::PubAck {
                        msg_id: *msg_id as u16,
                        return_code: 0,
                    }) {
                        Ok => {}
                        Err => error!("no se pudo enviar MqttData por el canal."),
                    }
                }

                Ok(MqttEvent::Received(msg)) => {
                    let mut state = rx_state.lock().unwrap();

                    // Si es el inicio del mensaje (offset 0)
                    if msg.offset() == 0 {
                        // Limpiamos y pre-alocamos memoria para el total del mensaje
                        state.buffer.clear();
                        state.buffer.reserve(msg.total_len() as usize);

                        // Guardamos el tópico solo en el primer fragmento recibido
                        if let Some(topic) = msg.topic() {
                            state.topic = topic.to_string();
                        }
                    }

                    // Extendemos nuestro vector con el fragmento actual
                    state.buffer.extend_from_slice(msg.data());

                    // Si ya recibimos todo
                    if state.buffer.len() == msg.total_len() as usize {
                        let parsed_msg = IncomingMessage {
                            topic: state.topic.clone(),
                            payload: std::mem::take(&mut state.buffer),
                        };

                        // Enviamos para parsear
                        if let Err(_) = data_tx.try_send(MqttData::IncomingMessage(parsed_msg)) {
                            warn!("no se pudo enviar mensaje para parsear, mensaje descartado.");
                        }
                    }
                }

                Err(e) => {
                    error!("error en cliente MQTT: {:?}", e);
                }
                _ => {}
            }
        })
        .map_err(|e| format!("error creando cliente: {}", e))?;

        Ok(Self { client })
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

    fn enable_subscriptions(&mut self, id_edge: String, id_network: String) {
        let mut enabled = self.subscriptions_enabled.lock().unwrap();
        if !*enabled {
            *enabled = true;
            let topic_balance_state = format!("iot/{id_edge}/state/balance");
            let topic_normal_state = format!("iot/{id_edge}/state/normal");
            let topic_safe_state = format!("iot/{id_edge}/state/safe");
            let topic_phase_state = format!("iot/{id_edge}/state/phase");
            let topic_handshake = format!("iot/{id_edge}/handshake");
            let topic_heartbeat = format!("iot/{id_edge}/heartbeat");
            let topic_new_firmware = format!("iot/{id_network}/new_firmware");
            let topic_new_setting = format!("iot/{id_network}/new_setting");
            let topic_new_setting_ok = format!("iot/{id_network}/new_setting_ok");
            let topic_linkage_ack = format!("iot/{id_edge}/linkage_ack");

            match self.subscribe(topic_balance_state, QoS::AtLeastOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_balance_state}"),
            }
            match self.subscribe(topic_normal_state, QoS::AtLeastOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_normal_state}"),
            }
            match self.subscribe(topic_safe_state, QoS::AtLeastOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_safe_state}"),
            }
            match self.subscribe(topic_phase_state, QoS::AtLeastOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_phase_state}"),
            }
            match self.subscribe(topic_handshake, QoS::AtLeastOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_handshake}"),
            }
            match self.subscribe(topic_heartbeat, QoS::AtMostOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_heartbeat}"),
            }
            match self.subscribe(topic_new_firmware, QoS::AtMostOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_new_firmware}"),
            }
            match self.subscribe(topic_new_setting, QoS::AtMostOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_new_setting}"),
            }
            match self.subscribe(topic_new_setting_ok, QoS::AtMostOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_new_setting_ok}"),
            }
            match self.subscribe(topic_linkage_ack, QoS::AtMostOnce) {
                Ok => {}
                EspError => error!("no se pudo subscribir a topico {topic_linkage_ack}"),
            }
        }
    }
}

/*
    "iot/{id_network}/ping"
*/
