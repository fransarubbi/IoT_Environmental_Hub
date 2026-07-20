//! Lógica de gestión de configuración.
//!
//! Controla la lectura concurrente, actualización y persistencia (NVS) de `SystemSettings`.

use crate::app::system_settings::domain::{
    ConfigCommand, ConfigResponse, EnergyMode, SystemSettings,
};
use anyhow::{Result, anyhow};
use async_channel::{Receiver, Sender, bounded};
use core::convert::TryFrom;
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use esp_idf_hal::sys::{esp_pm_config_esp32_t, esp_pm_configure};
use esp_idf_svc::nvs::{EspDefaultNvsPartition, EspNvs, NvsDefault};
use log::{error, info};
use serde_json_core::{from_slice, to_vec};
use std::{
    error::Error,
    sync::{Arc, RwLock},
};

/// Gestor principal de la configuración.
/// Escucha órdenes de actualización e impacta los cambios en memoria RAM y memoria Flash (NVS).
pub struct ConfigManager {
    /// Referencia segura y sincronizada a la configuración.
    config: Arc<RwLock<SystemSettings>>,
    /// Receptor de comandos de modificación.
    receiver: Receiver<ConfigCommand>,
    /// Envía un comando indicando si habia datos o no en NVS.
    sender: Sender<ConfigResponse>,
    /// Manipulador de la partición de almacenamiento no volátil.
    nvs: EspNvs<NvsDefault>,
    /// Flag de datos en NVS
    flag: bool,
}

enum PeriodicCommand {
    Start { interval_secs: u64, event: Event },
    Stop,
}

#[derive(Clone)]
pub enum Event {
    Timeout,
}

impl ConfigManager {
    /// Construye el gestor, recupera la última configuración desde NVS y devuelve
    /// la instancia junto con el puntero `Arc` que debe repartirse al resto del sistema.
    pub fn new(
        sender: Sender<ConfigResponse>,
        receiver: Receiver<ConfigCommand>,
        nvs_partition: EspDefaultNvsPartition,
    ) -> Result<(Self, Arc<RwLock<SystemSettings>>)> {
        info!("creando ConfigManager...");
        let nvs = EspNvs::new(nvs_partition, "config", true)?;

        // Intentamos cargar; si falla, instanciamos el default.
        match Self::load_from_nvs(&nvs) {
            Ok((loaded_config, has_data)) => {
                let shared_config = Arc::new(RwLock::new(loaded_config));

                let manager = Self {
                    config: Arc::clone(&shared_config),
                    receiver,
                    sender,
                    nvs,
                    flag: has_data,
                };
                info!("ConfigManager creado correctamente.");

                Ok((manager, shared_config))
            }
            Err(e) => {
                // Retornamos el error si falla la inicialización
                Err(anyhow!("error al inicializar NVS: {}", e))
            }
        }
    }

    /// Ciclo de vida asíncrono del Manager. Recibe comandos y actúa en consecuencia.
    pub async fn run<'a>(mut self, executor: &'a LocalExecutor<'a>) {
        let (tx, rx_timer) = bounded(3);
        let (tx_timer, rx) = bounded(3);

        let id = self.config.read().unwrap().message_id();
        self.config.write().unwrap().set_message_id(id + 1);

