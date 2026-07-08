//! Módulo MQTT.
//! Abstrae la implementación específica del hardware detrás de traits.


use crate::svc::mqtt::Mqtt;
use async_channel::Sender;
use esp_idf_svc::mqtt::client::{
    EspMqttClient, Event as MqttEvent, MqttClientConfiguration, MqttProtocolVersion, QoS,
};
use log::{debug, error, info, warn};
use std::sync::{Arc, Mutex, RwLock};
use crate::app::system_settings::domain::SystemSettings;
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
    settings: Arc<RwLock<SystemSettings>>
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
        settings: Arc<RwLock<SystemSettings>>
    ) -> Result<Self, String> {
        // Configuración
        let config = MqttClientConfiguration {
            client_id: Some(client_id),
            protocol_version: MqttProtocolVersion::V5,
            keep_alive: Duration::from_secs(60),
            network_timeout: Duration::from_secs(30),
            crt_bundle_attach: None,
            server_certificate: Some(root_ca),
            client_certificate: Some(hub_cert),
            private_key: Some(hub_key),
            ..Default::default()
        };

        /*
        broker: Configuración relacionada con el servidor MQTT .
        address: Define la dirección del broker. Puedes usar la uri completa o campos específicos como hostname, port y transport .
        verification: Configuración para la verificación de seguridad del broker (como certificados TLS) .
        credentials: Configura las credenciales del cliente para autenticarse con el broker .
        username: Nombre de usuario.
        password: Contraseña.
        session: Configuración de la sesión MQTT .
        keepalive: Intervalo de keep-alive en segundos (por defecto 120) .
        disable_clean_session: Si es false (por defecto), la sesión se limpia al conectar .
        last_will: Estructura para configurar el mensaje de "última voluntad" (LWT) .
        network: Configuración de la red, como el timeout de reconexión (reconnect_timeout_ms) .
        task: Configuración de la tarea de FreeRTOS que maneja el cliente MQTT .
        priority: Prioridad de la tarea.
        stack_size: Tamaño de la pila de la tarea.
        buffer: Configuración de los tamaños de buffer de envío y recepción .
        outbox: Configuración de la bandeja de salida para mensajes pendientes 
         */


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
                        Ok() => {}
                        Err(e) => error!("no se pudo enviar MqttData por el canal. {e}"),
                    }
                }

                Ok(MqttEvent::Disconnected) => {
                    warn!("desconectado del broker");
                    match data_tx.try_send(MqttData::Disconnected) {
                        Ok() => {}
                        Err(e) => error!("no se pudo enviar MqttData por el canal. {e}"),
                    }
                }

                Ok(MqttEvent::Published(msg_id)) => {
                    debug!("puback recibido para msg_id: {msg_id}");
                    match data_tx.try_send(MqttData::PubAck {
                        msg_id: *msg_id as u16,
                        return_code: 0,
                    }) {
                        Ok() => {}
                        Err(e) => error!("no se pudo enviar MqttData por el canal.{e}"),
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
                        if let Err(e) = data_tx.try_send(MqttData::IncomingMessage(parsed_msg)) {
                            warn!("no se pudo enviar mensaje para parsear, mensaje descartado. {e}");
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

    fn enable_subscriptions(&mut self) {
        let mut enabled = self.subscriptions_enabled.lock().unwrap();
        if !*enabled {
            *enabled = true;

            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_state_balance().topic, match_qos(self.settings.read().unwrap().topic_edge_state_balance().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_state_normal().topic, match_qos(self.settings.read().unwrap().topic_edge_state_normal().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_state_safe().topic, match_qos(self.settings.read().unwrap().topic_edge_state_safe().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_phase().topic, match_qos(self.settings.read().unwrap().topic_edge_phase().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_handshake().topic, match_qos(self.settings.read().unwrap().topic_edge_handshake().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_heartbeat().topic, match_qos(self.settings.read().unwrap().topic_heartbeat().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_new_firmware().topic, match_qos(self.settings.read().unwrap().topic_new_firmware().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_new_settings().topic, match_qos(self.settings.read().unwrap().topic_new_settings().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_edge_setting_ok().topic, match_qos(self.settings.read().unwrap().topic_edge_setting_ok().qos));
            let _ = self.subscribe(&self.settings.read().unwrap().topic_linkage_ack().topic, match_qos(self.settings.read().unwrap().topic_linkage_ack().qos));
        }
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