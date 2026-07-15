//! Módulo WiFi.
//! Abstrae la conexión a la red detrás de un trait.

use crate::app::message::domain::WIFI_SSID_STRING_LEN;
use async_channel::Receiver;
use esp_idf_hal::modem::Modem;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sys::esp_wifi_set_max_tx_power;
use esp_idf_svc::wifi::{AuthMethod, BlockingWifi, ClientConfiguration, Configuration, EspWifi};
use heapless::String;
use log::{error, info, warn};

const WIFI_MAX_RETRY: u8 = 5;

pub enum WifiCommand {
    Update {
        ssid: String<WIFI_SSID_STRING_LEN>,
        password: String<30>,
    },
}

pub fn get_unix_epoch() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs()
}

pub struct EspIdfWifiManager<'a> {
    // Usamos BlockingWifi que envuelve al cliente WiFi de esp-idf-svc y maneja
    // la máquina de eventos y esperas
    wifi: BlockingWifi<EspWifi<'a>>,
    receiver: Receiver<WifiCommand>,
}

impl<'a> EspIdfWifiManager<'a> {
    /// Constructor.
    /// Recibe el periférico del Módem (hardware) y las particiones/event_loops del sistema.
    pub fn new(
        modem: Modem<'a>,
        sys_loop: EspSystemEventLoop,
        nvs: EspDefaultNvsPartition,
        ssid: &str,
        password: &str,
        receiver: Receiver<WifiCommand>,
    ) -> Result<Self, String<100>> {
        // Inicializar el driver WiFi
        let esp_wifi = EspWifi::new(modem, sys_loop.clone(), Some(nvs)).map_err(|e| {
            let mut s = String::<100>::new();
            let _ = core::fmt::write(&mut s, format_args!("error creando EspWifi: {}", e));
            s
        })?;

        // Envolverlo en BlockingWifi
        let mut wifi = BlockingWifi::wrap(esp_wifi, sys_loop).map_err(|e| {
            let mut s = String::<100>::new();
            let _ = core::fmt::write(&mut s, format_args!("error creando BlockingWifi: {}", e));
            s
        })?;

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
        wifi.set_configuration(&wifi_configuration).map_err(|e| {
            let mut s = String::<100>::new();
            let _ = core::fmt::write(&mut s, format_args!("error configurando WiFi: {}", e));
            s
        })?;

        // Iniciamos el driver
        wifi.start().map_err(|e| {
            let mut s = String::<100>::new();
            let _ = core::fmt::write(&mut s, format_args!("error iniciando WiFi: {}", e));
            s
        })?;

        // Equivale a: esp_wifi_set_max_tx_power(20)
        unsafe {
            // 80 representa 20 dBm (el valor interno se multiplica por 4, o sea 20 * 4 = 80)
            esp_wifi_set_max_tx_power(80);
        }

        info!("WiFi inicializado y configurado correctamente.");

        Ok(Self { wifi, receiver })
    }

    pub async fn run(mut self) {
        if let Err(e) = self.connect().await {
            error!("Fallo en la conexión inicial: {}", e);
        }

        loop {
            // Esperamos comandos (ej. si cambian la config de red)
            if let Ok(cmd) = self.receiver.recv().await {
                match cmd {
                    WifiCommand::Update { ssid, password } => {
                        info!("Recibidas nuevas credenciales. Reconectando...");

                        // Desconectamos la red actual
                        let _ = self.disconnect();

                        // Creamos la nueva configuración
                        let wifi_configuration = Configuration::Client(ClientConfiguration {
                            ssid: ssid.as_str().try_into().unwrap(),
                            password: password.as_str().try_into().unwrap(),
                            auth_method: if password.is_empty() {
                                AuthMethod::None
                            } else {
                                AuthMethod::WPA2Personal
                            },
                            ..Default::default()
                        });

                        // Aplicamos y volvemos a conectar
                        if let Err(e) = self.wifi.set_configuration(&wifi_configuration) {
                            error!("No se pudo setear nueva config WiFi: {e}");
                            continue;
                        }

                        if let Err(e) = self.connect().await {
                            error!("Fallo al reconectar a nueva red: {e}");
                        }
                    }
                }
            }
        }
    }

    async fn connect(&mut self) -> Result<(), String<50>> {
        let mut retry_count = 0;

        loop {
            info!(
                "intentando conectar al AP... (Intento {}/{})",
                retry_count + 1,
                WIFI_MAX_RETRY
            );

            match self.wifi.connect() {
                Ok(_) => match self.wifi.wait_netif_up() {
                    Ok(_) => {
                        info!("conexión WiFi exitosa. IP obtenida.");
                        return Ok(());
                    }
                    Err(e) => warn!("fallo obteniendo IP tras conectar: {}", e),
                },
                Err(e) => warn!("fallo al conectar al AP: {}", e),
            }

            retry_count += 1;
            if retry_count >= WIFI_MAX_RETRY {
                return Err(
                    String::<50>::try_from("timeout/Fallo de conexión WiFi").unwrap_or_default()
                );
            }

            // ESPERA ASÍNCRONA: Permite que MQTT siga trabajando mientras reintentamos
            embassy_time::Timer::after(embassy_time::Duration::from_secs(5)).await;
        }
    }

    fn disconnect(&mut self) -> Result<(), String<50>> {
        self.wifi.disconnect().map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("error al desconectar: {}", e));
            s
        })
    }
}
