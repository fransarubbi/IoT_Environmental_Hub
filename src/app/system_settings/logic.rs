use log::{error, info, warn};

pub struct ConfigManager {
    /// Datos compartidos con RwLock
    config: Arc<RwLock<SystemSettings>>,
    /// Canal para comandos de escritura
    command_sender: Sender<ConfigCommand>,
    /// NVS para persistencia
    nvs: EspNvs<NvsDefault>,
}

impl ConfigManager {
    /// Inicializa el gestor de configuración.
    pub fn new() -> Result<Self> {
        // Inicializar NVS
        let nvs = EspNvs::new("config", true)?;

        // Cargar configuración inicial
        let initial_config = Self::load_from_nvs(&nvs).unwrap_or_default();

        // Crear RwLock
        let config = Arc::new(RwLock::new(initial_config));

        // Crear canal para comandos
        let (tx, rx) = mpsc::channel::<ConfigCommand>();

        let manager = Self {
            config: Arc::clone(&config),
            command_sender: tx,
            nvs,
        };

        // Iniciar el worker que procesa comandos de escritura
        manager.start_worker(rx, config);

        Ok(manager)
    }

    /// Inicia el worker que procesa los comandos de escritura
    fn start_worker(&self, rx: Receiver<ConfigCommand>, config: Arc<RwLock<SystemSettings>>) {
        // Clonamos NVS para el worker
        let mut nvs = match self.nvs.clone() {
            Ok(n) => n,
            Err(e) => {
                error!("error clonando NVS: {}", e);
                return;
            }
        };

        thread::spawn(move || {
            info!("worker de configuración iniciado");

            for command in rx {
                match command {
                    ConfigCommand::UpdateConfig(new_config) => {
                        // Escritura con RwLock (bloquea todos los lectores momentáneamente)
                        let mut cfg = config.write().unwrap();
                        *cfg = new_config;
                        info!("configuración actualizada");

                        // Guardar en NVS
                        if let Err(e) = Self::save_to_nvs(&mut nvs, &cfg) {
                            error!("error guardando en NVS: {}", e);
                        }
                    }

                    ConfigCommand::UpdateField(field) => {
                        // Escritura con RwLock
                        let mut cfg = config.write().unwrap();
                        Self::apply_field(&mut cfg, field);
                        info!("campo de configuración actualizado");

                        if let Err(e) = Self::save_to_nvs(&mut nvs, &cfg) {
                            error!("error guardando en NVS: {}", e);
                        }
                    }

                    ConfigCommand::Reload => {
                        // Cargar desde NVS
                        if let Ok(loaded) = Self::load_from_nvs(&nvs) {
                            let mut cfg = config.write().unwrap();
                            *cfg = loaded;
                            info!("configuración recargada desde NVS");
                        }
                    }

                    ConfigCommand::Reset => {
                        let default = SystemConfig::default();
                        let mut cfg = config.write().unwrap();
                        *cfg = default.clone();

                        // Guardar defaults en NVS
                        if let Err(e) = Self::save_to_nvs(&mut nvs, &default) {
                            error!("error guardando defaults en NVS: {}", e);
                        }
                        info!("configuración reseteada a defaults");
                    }

                    ConfigCommand::Save => {
                        let cfg = config.read().unwrap();
                        if let Err(e) = Self::save_to_nvs(&mut nvs, &cfg) {
                            error!("error guardando en NVS: {}", e);
                        } else {
                            info!("configuración guardada en NVS");
                        }
                    }
                }
            }

            warn!("worker de configuración terminado");
        });
    }

    // ========== LECTURA (RÁPIDA, DIRECTA) ==========

    /// Obtiene una referencia de solo lectura (¡MUY RÁPIDO!)
    pub fn get_config(&self) -> RwLockReadGuard<'_, SystemSettings> {
        self.config.read().unwrap() // Bloqueo mínimo para lectura
    }

    /// Obtiene un valor específico de forma rápida
    pub fn get_wifi_ssid(&self) -> String {
        let cfg = self.config.read().unwrap();
        cfg.wifi_ssid.clone()
    }

    /// Obtiene el periodo de sampleo
    pub fn get_sample_period(&self) -> u32 {
        let cfg = self.config.read().unwrap();
        cfg.sample_period_ms
    }

    /// Obtiene el modo de energía
    pub fn get_power_mode(&self) -> PowerMode {
        let cfg = self.config.read().unwrap();
        cfg.power_mode.clone()
    }

    // ========== ESCRITURA (A TRAVÉS DEL CANAL) ==========

    /// Envía un comando para actualizar toda la configuración
    pub fn update_config(&self, new_config: SystemSettings) -> Result<()> {
        self.command_sender
            .send(ConfigCommand::UpdateConfig(new_config))
            .map_err(|e| anyhow!("Error enviando comando: {}", e))
    }

    /// Envía un comando para actualizar un campo específico
    pub fn update_field(&self, field: ConfigField) -> Result<()> {
        self.command_sender
            .send(ConfigCommand::UpdateField(field))
            .map_err(|e| anyhow!("Error enviando comando: {}", e))
    }

    /// Recarga configuración desde NVS
    pub fn reload(&self) -> Result<()> {
        self.command_sender
            .send(ConfigCommand::Reload)
            .map_err(|e| anyhow!("Error enviando comando: {}", e))
    }

    /// Resetea a valores por defecto
    pub fn reset(&self) -> Result<()> {
        self.command_sender
            .send(ConfigCommand::Reset)
            .map_err(|e| anyhow!("Error enviando comando: {}", e))
    }

    /// Guarda la configuración actual en NVS
    pub fn save(&self) -> Result<()> {
        self.command_sender
            .send(ConfigCommand::Save)
            .map_err(|e| anyhow!("Error enviando comando: {}", e))
    }

    // ========== PERSISTENCIA EN NVS ==========

    fn load_from_nvs(nvs: &EspNvs<NvsDefault>) -> Result<SystemSettings> {
        // Leer como JSON desde NVS
        if let Some(data) = nvs.get_str("config")? {
            let config: SystemConfig = serde_json::from_str(&data)?;
            Ok(config)
        } else {
            Err(anyhow!("No hay configuración en NVS"))
        }
    }

    fn save_to_nvs(nvs: &mut EspNvs<NvsDefault>, config: &SystemSettings) -> Result<()> {
        let json = serde_json::to_string(config)?;
        nvs.set_str("config", &json)?;
        nvs.commit()?;
        Ok(())
    }

    fn apply_field(config: &mut SystemConfig, field: ConfigField) {
        match field {
            ConfigField::WifiSsid(ssid) => config.wifi_ssid = ssid,
            ConfigField::WifiPassword(pass) => config.wifi_password = pass,
            ConfigField::MqttBroker(broker) => config.mqtt_broker = broker,
            ConfigField::MqttPort(port) => config.mqtt_port = port,
            ConfigField::DeviceName(name) => config.device_name = name,
            ConfigField::SamplePeriodMs(period) => config.sample_period_ms = period,
            ConfigField::PowerMode(mode) => config.power_mode = mode,
        }
    }
}

// Implementación de Clone para compartir entre tareas
impl Clone for ConfigManager {
    fn clone(&self) -> Self {
        Self {
            config: Arc::clone(&self.config),
            command_sender: self.command_sender.clone(),
            nvs: self.nvs.clone().unwrap(),
        }
    }
}
