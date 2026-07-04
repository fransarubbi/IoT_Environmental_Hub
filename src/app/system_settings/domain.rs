//! Definición del Contexto de Aplicación (Shared State).
//!
//! Este módulo implementa el patrón de **Estado Compartido** para aplicaciones asíncronas.
//! El `SystemSettings` actúa como un contenedor de "Inyección de Dependencias" manual,
//! agrupando los recursos que deben ser accesibles por múltiples tareas concurrentes

use esp_idf_svc::nvs::{EspNvs, NvsDefault};
use std::sync::mpsc::{self, Receiver, Sender};
use std::sync::{Arc, RwLock, RwLockReadGuard, RwLockWriteGuard};

/// Comandos para el gestor de configuración.
pub enum ConfigCommand {
    /// Actualizar toda la configuración
    UpdateConfig(SystemSettings),
    /// Actualizar solo un campo (para evitar sobrescribir todo)
    UpdateField(ConfigField),
    /// Recargar desde NVS
    Reload,
    /// Resetear a valores por defecto
    Reset,
    /// Guardar en NVS (persistir)
    Save,
}

/// Campos que se pueden actualizar individualmente
pub enum ConfigField {
    BalanceEpoch(u32),
    LinkageFlag(bool),
    MessageId(u32),
}

#[derive(Clone, Debug, Default)]
pub struct SystemSettings {
    id: IdentificationData,
    sample_rate: u32, // Segundos
    heartbeat: Heartbeat,
    network: Network,
    wifi: Wifi,
    mqtt_uri: String,
    energy_mode: EnergyMode,
    filters: Filters,
    air_r0: f32,
}

#[derive(Clone, Debug, Default)]
struct IdentificationData {
    pub mac_addr: String,
    pub device_name: String,
}

#[derive(Clone, Debug, Default)]
struct Heartbeat {
    pub time_between_heartbeats_balance_mode: u32,
    pub time_between_heartbeats_normal_mode: u32,
    pub time_between_heartbeats_safe_mode: u32,
}

#[derive(Clone, Debug, Default)]
struct Network {
    pub id_network: String,
    pub id_edge: String,
    pub balance_epoch: u32,
    pub url_bypass: String,
    pub linkage_flag: bool,
    pub message_id: u32,
}

#[derive(Clone, Debug, Default)]
struct Wifi {
    pub ssid: u8,
    pub ssid_len: u8,
    pub password: String,
    pub password_len: u8,
    pub ipv4: String,
}

#[derive(Clone, Debug, Default)]
struct Filters {
    pub air_alpha_ema: f32,
    pub temp_alpha_ema: f32,
}

#[derive(Clone, Debug, Default)]
pub enum EnergyMode {
    LOW,
    NORMAL,
    PERFORMANCE,
}

impl SystemSettings {
    pub fn mac_addr(&self) -> &str {
        self.id.mac_addr.as_str()
    }
    pub fn device_name(&self) -> &str {
        self.id.device_name.as_str()
    }
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }
    pub fn heartbeat_balance_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_balance_mode
    }
    pub fn heartbeat_normal_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_normal_mode
    }
    pub fn heartbeat_safe_mode(&self) -> u32 {
        self.heartbeat.time_between_heartbeats_safe_mode
    }
    pub fn id_network(&self) -> &str {
        self.network.id_network.as_str()
    }
    pub fn id_edge(&self) -> &str {
        self.network.id_edge.as_str()
    }
    pub fn balance_epoch(&self) -> u32 {
        self.network.balance_epoch
    }
    pub fn url_bypass(&self) -> &str {
        self.network.url_bypass.as_str()
    }
    pub fn linkage_flag(&self) -> bool {
        self.network.linkage_flag
    }
    pub fn message_id(&self) -> u32 {
        self.network.message_id
    }
    pub fn wifi_ssid(&self) -> u8 {
        self.wifi.ssid
    }
    pub fn wifi_ssid_len(&self) -> u8 {
        self.wifi.ssid_len
    }
    pub fn wifi_password(&self) -> &str {
        self.wifi.password.as_str()
    }
    pub fn password_len(&self) -> u8 {
        self.wifi.password_len
    }
    pub fn ipv4(&self) -> &str {
        self.wifi.ipv4.as_str()
    }
    pub fn mqtt_uri(&self) -> &str {
        self.mqtt_uri.as_str()
    }
    pub fn energy_mode(&self) -> EnergyMode {
        self.energy_mode
    }
    pub fn air_alpha_ema(&self) -> f32 {
        self.filters.air_alpha_ema
    }
    pub fn temp_alpha_ema(&self) -> f32 {
        self.filters.temp_alpha_ema
    }
    pub fn air_r0(&self) -> f32 {
        self.air_r0
    }
}
