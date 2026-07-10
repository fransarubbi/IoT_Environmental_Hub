/// Contrato que cualquier implementador de WiFi debe cumplir
#[allow(async_fn_in_trait)]
pub trait Wifi {
    /// Inicia la conexión y espera hasta obtener una IP
    async fn connect(&mut self);
    /// Se desconecta de la red actual
    fn disconnect(&mut self) -> Result<(), String>;
    /// Retorna estadísticas en tiempo real (RSSI, SSID actual, IP)
    fn get_stats(&self) -> WifiStats;
}

#[derive(Debug, Default)]
pub struct WifiStats {
    pub rssi: i8,
    pub ssid: String,
    pub ip: String,
}
