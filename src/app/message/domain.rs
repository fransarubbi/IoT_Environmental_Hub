use serde::{Deserialize, Serialize};
use async_channel::{bounded, Sender, Receiver};
use edge_executor::LocalExecutor;
use log::{error};
use embassy_futures::select::{select, Either};
use std::sync::{Arc, RwLock};
use crate::app::system_settings::domain::SystemSettings;
use crate::bsp::mqtt::IncomingMessage;
use crate::app::message::logic::{parser, generator};


pub enum MessageServiceResponse {
    Serialized(SerializedMessage),
    Message(MessageFromEdge),
}

pub enum MessageServiceCommand {
    ParseMessage(IncomingMessage),
    GenerateMessage(MessageToEdge),
}

pub struct MessageService {
    sender: Sender<MessageServiceResponse>,
    receiver:Receiver<MessageServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>
}

impl MessageService {
    pub fn new(
        sender: Sender<MessageServiceResponse>,
        receiver: Receiver<MessageServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>
    ) -> Self {
        Self {
            sender,
            receiver,
            settings
        }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>) {

        let (to_parser, from_service_parser) = bounded::<IncomingMessage>(10);
        let (to_generator, from_service_generator) = bounded::<MessageToEdge>(10);
        let (tx, rx) = bounded::<MessageServiceResponse>(10);

        let settings_for_generator = Arc::clone(&self.settings);

        executor.spawn(parser(from_service_parser, tx.clone())).detach();
        executor.spawn(generator(from_service_generator, tx.clone(), settings_for_generator)).detach();

        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                // Caso 1: Recibimos un comando del exterior (receiver)
                Either::First(Ok(cmd)) => {
                    match cmd {
                        MessageServiceCommand::ParseMessage(msg) => {
                            if let Err(e) = to_parser.try_send(msg) {
                                error!("no se pudo enviar mensaje para parsear, mensaje descartado. {e}");
                            }
                        },
                        MessageServiceCommand::GenerateMessage(msg) => {
                            if let Err(e) = to_generator.try_send(msg) {
                                error!("no se pudo enviar mensaje para generar, mensaje descartado. {e}");
                            }
                        },
                    }
                }
                Either::First(Err(_)) => {
                    error!("el canal receiver se ha cerrado.");
                    break;
                }

                // Caso 2: Recibimos una respuesta interna de los submódulos (rx)
                Either::Second(Ok(msg)) => {
                    if let Err(e) = self.sender.try_send(msg) {
                        error!("no se pudo enviar mensaje MessageServiceResponse, mensaje descartado. {e}");
                    }
                }
                Either::Second(Err(_)) => {
                    error!("el canal interno rx se ha cerrado.");
                    break;
                }
            }
        }
    }
}




/// Metadatos estándar para todos los mensajes del sistema.
///
/// Proporciona contexto de trazabilidad, origen y destino para cada paquete de datos.
#[derive(Clone, Serialize, Deserialize)]
pub struct Metadata {
    #[serde(rename = "s")]
    pub sender_user_id: String,
    #[serde(rename = "d")]
    pub destination_id: String,
    #[serde(rename = "t")]
    pub timestamp: i64,
}

/// Mediciones de sensores ambientales y operativos.
///
/// Representa el paquete de datos principal generado por los nodos.
#[derive(Clone, Serialize, Deserialize)]
pub struct Measurement {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "pc")]
    pub pulse_counter: i64,
    #[serde(rename = "pm")]
    pub pulse_max_duration: i64,
    #[serde(rename = "t")]
    pub temperature: f32,
    #[serde(rename = "h")]
    pub humidity: f32,
    #[serde(rename = "aq")]
    pub air_quality: f32,
    #[serde(rename = "s")]
    pub sample: u16,
}

/// Alerta de calidad de aire.
#[derive(Clone, Serialize, Deserialize)]
pub struct AlertAir {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "i")]
    pub initial_air_quality: f32,
    #[serde(rename = "a")]
    pub actual_air_quality: f32,
}

/// Alerta de Temperatura y Humedad.
#[derive(Clone, Serialize, Deserialize)]
pub struct AlertTh {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "i")]
    pub initial_temp: f32,
    #[serde(rename = "a")]
    pub actual_temp: f32,
}

/// Datos de telemetría y salud del Hub.
/// Incluye información sobre memoria, stack y conectividad para diagnóstico.
#[derive(Clone, Serialize, Deserialize)]
pub struct Monitor {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "hf")]
    pub heap_free: u32,
    #[serde(rename = "hm")]
    pub heap_min_free: u32,
    #[serde(rename = "hb")]
    pub heap_largest_block: u32,
    #[serde(rename = "ut")]
    pub uptime_sec: u64,
    #[serde(rename = "ws")]
    pub wifi_ssid: String,
    #[serde(rename = "wr")]
    pub wifi_rssi: i8,
}

