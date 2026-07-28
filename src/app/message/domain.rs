use crate::app::message::logic::{generator, parser};
use crate::app::system_settings::domain::SystemSettings;
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use heapless::{String, Vec};
use log::{error, info};
use serde::{Deserialize, Serialize};
use std::sync::{Arc, RwLock};

pub const NETWORK_STRING_LEN: usize = 20;
pub const WIFI_SSID_STRING_LEN: usize = 20;
pub const WIFI_PASSWORD_STRING_LEN: usize = 30;
pub const MQTT_URI_STRING_LEN: usize = 28;
pub const DEVICE_NAME_STRING_LEN: usize = 10;
pub const MAX_MSGPACK_BUFFER_SIZE: usize = 192;

pub enum MessageServiceResponse {
    Serialized(usize),
    SerializedBypass(usize),
    Message(usize),
}

pub enum MessageServiceCommand {
    ParseMessage(usize),

    Report {
        pulse_counter: f32,
        mq135_aqi: f32,
        dht11_temp: f32,
        dht11_hum: f32,
    },
    Monitor {
        timestamp: u64,
        uptime_sec: u64,
        heap_free: u32,
        heap_min_free: u32,
        heap_largest_block: u32,
    },
    AlertAir {
        initial_air_quality: f32,
        actual_air_quality: f32,
    },
    AlertTemp {
        initial_temp: f32,
        actual_temp: f32,
    },
    GenerateFirmwareOk((bool, bool)), // is_updated, success
    GenerateSettings(u32),            // message_id
    GenerateSettingsAck(u32),         // message_id
    EmptyQueuePhase {
        state: String<15>,
        phase: String<10>,
    },
    GenerateEmptyQueueSafe,
    GenerateLinkageRequest,
    GenerateHandshake((u32, String<15>)), // balance_epoch
    GenerateBypassAlertAir {
        initial_air_quality: f32,
        actual_air_quality: f32,
    },
    GenerateBypassAlertTemp {
        initial_temp: f32,
        actual_temp: f32,
    },
    GenerateHubState(String<20>),
}

pub struct MessageService {
    sender: Sender<MessageServiceResponse>,
    receiver: Receiver<MessageServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
    free_pool_index_rx: Receiver<usize>,
    free_pool_index_tx: Sender<usize>,
}

impl MessageService {
    pub fn new(
        sender: Sender<MessageServiceResponse>,
        receiver: Receiver<MessageServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
        free_pool_index_rx: Receiver<usize>,
        free_pool_index_tx: Sender<usize>,
    ) -> Self {
        info!("creando MessageService...");
        Self {
            sender,
            receiver,
            settings,
            free_pool_index_rx,
            free_pool_index_tx,
        }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>) {
        let (to_parser, from_service_parser) = bounded::<usize>(10);
        let (to_generator, from_service_generator) = bounded::<MessageServiceCommand>(10);
        let (tx, rx) = bounded::<MessageServiceResponse>(10);

        let settings_for_generator = Arc::clone(&self.settings);

        executor
            .spawn(parser(
                from_service_parser,
                tx.clone(),
                self.free_pool_index_tx.clone(),
            ))
            .detach();
        executor
            .spawn(generator(
                from_service_generator,
                tx.clone(),
                settings_for_generator,
                self.free_pool_index_rx.clone(),
                self.free_pool_index_tx.clone(),
            ))
            .detach();
        info!("iniciando MessageService...");
        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                Either::First(Ok(cmd)) => match cmd {
                    MessageServiceCommand::ParseMessage(idx) => {
                        if let Err(e) = to_parser.try_send(idx) {
                            error!(
                                "no se pudo enviar mensaje para parsear, mensaje descartado. {e}"
                            );
                            self.free_pool_index_tx.try_send(idx).unwrap();
                        }
                    }
                    _ => {
                        if let Err(e) = to_generator.try_send(cmd) {
                            error!(
                                "no se pudo enviar mensaje para generar, mensaje descartado. {e}"
                            );
                        }
                    }
                },
                Either::First(Err(_)) => {
                    error!("el canal receiver se ha cerrado.");
                    break;
                }

                Either::Second(Ok(msg)) => {
                    if let Err(e) = self.sender.try_send(msg) {
                        error!(
                            "no se pudo enviar mensaje MessageServiceResponse, mensaje descartado. {e}"
                        );
                        match e.into_inner() {
                            MessageServiceResponse::Serialized(idx)
                            | MessageServiceResponse::SerializedBypass(idx) => {
                                {
                                    crate::app::pool::pool::CORE_DATA_POOL[idx]
                                        .lock()
                                        .unwrap()
                                        .serialized = None;
                                }
                                self.free_pool_index_tx.try_send(idx).unwrap();
                            }
                            MessageServiceResponse::Message(idx) => {
                                {
                                    crate::app::pool::pool::CORE_DATA_POOL[idx]
                                        .lock()
                                        .unwrap()
                                        .from_edge = None;
                                }
                                self.free_pool_index_tx.try_send(idx).unwrap();
                            }
                        }
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
    pub sender_user_id: String<18>,
    #[serde(rename = "d")]
    pub destination_id: String<18>,
    #[serde(rename = "t")]
    pub timestamp: u64,
}

/// Mediciones de sensores ambientales y operativos.
///
/// Representa el paquete de datos principal generado por los nodos.
#[derive(Clone, Serialize, Deserialize)]
pub struct Measurement {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "pc")]
    pub pulse_counter: f32,
    #[serde(rename = "t")]
    pub temperature: f32,
    #[serde(rename = "h")]
    pub humidity: f32,
    #[serde(rename = "aq")]
    pub air_quality: f32,
    #[serde(rename = "s")]
    pub sample: u16,
}

impl Measurement {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_data().topic.clone(),
            settings.read().unwrap().topic_data().qos,
            settings.read().unwrap().topic_data().retain,
        )
    }
}

