use crate::app::message::domain::WIFI_SSID_STRING_LEN;
use heapless::String;

/// Contrato que cualquier implementador de WiFi debe cumplir
#[allow(async_fn_in_trait)]
pub trait Wifi {
    /// Inicia la conexión y espera hasta obtener una IP
    async fn connect(&mut self);
    /// Se desconecta de la red actual
    fn disconnect(&mut self) -> Result<(), String<20>>;
    /// Retorna estadísticas en tiempo real (RSSI, SSID actual, IP)
    fn get_stats(&self) -> WifiStats;
}

#[derive(Debug, Default)]
pub struct WifiStats {
    pub rssi: i8,
    pub ssid: String<WIFI_SSID_STRING_LEN>,
    pub ip: String<20>,
}