        executor.spawn(timer(rx_timer, tx_timer)).detach();
        info!("iniciando ConfigManager...");
        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                Either::First(Ok(cmd)) => match cmd {
                    ConfigCommand::UpdateConfig(new_config) => {
                        let id = self.config.read().unwrap().message_id();
                        let dest = self.config.read().unwrap().mac_addr().to_string();
                        let destination =
                            heapless::String::<18>::try_from(dest.as_str()).unwrap_or_default();
                        if new_config.metadata.destination_id == destination {
                            if new_config.message_id > id {
                                self.config
                                    .write()
                                    .unwrap()
                                    .set_message_id(new_config.message_id);
                                self.config
                                    .write()
                                    .unwrap()
                                    .set_id_network(new_config.network);

                                let wifi_ssid = heapless::String::<20>::try_from(
                                    self.config.read().unwrap().wifi_ssid().to_string().as_str(),
                                )
                                .unwrap_or_default();

                                let wifi_pass = heapless::String::<30>::try_from(
                                    self.config
                                        .read()
                                        .unwrap()
                                        .wifi_password()
                                        .to_string()
                                        .as_str(),
                                )
                                .unwrap_or_default();

                                if new_config.wifi_ssid != wifi_ssid
                                    || new_config.wifi_password != wifi_pass
                                {
                                    self.config
                                        .write()
                                        .unwrap()
                                        .set_wifi_ssid(new_config.wifi_ssid);

                                    self.config
                                        .write()
                                        .unwrap()
                                        .set_wifi_password(new_config.wifi_password);

                                    let wifi_ssid = heapless::String::<20>::try_from(
                                        self.config
                                            .read()
                                            .unwrap()
                                            .wifi_ssid()
                                            .to_string()
                                            .as_str(),
                                    )
                                    .unwrap_or_default();

                                    let wifi_pass = heapless::String::<30>::try_from(
                                        self.config
                                            .read()
                                            .unwrap()
                                            .wifi_password()
                                            .to_string()
                                            .as_str(),
                                    )
                                    .unwrap_or_default();

                                    if let Err(e) =
                                        self.sender.try_send(ConfigResponse::UpdateWifi {
                                            ssid: wifi_ssid,
                                            password: wifi_pass,
                                        })
                                    {
                                        error!("{e}");
                                    }
                                }

                                self.config
                                    .write()
                                    .unwrap()
                                    .set_mqtt_uri(new_config.mqtt_uri);
                                self.config
                                    .write()
                                    .unwrap()
                                    .set_device_name(new_config.device_name);
                                self.config
                                    .write()
                                    .unwrap()
                                    .set_sample_rate(new_config.sample);
                                let energy = EnergyMode::from_u32(new_config.energy_mode);
                                if energy.is_some() {
                                    self.config
                                        .write()
                                        .unwrap()
                                        .set_energy_mode(energy.unwrap());
                                    set_cpu_frequency(energy.unwrap());
                                }
                                info!("configuración completamente actualizada.");
                                if let Err(e) = self.save_to_nvs() {
                                    error!("fallo al guardar la configuración en nvs. {e}");
                                }

                                if let Err(e) = self.sender.try_send(
                                    ConfigResponse::GenerateSettingsAck(new_config.message_id),
                                ) {
                                    error!("no se pudo enviar GenerateSettingsAck. {e}");
                                }
                                self.config.write().unwrap().update_topics();
                            } else {
                                if let Err(e) = self
                                    .sender
                                    .try_send(ConfigResponse::GenerateSettingsAck(id))
                                {
                                    error!("no se pudo enviar GenerateSettingsAck. {e}");
                                }
                            }
                        }
                    }
                    ConfigCommand::UpdateField(field) => {
                        {
                            let mut cfg = self.config.write().unwrap();
                            cfg.apply_field(field);
                        }
                        info!("campo de configuración actualizado.");
                        let _ = self.save_to_nvs();
                    }
                    ConfigCommand::SettingsAck(ack) => {
                        let id = self.config.read().unwrap().message_id();
                        if ack.message_id == id && ack.handshake {
                            if let Err(e) = tx.try_send(PeriodicCommand::Stop) {
                                error!("no se pudo enviar Stop. {e}");
                            }
                        }
                    }
                    ConfigCommand::StartSendingSettings => {
                        if let Err(e) = tx.try_send(PeriodicCommand::Start {
                            interval_secs: 10,
                            event: Event::Timeout,
                        }) {
                            error!("no se pudo enviar Start. {e}");
                        }
                    }
                },
                Either::First(Err(e)) => {
                    error!("{e}")
                }
                Either::Second(Ok(msg)) => match msg {
                    Event::Timeout => {
                        let id = self.config.read().unwrap().message_id();
                        if let Err(e) = self.sender.try_send(ConfigResponse::GenerateSettings(id)) {
                            error!("no se pudo enviar GenerateSettings. {e}");
                        }
                    }
                },
                Either::Second(Err(e)) => {
                    error!("{e}")
                }
            }
        }
    }

    pub fn has_data(&self) -> bool {
        self.flag
    }

    // --- Funciones internas de NVS ---

    /// Carga la configuración como String JSON desde la Flash NVS.
    fn load_from_nvs(nvs: &EspNvs<NvsDefault>) -> Result<(SystemSettings, bool), Box<dyn Error>> {
        let mut buf = [0u8; 4096];

        if let Some(data) = nvs.get_str("config", &mut buf)? {
            // ⭐ Deserializar directamente desde el buffer
            // IMPORTANTE: data es &str, necesitamos convertirlo a &[u8]
            let bytes = data.as_bytes();

            // Deserializar usando serde-json-core
            match from_slice::<SystemSettings>(bytes) {
                Ok((config, _bytes_used)) => Ok((config, true)),
                Err(e) => {
                    error!("Error al deserializar config desde NVS: {}", e);
                    // Si falla, usar default
                    Ok((SystemSettings::default(), false))
                }
            }
        } else {
            // Devuelve configuración por defecto y false
            Ok((SystemSettings::default(), false))
        }
    }

    /// Bloquea temporalmente para leer RAM y persiste la estructura en la memoria Flash NVS.
    pub fn save_to_nvs(&mut self) -> Result<()> {
        let cfg = self.config.read().unwrap();

        // Serializar usando serde-json-core
        match to_vec::<_, 4096>(&*cfg) {
            Ok(buffer) => {
                // Convertir a &str para NVS
                let json_str = core::str::from_utf8(&buffer)
                    .map_err(|e| anyhow!("Error al convertir JSON a UTF-8: {}", e))?;

                self.nvs.set_str("config", json_str)?;
                info!("configuración guardada en NVS ({} bytes)", buffer.len());
                Ok(())
            }
            Err(e) => {
                error!("Error al serializar config: {}", e);
                Err(anyhow!("Error de serialización: {}", e))
            }
        }
    }
}

