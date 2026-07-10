//! Definición de la configuración compartida del sistema.
//!
//! Este módulo implementa el patrón de **Estado Compartido**.
//! El `SystemSettings` actúa como el repositorio central de parámetros vitales.

use serde::{Deserialize, Serialize};

/// Comandos para el gestor de configuración.
pub enum ConfigCommand {
    /// Reemplaza toda la configuración
    UpdateConfig(SystemSettings),
    /// Actualiza solo un campo específico
    UpdateField(ConfigField),
    /// Obliga a leer nuevamente desde NVS
    Reload,
    /// Obliga a guardar la configuración actual en NVS
    Save,
    /// Pregunta si existia o no config en NVS
    ThereIsSettingsInNVS,
}

/// Campos que se pueden actualizar individualmente.
pub enum ConfigField {
    BalanceEpoch(u32),
    LinkageFlag(bool),
    MessageId(u32),
}

pub enum ConfigResponse {
    ExistsInNVS,
    NotExistsInNVS,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct SystemSettings {
    id: IdentificationData,
    sample_rate: u16, // Segundos
    heartbeat: Heartbeat,
    network: Network,
    wifi: Wifi,
    mqtt_uri: String,

    // Publica
    topic_data: Topic,
    topic_alert_air: Topic,
    topic_alert_temp: Topic,
    topic_monitor: Topic,
    topic_settings: Topic,
    topic_settings_ok: Topic,
    topic_hub_firmware_ok: Topic,
    topic_handshake_to_edge: Topic,
    topic_ping: Topic,
    topic_empty_queue: Topic,
    topic_linkage_request: Topic,

    // Escucha
    topic_edge_state_normal: Topic,
    topic_edge_state_balance: Topic,
    topic_edge_state_safe: Topic,
    topic_edge_phase: Topic,
    topic_edge_handshake: Topic,
    topic_heartbeat: Topic,
    topic_new_firmware: Topic,
    topic_new_settings: Topic,
    topic_edge_setting_ok: Topic,
    topic_linkage_ack: Topic,
    topic_ping_ack: Topic,

    energy_mode: EnergyMode,
    filters: Filters,

    air_r0: f32,
}

#[derive(Debug, Clone)]
pub struct MqttTopic {}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct IdentificationData {
    pub mac_addr: String,
    pub device_name: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct Heartbeat {
    pub time_between_heartbeats_balance_mode: u32,
    pub time_between_heartbeats_normal_mode: u32,
    pub time_between_heartbeats_safe_mode: u32,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct Network {
    pub id_network: String,
    pub id_edge: String,
    pub balance_epoch: u32,
    pub url_bypass: String,
    pub linkage_flag: bool,
    pub message_id: u32,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct Wifi {
    pub ssid: String,
    pub password: String,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Topic {
    pub topic: String,
    pub qos: u8,
    pub retain: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct Filters {
    pub air_alpha_ema: f32,
    pub temp_alpha_ema: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub enum EnergyMode {
    #[default]
    LOW,
    NORMAL,
    PERFORMANCE,
}

impl EnergyMode {
    pub fn from_u32(val: u32) -> Option<Self> {
        match val {
            0 => Some(Self::LOW),
            1 => Some(Self::NORMAL),
            2 => Some(Self::PERFORMANCE),
            _ => None,
        }
    }

    pub fn label(&self) -> &'static str {
        match self {
            Self::LOW => "LOW (Ahorro)",
            Self::NORMAL => "NORMAL (Medio)",
            Self::PERFORMANCE => "PERFORMANCE (Máximo)",
        }
    }

    pub fn as_str(&self) -> &'static str {
        self.label()
    }
}

impl SystemSettings {
    // --- Identificación ---
    pub fn mac_addr(&self) -> &str {
        self.id.mac_addr.as_str()
    }
    pub fn set_mac_addr(&mut self, mac: String) {
        self.id.mac_addr = mac;
    }

    pub fn device_name(&self) -> &str {
        self.id.device_name.as_str()
    }
    pub fn set_device_name(&mut self, name: String) {
        self.id.device_name = name;
    }

    // --- Sample Rate ---
    pub fn sample_rate(&self) -> u16 {
        self.sample_rate
    }
    pub fn set_sample_rate(&mut self, rate: u16) {
        self.sample_rate = rate;
    }

    // --- Heartbeat ---
    pub fn heartbeat_balance_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_balance_mode
    }
    pub fn set_heartbeat_balance_mode(&mut self, val: u32) {
        self.heartbeat.time_between_heartbeats_balance_mode = val;
    }

    pub fn heartbeat_normal_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_normal_mode
    }
    pub fn set_heartbeat_normal_mode(&mut self, val: u32) {
        self.heartbeat.time_between_heartbeats_normal_mode = val;
    }

    pub fn heartbeat_safe_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_safe_mode
    }
    pub fn set_heartbeat_safe_mode(&mut self, val: u32) {
        self.heartbeat.time_between_heartbeats_safe_mode = val;
    }

    // --- Network ---
    pub fn id_network(&self) -> &str {
        self.network.id_network.as_str()
    }
    pub fn set_id_network(&mut self, id: String) {
        self.network.id_network = id;
    }

    pub fn id_edge(&self) -> &str {
        self.network.id_edge.as_str()
    }
    pub fn set_id_edge(&mut self, id: String) {
        self.network.id_edge = id;
    }

    pub fn balance_epoch(&self) -> u32 {
        self.network.balance_epoch
    }
    pub fn set_balance_epoch(&mut self, epoch: u32) {
        self.network.balance_epoch = epoch;
    }

    pub fn url_bypass(&self) -> &str {
        self.network.url_bypass.as_str()
    }
    pub fn set_url_bypass(&mut self, url: String) {
        self.network.url_bypass = url;
    }

    pub fn linkage_flag(&self) -> bool {
        self.network.linkage_flag
    }
    pub fn set_linkage_flag(&mut self, flag: bool) {
        self.network.linkage_flag = flag;
    }

    pub fn message_id(&self) -> u32 {
        self.network.message_id
    }
    pub fn set_message_id(&mut self, id: u32) {
        self.network.message_id = id;
    }

    // --- Wifi y URI ---
    pub fn wifi_ssid(&self) -> &str {
        self.wifi.ssid.as_str()
    }
    pub fn set_wifi_ssid(&mut self, ssid: String) {
        self.wifi.ssid = ssid;
    }

    pub fn wifi_password(&self) -> &str {
        self.wifi.password.as_str()
    }
    pub fn set_wifi_password(&mut self, pass: String) {
        self.wifi.password = pass;
    }

    pub fn mqtt_uri(&self) -> &str {
        self.mqtt_uri.as_str()
    }
    pub fn set_mqtt_uri(&mut self, uri: String) {
        self.mqtt_uri = uri;
    }

    // --- Energía y Filtros ---
    pub fn energy_mode(&self) -> EnergyMode {
        self.energy_mode
    }
    pub fn set_energy_mode(&mut self, mode: EnergyMode) {
        self.energy_mode = mode;
    }

    pub fn air_alpha_ema(&self) -> f32 {
        self.filters.air_alpha_ema
    }
    pub fn set_air_alpha_ema(&mut self, val: f32) {
        self.filters.air_alpha_ema = val;
    }

    pub fn temp_alpha_ema(&self) -> f32 {
        self.filters.temp_alpha_ema
    }
    pub fn set_temp_alpha_ema(&mut self, val: f32) {
        self.filters.temp_alpha_ema = val;
    }

    pub fn air_r0(&self) -> f32 {
        self.air_r0
    }
    pub fn set_air_r0(&mut self, val: f32) {
        self.air_r0 = val;
    }

    // --- GETTERS & SETTERS PARA LOS TOPICS (Publicación) ---
    pub fn topic_data(&self) -> &Topic {
        &self.topic_data
    }
    pub fn set_topic_data(&mut self, t: Topic) {
        self.topic_data = t;
    }

    pub fn topic_alert_air(&self) -> &Topic {
        &self.topic_alert_air
    }
    pub fn set_topic_alert_air(&mut self, t: Topic) {
        self.topic_alert_air = t;
    }

    pub fn topic_alert_temp(&self) -> &Topic {
        &self.topic_alert_temp
    }
    pub fn set_topic_alert_temp(&mut self, t: Topic) {
        self.topic_alert_temp = t;
    }

    pub fn topic_monitor(&self) -> &Topic {
        &self.topic_monitor
    }
    pub fn set_topic_monitor(&mut self, t: Topic) {
        self.topic_monitor = t;
    }

    pub fn topic_settings(&self) -> &Topic {
        &self.topic_settings
    }
    pub fn set_topic_settings(&mut self, t: Topic) {
        self.topic_settings = t;
    }

    pub fn topic_settings_ok(&self) -> &Topic {
        &self.topic_settings_ok
    }
    pub fn set_topic_settings_ok(&mut self, t: Topic) {
        self.topic_settings_ok = t;
    }

    pub fn topic_hub_firmware_ok(&self) -> &Topic {
        &self.topic_hub_firmware_ok
    }
    pub fn set_topic_hub_firmware_ok(&mut self, t: Topic) {
        self.topic_hub_firmware_ok = t;
    }

    pub fn topic_handshake_to_edge(&self) -> &Topic {
        &self.topic_handshake_to_edge
    }
    pub fn set_topic_handshake_to_edge(&mut self, t: Topic) {
        self.topic_handshake_to_edge = t;
    }

    pub fn topic_ping(&self) -> &Topic {
        &self.topic_ping
    }
    pub fn set_topic_ping(&mut self, t: Topic) {
        self.topic_ping = t;
    }

    pub fn topic_empty_queue(&self) -> &Topic {
        &self.topic_empty_queue
    }
    pub fn set_topic_empty_queue(&mut self, t: Topic) {
        self.topic_empty_queue = t;
    }

    pub fn topic_linkage_request(&self) -> &Topic {
        &self.topic_linkage_request
    }
    pub fn set_topic_linkage_request(&mut self, t: Topic) {
        self.topic_linkage_request = t;
    }

    // --- GETTERS & SETTERS PARA LOS TOPICS (Escucha) ---
    pub fn topic_edge_state_normal(&self) -> &Topic {
        &self.topic_edge_state_normal
    }
    pub fn set_topic_edge_state_normal(&mut self, t: Topic) {
        self.topic_edge_state_normal = t;
    }

    pub fn topic_edge_state_balance(&self) -> &Topic {
        &self.topic_edge_state_balance
    }
    pub fn set_topic_edge_state_balance(&mut self, t: Topic) {
        self.topic_edge_state_balance = t;
    }

    pub fn topic_edge_state_safe(&self) -> &Topic {
        &self.topic_edge_state_safe
    }
    pub fn set_topic_edge_state_safe(&mut self, t: Topic) {
        self.topic_edge_state_safe = t;
    }

    pub fn topic_edge_phase(&self) -> &Topic {
        &self.topic_edge_phase
    }
    pub fn set_topic_edge_phase(&mut self, t: Topic) {
        self.topic_edge_phase = t;
    }

    pub fn topic_edge_handshake(&self) -> &Topic {
        &self.topic_edge_handshake
    }
    pub fn set_topic_edge_handshake(&mut self, t: Topic) {
        self.topic_edge_handshake = t;
    }

    pub fn topic_heartbeat(&self) -> &Topic {
        &self.topic_heartbeat
    }
    pub fn set_topic_heartbeat(&mut self, t: Topic) {
        self.topic_heartbeat = t;
    }

    pub fn topic_new_firmware(&self) -> &Topic {
        &self.topic_new_firmware
    }
    pub fn set_topic_new_firmware(&mut self, t: Topic) {
        self.topic_new_firmware = t;
    }

    pub fn topic_new_settings(&self) -> &Topic {
        &self.topic_new_settings
    }
    pub fn set_topic_new_settings(&mut self, t: Topic) {
        self.topic_new_settings = t;
    }

    pub fn topic_edge_setting_ok(&self) -> &Topic {
        &self.topic_edge_setting_ok
    }
    pub fn set_topic_edge_setting_ok(&mut self, t: Topic) {
        self.topic_edge_setting_ok = t;
    }

    pub fn topic_linkage_ack(&self) -> &Topic {
        &self.topic_linkage_ack
    }
    pub fn set_topic_linkage_ack(&mut self, t: Topic) {
        self.topic_linkage_ack = t;
    }

    pub fn topic_ping_ack(&self) -> &Topic {
        &self.topic_ping_ack
    }
    pub fn set_topic_ping_ack(&mut self, t: Topic) {
        self.topic_ping_ack = t;
    }

    /// Aplica una modificación aislada a un campo específico usando el enum ConfigField.
    /// Esto mueve la lógica de enrutamiento al dominio en lugar de ensuciar la lógica.
    pub fn apply_field(&mut self, field: ConfigField) {
        match field {
            ConfigField::BalanceEpoch(epoch) => self.set_balance_epoch(epoch),
            ConfigField::LinkageFlag(flag) => self.set_linkage_flag(flag),
            ConfigField::MessageId(id) => self.set_message_id(id),
        }
    }
}