/// Alerta de calidad de aire.
#[derive(Clone, Serialize, Deserialize)]
pub struct AlertAir {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "ia")]
    pub initial_air_quality: f32,
    #[serde(rename = "aa")]
    pub actual_air_quality: f32,
}

impl AlertAir {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_alert_air().topic.clone(),
            settings.read().unwrap().topic_alert_air().qos,
            settings.read().unwrap().topic_alert_air().retain,
        )
    }
}

/// Alerta de Temperatura y Humedad.
#[derive(Clone, Serialize, Deserialize)]
pub struct AlertTh {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "i")]
    pub initial_temp: f32,
    #[serde(rename = "a")]
    pub actual_temp: f32,
}

impl AlertTh {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_alert_temp().topic.clone(),
            settings.read().unwrap().topic_alert_temp().qos,
            settings.read().unwrap().topic_alert_temp().retain,
        )
    }
}

/// Datos de telemetría y salud del Hub.
/// Incluye información sobre memoria, stack y conectividad para diagnóstico.
#[derive(Clone, Serialize, Deserialize)]
pub struct Monitor {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "hf")]
    pub heap_free: u32,
    #[serde(rename = "hm")]
    pub heap_min_free: u32,
    #[serde(rename = "hb")]
    pub heap_largest_block: u32,
    #[serde(rename = "ut")]
    pub uptime_sec: u64,
}

impl Monitor {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_monitor().topic.clone(),
            settings.read().unwrap().topic_monitor().qos,
            settings.read().unwrap().topic_monitor().retain,
        )
    }
}

/// Configuración del dispositivo.
/// Contiene credenciales WiFi/MQTT y parámetros operativos.
#[derive(Clone, Serialize, Deserialize)]
pub struct Settings {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "mi")]
    pub message_id: u32,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "ws")]
    pub wifi_ssid: String<WIFI_SSID_STRING_LEN>,
    #[serde(rename = "wp")]
    pub wifi_password: String<WIFI_PASSWORD_STRING_LEN>,
    #[serde(rename = "mu")]
    pub mqtt_uri: String<MQTT_URI_STRING_LEN>,
    #[serde(rename = "dn")]
    pub device_name: String<DEVICE_NAME_STRING_LEN>,
    #[serde(rename = "s")]
    pub sample: u16,
    #[serde(rename = "e")]
    pub energy_mode: u32,
}

impl Settings {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_settings().topic.clone(),
            settings.read().unwrap().topic_settings().qos,
            settings.read().unwrap().topic_settings().retain,
        )
    }
}

/// Mensaje de Handshake.
#[derive(Clone, Serialize, Deserialize)]
pub struct Handshake {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "f")]
    pub flag: String<15>,
    #[serde(rename = "b")]
    pub balance_epoch: u32,
}

impl Handshake {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings
                .read()
                .unwrap()
                .topic_handshake_to_edge()
                .topic
                .clone(),
            settings.read().unwrap().topic_handshake_to_edge().qos,
            settings.read().unwrap().topic_handshake_to_edge().retain,
        )
    }
}

/// Envio periódico de estado al servidor.
/// Si el Edge esta caido, se deja de enviar.
/// Se reanuda con el envío cuando el Edge vuelve.
/// El campo state puede ser: normal, balance, safe
#[derive(Clone, Serialize, Deserialize)]
pub struct HubState {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "s")]
    pub state: String<20>,
}

impl HubState {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_hub_state().topic.clone(),
            settings.read().unwrap().topic_hub_state().qos,
            settings.read().unwrap().topic_hub_state().retain,
        )
    }
}

