//! Módulo WiFi.
//! Abstrae la conexión a la red detrás de un trait.

use crate::app::message::domain::WIFI_SSID_STRING_LEN;
use async_channel::Receiver;
use embassy_futures::select::{select, Either};
use embassy_time::{Duration, Timer};
use esp_idf_hal::modem::Modem;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sys::esp_wifi_set_max_tx_power;
use esp_idf_svc::wifi::{AuthMethod, BlockingWifi, ClientConfiguration, Configuration, EspWifi};
use heapless::String;
use log::{error, info, warn};

pub enum WifiCommand {
    Update {
        ssid: String<WIFI_SSID_STRING_LEN>,
        password: String<30>,
    },
    Stop,
}

pub fn get_unix_epoch() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs()
}

const HEALTHCHECK_INTERVAL: Duration = Duration::from_secs(10);

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

        unsafe {
            // 80 representa 20 dBm (el valor interno se multiplica por 4, o sea 20 * 4 = 80)
            esp_wifi_set_max_tx_power(80);
        }

        info!("WiFi inicializado y configurado correctamente.");

        Ok(Self { wifi, receiver })
    }

    pub async fn run(mut self) {
        self.connect().await;

        loop {
            // Sondeamos el estado de la red cada HEALTHCHECK_INTERVAL, en
            // paralelo con la espera de comandos por el canal.
            let timeout_fut = Timer::after(HEALTHCHECK_INTERVAL);
            let cmd_fut = self.receiver.recv();

            match select(timeout_fut, cmd_fut).await {
                // El timeout expiró: verificamos si seguimos conectados de verdad.
                Either::First(_) => match self.wifi.is_connected() {
                    Ok(true) => {
                        // Todo OK, seguimos esperando normalmente.
                    }
                    Ok(false) | Err(_) => {
                        warn!("WiFi desconectado sin comando. Reintentando...");
                        self.connect().await;
                    }
                },
                // Llegó un comando por el canal
                Either::Second(Ok(cmd)) => match cmd {
                    WifiCommand::Update { ssid, password } => {
                        info!("recibidas nuevas credenciales. Reconectando...");

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
                            error!("no se pudo setear nueva config WiFi: {e}");
                            continue;
                        }

                        self.connect().await;
                    }
                    WifiCommand::Stop => {
                        info!("desconectando WiFi...");
                        self.disconnect();
                        break;
                    }
                },
                // El canal se cerró (todos los senders fueron drop-eados)
                Either::Second(Err(_)) => {
                    warn!("canal de comandos WiFi cerrado. Finalizando tarea WiFi.");
                    break;
                }
            }
        }
    }

    async fn connect(&mut self) {
        let mut attempt = 1;
        loop {
            info!("intentando conectar al AP... (Intento {})", attempt);
            match self.wifi.connect() {
                Ok(_) => match self.wifi.wait_netif_up() {
                    Ok(_) => {
                        info!("conexión WiFi exitosa. IP obtenida.");
                        embassy_time::Timer::after(embassy_time::Duration::from_secs(5)).await;
                        return;
                    }
                    Err(e) => warn!("fallo obteniendo IP tras conectar: {}", e),
                },
                Err(e) => warn!("fallo al conectar al AP: {}", e),
            }
            attempt += 1;
            // Esperamos 5 segundos antes del próximo intento
            embassy_time::Timer::after(embassy_time::Duration::from_secs(5)).await;
        }
    }

    fn disconnect(&mut self) {
        match self.wifi.disconnect() {
            Ok(_) => info!("WiFi desconectado correctamente"),
            Err(e) => error!("error al desconectar WiFi: {e}"),
        }
    }
}