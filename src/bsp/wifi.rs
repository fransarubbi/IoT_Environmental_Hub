//! Módulo WiFi.
//! Abstrae la conexión a la red detrás de un trait.

use esp_idf_hal::modem::Modem;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::wifi::{AuthMethod, BlockingWifi, ClientConfiguration, Configuration, EspWifi};
use log::{error, info, warn};
use std::time::Duration;

// FFI para acceder a funciones de bajo nivel de C si es estrictamente necesario
use esp_idf_sys::{esp_wifi_set_max_tx_power, esp_wifi_sta_get_ap_info, wifi_ap_record_t};

const WIFI_MAX_RETRY: u8 = 5;

pub struct EspIdfWifiManager<'a> {
    // Usamos BlockingWifi que envuelve al cliente WiFi de esp-idf-svc y maneja
    // la máquina de eventos y esperas
    wifi: BlockingWifi<EspWifi<'a>>,
}

impl<'a> EspIdfWifiManager<'a> {
    /// Constructor.
    /// Recibe el periférico del Módem (hardware) y las particiones/event_loops del sistema.
    pub fn new(
        modem: Modem,
        sys_loop: EspSystemEventLoop,
        nvs: EspDefaultNvsPartition,
        ssid: &str,
        password: &str,
    ) -> Result<Self, String> {
        // Inicializar el driver WiFi
        let esp_wifi = EspWifi::new(modem, sys_loop.clone(), Some(nvs))
            .map_err(|e| format!("error creando EspWifi: {}", e))?;

        // Envolverlo en BlockingWifi
        let mut wifi = BlockingWifi::wrap(esp_wifi, sys_loop)
            .map_err(|e| format!("error creando BlockingWifi: {}", e))?;

        // Configurar credenciales (Modo Station)
        let wifi_configuration = Configuration::Client(ClientConfiguration {
            ssid: ssid.try_into().unwrap(), // Convierte el &str al buffer interno
            password: password.try_into().unwrap(),
            auth_method: if password.is_empty() {
                AuthMethod::None
            } else {
                AuthMethod::WPA2Personal
            },
            ..Default::default()
        });

        // Aplicamos la configuración
        wifi.set_configuration(&wifi_configuration)
            .map_err(|e| format!("error configurando WiFi: {}", e))?;

        // Iniciamos el driver
        wifi.start()
            .map_err(|e| format!("error iniciando WiFi: {}", e))?;

        // Equivale a: esp_wifi_set_max_tx_power(20)
        unsafe {
            // 80 representa 20 dBm (el valor interno se multiplica por 4, o sea 20 * 4 = 80)
            esp_wifi_set_max_tx_power(80);
        }

        info!("WiFi inicializado y configurado correctamente.");

        Ok(Self { wifi })
    }
}

/// Implementamos el trait para nuestra estructura específica de ESP-IDF
impl<'a> WifiService for EspIdfWifiManager<'a> {
    fn connect(&mut self) -> Result<(), String> {
        let mut retry_count = 0;

        // Reemplaza la lógica de s_retry_num y el wifi_event_handler en C
        loop {
            info!(
                "intentando conectar al AP... (Intento {}/{})",
                retry_count + 1,
                WIFI_MAX_RETRY
            );

            // Intenta conectar. Si falla, manejamos el error
            match self.wifi.connect() {
                Ok(_) => {
                    // Si se conecta, ahora debemos esperar a que el DHCP nos asigne una IP
                    match self.wifi.wait_netif_up() {
                        Ok(_) => {
                            info!("conexión WiFi exitosa. IP obtenida.");
                            return Ok(());
                        }
                        Err(e) => warn!("fallo obteniendo IP tras conectar: {}", e),
                    }
                }
                Err(e) => {
                    warn!("fallo al conectar al AP: {}", e);
                }
            }

            retry_count += 1;
            if retry_count >= WIFI_MAX_RETRY {
                error!(
                    "fallo definitivo al conectar WiFi tras {} intentos",
                    WIFI_MAX_RETRY
                );
                return Err("timeout/Fallo de conexión WiFi".to_string());
            }

            // Esperar un poco antes de reintentar
            std::thread::sleep(Duration::from_secs(5));
        }
    }

    fn disconnect(&mut self) -> Result<(), String> {
        self.wifi
            .disconnect()
            .map_err(|e| format!("error al desconectar: {}", e))
    }

    fn get_stats(&self) -> WifiStats {
        let mut stats = WifiStats {
            rssi: -127,
            ssid: String::new(),
            ip: String::new(),
        };

        // Obtenemos la IP de forma segura a través de esp-idf-svc
        if let Ok(ip_info) = self.wifi.wifi().sta_netif().get_ip_info() {
            stats.ip = ip_info.ip.to_string();
        }

        // Para el RSSI y SSID actuales, accedemos al driver subyacente en C
        let mut ap_info: wifi_ap_record_t = Default::default();
        let err = unsafe { esp_wifi_sta_get_ap_info(&mut ap_info) };

        if err == esp_idf_sys::ESP_OK {
            stats.rssi = ap_info.rssi;
            // Convertimos el array de u8 de C a un String de Rust, ignorando nulos
            stats.ssid = String::from_utf8_lossy(&ap_info.ssid)
                .trim_end_matches(char::from(0))
                .to_string();
        }

        stats
    }
}