/// Mensaje enviado por el Edge indicando su estado actual.
/// Cada estado tiene atributos importantes para el, por ende
/// todos los campos estan definidas en una misma estructura
/// y cuando se envia un estado que no tiene implicancias en
/// otras variables, simplemente las envia vacias o nulas.
/// De todos modos, el Hub debe validar el campo "state" y
/// en base a eso inspeccionar otras varibales o no.
#[derive(Clone, Serialize, Deserialize)]
pub struct EdgeState {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String<20>,

    #[serde(rename = "b")]
    pub balance_epoch: u32,
    #[serde(rename = "d")]
    pub duration: u32,

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
    pub state: String<15>,
    #[serde(rename = "e")]
    pub epoch: u32,
    #[serde(rename = "p")]
    pub phase: String<10>,
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

/// Confirmación de recepción de configuración (Handshake bidireccional).
#[derive(Clone, Serialize, Deserialize)]
pub struct SettingOk {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "i")]
    pub message_id: u32,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "h")]
    pub handshake: bool,
}

impl SettingOk {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_settings_ok().topic.clone(),
            settings.read().unwrap().topic_settings_ok().qos,
            settings.read().unwrap().topic_settings_ok().retain,
        )
    }
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FirmwareRequest {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "v")]
    pub version: String<10>,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FirmwareResponse {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "u")]
    pub is_updated: bool,
    #[serde(rename = "s")]
    pub success: bool,
}

impl FirmwareResponse {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings
                .read()
                .unwrap()
                .topic_hub_firmware_ok()
                .topic
                .clone(),
            settings.read().unwrap().topic_hub_firmware_ok().qos,
            settings.read().unwrap().topic_hub_firmware_ok().retain,
        )
    }
}

#[derive(Serialize, Deserialize, Clone)]
pub struct EmptyQueue {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String<15>,
    #[serde(rename = "p")]
    pub phase: String<10>,
    #[serde(rename = "q")]
    pub queue_empty: bool,
}

impl EmptyQueue {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings.read().unwrap().topic_empty_queue().topic.clone(),
            settings.read().unwrap().topic_empty_queue().qos,
            settings.read().unwrap().topic_empty_queue().retain,
        )
    }
}

#[derive(Serialize, Deserialize, Clone)]
pub struct EmptyQueueSafeMode {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "s")]
    pub state: String<15>,
    #[serde(rename = "q")]
    pub queue_empty: bool,
}

impl EmptyQueueSafeMode {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings
                .read()
                .unwrap()
                .topic_empty_queue_safe()
                .topic
                .clone(),
            settings.read().unwrap().topic_empty_queue_safe().qos,
            settings.read().unwrap().topic_empty_queue_safe().retain,
        )
    }
}

#[derive(Serialize, Deserialize, Clone)]
pub struct LinkageRequest {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "d")]
    pub device_name: String<DEVICE_NAME_STRING_LEN>,
    #[serde(rename = "n")]
    pub network: String<NETWORK_STRING_LEN>,
    #[serde(rename = "l")]
    pub linkage_request: bool,
}

impl LinkageRequest {
    pub fn resolve_topic(&self, settings: &Arc<RwLock<SystemSettings>>) -> (String<75>, u8, bool) {
        (
            settings
                .read()
                .unwrap()
                .topic_linkage_request()
                .topic
                .clone(),
            settings.read().unwrap().topic_linkage_request().qos,
            settings.read().unwrap().topic_linkage_request().retain,
        )
    }
}

#[derive(Serialize, Deserialize, Clone)]
pub struct LinkageAck {
    #[serde(rename = "m")]
    pub metadata: Metadata,
    #[serde(rename = "l")]
    pub linkage_ack: bool,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(untagged)]
pub enum MessageFromEdge {
    // Mensajes de bajada
    UpdateFirmware(FirmwareRequest),
    FromServerSettings(Settings),
    FromServerSettingsAck(SettingOk),
    Heartbeat(Heartbeat),
    HandshakeToHub(Handshake),
    PhaseNotification(PhaseNotification),
    LinkageAck(LinkageAck),
    State(EdgeState),
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
    FirmwareOk(FirmwareResponse),
    FromHubSettings(Settings),
    FromHubSettingsAck(SettingOk),
    EmptyQueue(EmptyQueue),
    EmptyQueueSafe(EmptyQueueSafeMode),
    LinkageRequest(LinkageRequest),
    State(HubState),
}

/// Representación final de un mensaje listo para ser enviado por MQTT.
/// Contiene el payload binario (serializado) y los parámetros de transporte.
#[derive(Serialize, Deserialize)]
pub struct SerializedMessage {
    topic: String<75>,
    payload: Vec<u8, MAX_MSGPACK_BUFFER_SIZE>,
    qos: u8,
    retain: bool,
}

impl SerializedMessage {
    pub fn new(
        topic: String<75>,
        payload: Vec<u8, MAX_MSGPACK_BUFFER_SIZE>,
        qos: u8,
        retain: bool,
    ) -> Self {
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
