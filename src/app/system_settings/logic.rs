//! Lógica de gestión de configuración.
//!
//! Controla la lectura concurrente, actualización y persistencia (NVS) de `SystemSettings`.

use log::{info, error};
use std::{error::Error, sync::{Arc, RwLock}}; 
use async_channel::{Receiver, Sender};
use esp_idf_svc::nvs::{EspNvs, NvsDefault, EspDefaultNvsPartition};
use anyhow::{anyhow, Result};
use crate::app::system_settings::domain::{ConfigCommand, ConfigResponse, SystemSettings};


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

impl ConfigManager {
    
    /// Construye el gestor, recupera la última configuración desde NVS y devuelve
    /// la instancia junto con el puntero `Arc` que debe repartirse al resto del sistema.
    pub fn new(
        sender: Sender<ConfigResponse>,
        receiver: Receiver<ConfigCommand>,
        nvs_partition: EspDefaultNvsPartition
    ) -> Result<(Self, Arc<RwLock<SystemSettings>>)> {
        
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
                    flag: has_data
                };

                Ok((manager, shared_config))
            }
            Err(e) => {
                // Retornamos el error si falla la inicialización
                Err(anyhow!("error al inicializar NVS: {}", e))
            }
        }
    }

    /// Ciclo de vida asíncrono del Manager. Recibe comandos y actúa en consecuencia.
    pub async fn run(mut self) {
        
        while let Ok(cmd) = self.receiver.recv().await { 
            match cmd {
                ConfigCommand::UpdateConfig(new_config) => {
                    {
                        // Bloqueo explícito y corto para escribir
                        let mut cfg = self.config.write().unwrap();
                        *cfg = new_config;
                    } 
                    info!("configuración completamente actualizada.");
                    let _ = self.save_to_nvs();
                }
                
                ConfigCommand::UpdateField(field) => {
                    {
                        let mut cfg = self.config.write().unwrap();
                        cfg.apply_field(field);
                    }
                    info!("campo de configuración actualizado.");
                    let _ = self.save_to_nvs();
                }
                
                ConfigCommand::Reload => {
                    if let Ok(loaded) = Self::load_from_nvs(&self.nvs) {
                        let mut cfg = self.config.write().unwrap();
                        *cfg = loaded.0;
                        info!("configuración recargada exitosamente desde NVS.");
                    }
                }
                
                ConfigCommand::Save => {
                    let _ = self.save_to_nvs();
                }

                ConfigCommand::ThereIsSettingsInNVS => {
                    if self.flag {
                        if let Err(e) = self.sender.try_send(ConfigResponse::ExistsInNVS) {
                            error!("no se pudo enviar Settings::ExistsInNVS. {e}");
                        }
                    } else {
                        if let Err(e) = self.sender.try_send(ConfigResponse::NotExistsInNVS) {
                            error!("no se pudo enviar Settings::NotExistsInNVS. {e}");
                        }
                    }
                }
            }
        }
    }


    // --- Funciones internas de NVS ---

    /// Carga la configuración como String JSON desde la Flash NVS.
    fn load_from_nvs(nvs: &EspNvs<NvsDefault>) -> Result<(SystemSettings, bool), Box<dyn Error>> {

        // Buffer pre-asignado de 4096 bytes (4KB).
        let mut buf = [0u8; 4096];

        if let Some(data) = nvs.get_str("config", &mut buf)? {
            let config: SystemSettings = serde_json::from_str(&data)?;
            Ok((config, true))  // Encontrado en NVS
        } else {
            // Devuelve configuración por defecto y false
            Ok((SystemSettings::default(), false))  
        }
    }

    /// Bloquea temporalmente para leer RAM y persiste la estructura en la memoria Flash NVS.
    fn save_to_nvs(&mut self) -> Result<()> {
        let json = {
            let cfg = self.config.read().unwrap();
            serde_json::to_string(&*cfg)?
        };
        self.nvs.set_str("config", &json)?;
        info!("configuración guardada en NVS.");
        Ok(())
    }
}