/// Configuración del dispositivo.
/// Contiene credenciales WiFi/MQTT y parámetros operativos.
#[derive(Clone, Serialize, Deserialize)]
pub struct Settings {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "mi")]
    pub message_id: u64,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "ws")]
    pub wifi_ssid: String,
    #[serde(rename = "wp")]
    pub wifi_password: String,
    #[serde(rename = "mu")]
    pub mqtt_uri: String,
    #[serde(rename = "dn")]
    pub device_name: String,
    #[serde(rename = "s")]
    pub sample: u32,
    #[serde(rename = "e")]
    pub energy_mode: u32,
}

/// Mensaje de Handshake.
#[derive(Clone, Serialize, Deserialize)]
pub struct Handshake {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "f")]
    pub flag: String,
    #[serde(rename = "b")]
    pub balance_epoch: u32,
}

/// Notificación de cambio a Modo Balance.
#[derive(Clone, Serialize, Deserialize)]
pub struct MessageStateBalanceMode {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
    #[serde(rename = "b")]
    pub balance_epoch: u32,
    #[serde(rename = "d")]
    pub duration: u32,
}

/// Notificación de cambio a Modo Normal.
#[derive(Clone, Serialize, Deserialize)]
pub struct MessageStateNormal {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
}

/// Notificación de cambio a Modo Seguro.
#[derive(Clone, Serialize, Deserialize)]
pub struct MessageStateSafeMode {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
    #[serde(rename = "f")]
    pub frequency: u32,
    #[serde(rename = "j")]
    pub jitter: u32,
}

/// Notificación de cambio de Fase dentro del modo Balance.
#[derive(Clone, Serialize, Deserialize)]
pub struct PhaseNotification {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
    #[serde(rename = "e")]
    pub epoch: u32,
    #[serde(rename = "p")]
    pub phase: String,
    #[serde(rename = "f")]
    pub frequency: u32,
    #[serde(rename = "j")]
    pub jitter: u32,
}

/// Mensaje de latido (Heartbeat) para indicar a los Hubs que el Edge está vivo.
#[derive(Clone, Serialize, Deserialize)]
pub struct Heartbeat {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "b")]
    pub beat: bool,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct Ping {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "p")]
    pub ping: bool,
}

/// Confirmación de recepción de configuración (Handshake bidireccional).
#[derive(Clone, Serialize, Deserialize)]
pub struct SettingOk {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "i")]
    pub message_id: u64,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "h")]
    pub handshake: bool,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FirmwareOk {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "v")]
    pub version: String,
    #[serde(rename = "o")]
    pub is_ok: bool,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct EmptyQueue {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
    #[serde(rename = "p")]
    pub phase: String,
    #[serde(rename = "q")]
    pub queue_empty: bool,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct EmptyQueueSafeMode {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String,
    #[serde(rename = "q")]
    pub queue_empty: bool,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct LinkageRequest {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "d")]
    pub device_name: String,
    #[serde(rename = "n")]
    pub network: String,
    #[serde(rename = "l")]
    pub linkage_request: bool,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct LinkageAck {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "l")]
    pub linkage_ack: bool,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct UpdateFirmware {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(untagged)]
pub enum MessageFromEdge {
    // Mensajes de bajada
    UpdateFirmware(UpdateFirmware),
    FromServerSettings(Settings),
    FromServerSettingsAck(SettingOk),
    Heartbeat(Heartbeat),
    HandshakeToHub(Handshake),
    PhaseNotification(PhaseNotification),
    StateBalanceMode(MessageStateBalanceMode),
    StateNormal(MessageStateNormal),
    StateSafeMode(MessageStateSafeMode),
    LinkageAck(LinkageAck),
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(untagged)]
pub enum MessageToEdge {
    // Mensajes de subida
    Report(Measurement),
    Monitor(Monitor),
    AlertAir(AlertAir),
    AlertTem(AlertTh),
    HandshakeToEdge(Handshake),
    FirmwareOk(FirmwareOk),
    FromHubSettings(Settings),
    FromHubSettingsAck(SettingOk),
    EmptyQueue(EmptyQueue),
    EmptyQueueSafe(EmptyQueueSafeMode),
    Ping(Ping),
    LinkageRequest(LinkageRequest),
}


/// Representación final de un mensaje listo para ser enviado por MQTT.
/// Contiene el payload binario (serializado) y los parámetros de transporte.
#[derive(Serialize, Deserialize)]
pub struct SerializedMessage {
    topic: String,
    payload: Vec<u8>,
    qos: u8,
    retain: bool,
}

impl SerializedMessage {
    pub fn new(topic: String, payload: Vec<u8>, qos: u8, retain: bool) -> Self {
        Self {
            topic,
            payload,
            qos,
            retain,
        }
    }
    pub fn get_topic(&self) -> &str {
        &self.topic
    }
    pub fn get_payload(&self) -> &[u8] {
        &self.payload
    }
    pub fn get_qos(&self) -> u8 {
        self.qos
    }
    pub fn get_retain(&self) -> bool {
        self.retain
    }
}