async fn timer(rx_cmd: Receiver<PeriodicCommand>, tx_external: Sender<Event>) {
    loop {
        // --- ESTADO IDLE ---
        let (mut interval, mut current_event) = match rx_cmd.recv().await {
            Ok(PeriodicCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(PeriodicCommand::Stop) => continue,
            Err(_) => break,
        };

        // --- ESTADO RUNNING ---
        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_cmd.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx_external.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer periódico detenido.");
                        break; // Volver a IDLE
                    }
                    PeriodicCommand::Start {
                        interval_secs,
                        event,
                    } => {
                        info!("timer periódico actualizado sin detener el hilo.");
                        interval = interval_secs;
                        current_event = event;
                    }
                },
                Either::Second(Err(_)) => return,
            }
        }
    }
}

pub fn set_cpu_frequency(mode: EnergyMode) {
    let freq_mhz = match mode {
        EnergyMode::LOW => 80,
        EnergyMode::NORMAL => 160,
        EnergyMode::PERFORMANCE => 240,
    };

    unsafe {
        // En ESP-IDF, la estructura específica para el ESP32 básico es esp_pm_config_esp32_t
        let mut config: esp_pm_config_esp32_t = core::mem::zeroed();
        config.max_freq_mhz = freq_mhz;
        config.min_freq_mhz = freq_mhz;
        config.light_sleep_enable = false; // Poner a true si quieres que ahorre más batería al estar inactivo

        // Llamada FFI a la API de ESP-IDF
        let res = esp_pm_configure(&config as *const _ as *const core::ffi::c_void);

        if res == 0 {
            log::info!("Frecuencia de CPU ajustada a {} MHz", freq_mhz);
        } else {
            log::error!(
                "Fallo al cambiar frecuencia. Verifica que CONFIG_PM_ENABLE=y esté en sdkconfig.defaults"
            );
        }
    }
}
