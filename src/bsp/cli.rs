//! Módulo UART CLI (Command Line Interface)
//! Encargado de la configuración inicial del dispositivo por puerto serie.

use crate::app::system_settings::SystemSettings;
use crate::hal::UartIo;
use esp_idf_hal::delay::TickType;
use esp_idf_hal::gpio::{AnyIOPin, InputPin, OutputPin};
use esp_idf_hal::peripheral::Peripheral;
use esp_idf_hal::uart::{UartConfig, UartDriver};
use log::{error, info, warn};
use std::time::{Duration, Instant};

// Tamaño máximo del buffer de lectura
const MAX_BUFFER_SIZE: usize = 256;

/// Tamaño máximo del buffer de línea, igual a `SETTINGS_BUFFER_SIZE`.
const BUFFER_SIZE: usize = 256;

/// Tiempo de espera para confirmar cambio de configuración existente (`setting_mode_change`).
const CHANGE_TIMEOUT: Duration = Duration::from_secs(20);

/// Prefijo obligatorio de la URI de MQTT (`MQTTS_PREFIX`).
const MQTTS_PREFIX: &str = "mqtts://";

mod color {
    pub const B_WHT: &str = "\x1b[1;37m"; // Bordes
    pub const T_RST: &str = "\x1b[0m"; // Reset
    pub const C_MAG: &str = "\x1b[1;35m"; // Título principal
    pub const C_CYN: &str = "\x1b[1;36m"; // Sección configuración
    pub const C_YEL: &str = "\x1b[1;33m"; // Sección sensores
    pub const C_GRN: &str = "\x1b[1;32m"; // Sección interfaz
}

mod cmd {
    pub const WIFI_SSID: &str = "WIFI-SSID";
    pub const WIFI_PASS: &str = "WIFI-PASS";
    pub const MQTT_URI: &str = "MQTT-URI";
    pub const NETWORK: &str = "NETWORK";
    pub const URL_HTTPS: &str = "URL-BYPASS";
    pub const EDGE: &str = "EDGE-ID";
    pub const DEVICE_NAME: &str = "NAME-DEVICE";
    pub const SAMPLE: &str = "SAMPLE-TIME";
    pub const ENERGY_MODE: &str = "ENERGY-MODE";
    pub const DELETE_LINKAGE: &str = "DELETE-LINKAGE";
    pub const HEARTBEAT_BALANCE: &str = "HEARTBEAT-BALANCE";
    pub const HEARTBEAT_NORMAL: &str = "HEARTBEAT-NORMAL";
    pub const HEARTBEAT_SAFE: &str = "HEARTBEAT-SAFE";
    pub const MQ135_R0: &str = "MQ135-R0";
    pub const EMA_ALPHA_MQ135: &str = "EMA-ALPHA-MQ135";
    pub const EMA_ALPHA_DHT11: &str = "EMA-ALPHA-DHT11";
    pub const SHOW: &str = "SHOW";
    pub const EXIT: &str = "EXIT";
    pub const HELP: &str = "HELP";
}

/// Flags de campos ya configurados.
mod flag {
    pub const WIFI_SSID_OK: u16 = 1 << 0;
    pub const WIFI_PASS_OK: u16 = 1 << 1;
    pub const MQTT_URI_OK: u16 = 1 << 2;
    pub const DEVICE_NAME_OK: u16 = 1 << 3;
    pub const NETWORK_OK: u16 = 1 << 4;
    pub const EDGE_OK: u16 = 1 << 5;
    pub const URL_HTTPS_OK: u16 = 1 << 6;
    pub const SAMPLE_OK: u16 = 1 << 7;
    pub const ENERGY_OK: u16 = 1 << 8;
    pub const TBM_OK: u16 = 1 << 9;
    pub const TN_OK: u16 = 1 << 10;
    pub const TSM_OK: u16 = 1 << 11;
    pub const MQ135_R0_OK: u16 = 1 << 12;
    pub const MQ135_ALPHA_EMA_OK: u16 = 1 << 13;
    pub const DHT11_ALPHA_EMA_OK: u16 = 1 << 14;
    pub const ALL_OK: u16 = 0x7fff;
}

pub struct SettingsCli<U: UartIo, S: SystemSettings> {
    uart: U,
    store: S,
    buffer: Vec<u8>,
    flags: u16,
}

impl<U: UartIo, S: SystemSettings> SettingsCli<U, S> {
    pub fn new(uart: U, store: S) -> Self {
        Self {
            uart,
            store,
            buffer: Vec::with_capacity(BUFFER_SIZE),
            flags: 0,
        }
    }

    fn send(&mut self, text: &str) {
        self.uart.send(text.as_bytes());
    }

    /// Ejecuta el flujo de configuración manual.
    ///
    /// `loaded_existing` indica si ya había una configuración guardada (lo decide quien
    /// maneje la persistencia, fuera de este módulo).
    pub fn run(&mut self, loaded_existing: bool) {
        if !loaded_existing {
            self.setting_mode_start(false);
        } else if self.setting_mode_change() {
            self.setting_mode_start(true);
        }
    }

    /// Modo de configuración interactivo. Vuelve cuando el usuario sale con `EXIT`
    /// (una vez que se cumplen las condiciones necesarias, ver [`Self::handle_exit`]).
    fn setting_mode_start(&mut self, flag_process: bool) {
        self.uart.flush_input();
        self.show_menu();
        self.send("config>  ");

        loop {
            let Some(c) = self.uart.read_byte(None) else {
                continue;
            };

            if c == b'\n' || c == b'\r' {
                if self.buffer.is_empty() {
                    continue;
                }
                self.send("\r\n");
                let line = String::from_utf8_lossy(&self.buffer).into_owned();
                self.buffer.clear();

                if self.process_command(&line, flag_process) {
                    return;
                }

                self.send("\r\n");
                self.show_menu();
                self.send(" >  ");
            } else if self.buffer.len() < BUFFER_SIZE - 1 {
                self.buffer.push(c);
                self.uart.send(&[c]); // eco del caracter recibido
            }
        }
    }

    /// Pregunta si se desea modificar una configuración ya existente. Da 20 segundos
    /// para responder; por omisión (timeout) se asume que no.
    fn setting_mode_change(&mut self) -> bool {
        self.uart.flush_input();
        self.show_menu_change_settings();

        let start = Instant::now();
        let mut buffer: Vec<u8> = Vec::with_capacity(BUFFER_SIZE);

        loop {
            if start.elapsed() >= CHANGE_TIMEOUT {
                return false;
            }

            let Some(c) = self.uart.read_byte(Some(Duration::from_millis(100))) else {
                continue;
            };

            if c == b'\n' || c == b'\r' {
                if buffer.is_empty() {
                    continue;
                }
                self.send("\r\n");

                match buffer[0].to_ascii_uppercase() {
                    b'Y' => return true,
                    b'N' => return false,
                    _ => self.send("Error, comando invalido. Ingrese y o n\r\n"),
                }

                buffer.clear();
                self.send("\r\n");
                self.show_menu_change_settings();
            } else if buffer.len() < BUFFER_SIZE - 1 {
                buffer.push(c);
                self.uart.send(&[c]);
            }
        }
    }

    /// Procesa una línea de comando. Devuelve `true` únicamente cuando corresponde
    /// salir del modo configuración (comando `EXIT` aceptado).
    fn process_command(&mut self, line: &str, flag_process: bool) -> bool {
        let mut parts = line.splitn(2, char::is_whitespace);
        let cmd_raw = parts.next().unwrap_or("");
        if cmd_raw.is_empty() {
            self.send("Error, comando inválido. Use HELP para ver los comandos disponibles.\r\n");
            return false;
        }
        let param = parts.next().unwrap_or("").trim_start();
        let has_param = !param.is_empty();
        let cmd = cmd_raw.to_uppercase();

        match cmd.as_str() {
            cmd::HELP => {
                self.show_help();
                return false;
            }
            cmd::SHOW => {
                let snapshot = self.store.snapshot();
                self.show_config(&snapshot);
                return false;
            }
            cmd::WIFI_SSID => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error, falta parametro <SSID>\r\n")
                {
                    self.store.set_wifi_ssid(v);
                    self.flags |= flag::WIFI_SSID_OK;
                    self.send("Info: SSID configurado correctamente\r\n");
                }
                return false;
            }
            cmd::WIFI_PASS => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <password>\r\n")
                {
                    self.store.set_wifi_pass(v);
                    self.flags |= flag::WIFI_PASS_OK;
                    self.send("Info: password WiFi configurado correctamente\r\n");
                }
                return false;
            }
            cmd::MQTT_URI => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <uri>\r\n")
                {
                    if !v.starts_with(MQTTS_PREFIX) {
                        self.send(
                            "Error: MQTT uri erroneo. Falta mqtts:// como primer parametro\r\n",
                        );
                    } else {
                        self.store.set_mqtt_uri(v);
                        self.flags |= flag::MQTT_URI_OK;
                        self.send("Info: MQTT uri configurado correctamente\r\n");
                    }
                }
                return false;
            }
            cmd::NETWORK => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <id_network>\r\n")
                {
                    self.store.set_network(v);
                    self.flags |= flag::NETWORK_OK;
                    self.send("Info: red configurada correctamente\r\n");
                }
                return false;
            }
            cmd::EDGE => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <id_edge>\r\n")
                {
                    self.store.set_edge(v);
                    self.flags |= flag::EDGE_OK;
                    self.send("Info: edge configurado correctamente\r\n");
                }
                return false;
            }
            cmd::URL_HTTPS => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <url>\r\n")
                {
                    self.store.set_url_https(v);
                    self.flags |= flag::URL_HTTPS_OK;
                    self.send("Info: bypass url configurado correctamente\r\n");
                }
                return false;
            }
            cmd::DEVICE_NAME => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <name>\r\n")
                {
                    self.store.set_device_name(v);
                    self.flags |= flag::DEVICE_NAME_OK;
                    self.send("Info: nombre del dispositivo configurado correctamente\r\n");
                }
                return false;
            }
            cmd::SAMPLE => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <rate>\r\n")
                {
                    match v.parse::<u32>() {
                        Ok(val) if val > 0 && val <= u16::MAX as u32 => {
                            self.store.set_sample_rate(val);
                            self.flags |= flag::SAMPLE_OK;
                            self.send("Info: muestreo configurado correctamente\r\n");
                        }
                        _ => self.send("Error: ingrese un numero de muestreo valido\r\n"),
                    }
                }
                return false;
            }
            cmd::ENERGY_MODE => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <energy>\r\n")
                {
                    match v.parse::<u32>().ok().and_then(EnergyMode::from_u32) {
                        Some(mode) => {
                            self.store.set_energy_mode(mode);
                            self.flags |= flag::ENERGY_OK;
                            self.send(&format!(
                                "Info: modo de energia configurado correctamente. {}\r\n",
                                mode.label()
                            ));
                        }
                        None => self.send("Error: ingrese un modo de energia valido\r\n"),
                    }
                }
                return false;
            }
            cmd::DELETE_LINKAGE => {
                self.store.delete_linkage_flag();
                self.send("Info: linkage flag reseteado correctamente\r\n");
                return false;
            }
            cmd::HEARTBEAT_BALANCE => {
                self.handle_heartbeat(has_param, param, flag::TBM_OK, "balance", |s, us| {
                    s.store.set_heartbeat_balance(us)
                });
                return false;
            }
            cmd::HEARTBEAT_NORMAL => {
                self.handle_heartbeat(has_param, param, flag::TN_OK, "normal", |s, us| {
                    s.store.set_heartbeat_normal(us)
                });
                return false;
            }
            cmd::HEARTBEAT_SAFE => {
                self.handle_heartbeat(has_param, param, flag::TSM_OK, "safe", |s, us| {
                    s.store.set_heartbeat_safe(us)
                });
                return false;
            }
            cmd::MQ135_R0 => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <resistance>\r\n")
                {
                    match v.parse::<f32>() {
                        Ok(val) if val > 0.0 => {
                            self.store.set_mq135_r0(val);
                            self.flags |= flag::MQ135_R0_OK;
                            self.send(
                                "Info: resistencia R0 del MQ135 configurada correctamente\r\n",
                            );
                        }
                        Ok(_) => self.send("Error: la resistencia R0 debe ser mayor a 0\r\n"),
                        Err(_) => self
                            .send("Error: ingrese un formato de resistencia valido (ej: 70)\r\n"),
                    }
                }
                return false;
            }
            cmd::EMA_ALPHA_MQ135 => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <alpha>\r\n")
                {
                    match v.parse::<f32>() {
                        Ok(val) if val > 0.0 && val < 1.0 => {
                            self.store.set_mq135_alpha_ema(val);
                            self.flags |= flag::MQ135_ALPHA_EMA_OK;
                            self.send(
                                "Info: parámetro alpha del MQ135 configurado correctamente\r\n",
                            );
                        }
                        Ok(_) => self.send(
                            "Error: el parámetro alpha debe ser mayor a 0.0 y menor a 1.0\r\n",
                        ),
                        Err(_) => {
                            self.send("Error: ingrese un formato de alpha valido (ej: 0.5)\r\n")
                        }
                    }
                }
                return false;
            }
            cmd::EMA_ALPHA_DHT11 => {
                if let Some(v) =
                    self.require_param(has_param, param, "Error: falta parametro <alpha>\r\n")
                {
                    match v.parse::<f32>() {
                        Ok(val) if val > 0.0 && val < 1.0 => {
                            self.store.set_dht11_alpha_ema(val);
                            self.flags |= flag::DHT11_ALPHA_EMA_OK;
                            self.send(
                                "Info: parámetro alpha del DHT11 configurado correctamente\r\n",
                            );
                        }
                        Ok(_) => self.send(
                            "Error: el parámetro alpha debe ser mayor a 0.0 y menor a 1.0\r\n",
                        ),
                        Err(_) => {
                            self.send("Error: ingrese un formato de alpha valido (ej: 0.5)\r\n")
                        }
                    }
                }
                return false;
            }
            cmd::EXIT => return self.handle_exit(flag_process),
            _ => {}
        }

        // Comando no reconocido entre los anteriores.
        if !flag_process {
            self.send("Error: comando desconocido. Use HELP para ver comandos disponibles\r\n");
        }
        false
    }

    /// `EXIT`: en modo "cambio de config existente" siempre permite salir guardando.
    /// En modo "config nueva" solo permite salir si todos los campos obligatorios
    /// están completos.
    fn handle_exit(&mut self, flag_process: bool) -> bool {
        let configured = self.flags == flag::ALL_OK;

        if flag_process || configured {
            match self.store.save_to_nvs() {
                Ok(()) => {
                    self.send("\nInfo: configuración guardada correctamente. Saliendo del modo configuración.\r\n");
                    true
                }
                Err(()) => {
                    self.send("Error: no se pudo guardar la configuracion.\r\n");
                    false
                }
            }
        } else {
            self.send("Info: para salir del modo configuración, deben estar todos los campos configurados.\r\n");
            false
        }
    }

    /// Valida y aplica un comando `HEARTBEAT-*`: recibe segundos, y guarda en
    /// microsegundos con un offset de 5 seg.
    fn handle_heartbeat(
        &mut self,
        has_param: bool,
        param: &str,
        ok_flag: u16,
        label: &str,
        apply: impl FnOnce(&mut Self, u32),
    ) {
        let Some(v) = self.require_param(has_param, param, "Error: falta parametro <time>\r\n")
        else {
            return;
        };

        match v.parse::<u32>() {
            Ok(secs) if secs > 0 && secs <= u16::MAX as u32 => {
                let micros = secs * 1_000_000 + 5_000_000;
                apply(self, micros);
                self.flags |= ok_flag;
                self.send(&format!(
                    "Info: tiempo de latidos en estado {label} configurado correctamente\r\n"
                ));
            }
            _ => self.send(&format!(
                "Error: ingrese un numero de tiempo de latido en estado {label} valido\r\n"
            )),
        }
    }

    /// Si `has_param` es falso, envía `msg` y devuelve `None`; de lo contrario
    /// devuelve `Some(param)`. Centraliza el chequeo repetido de "falta parámetro".
    fn require_param<'a>(&mut self, has_param: bool, param: &'a str, msg: &str) -> Option<&'a str> {
        if has_param {
            Some(param)
        } else {
            self.send(msg);
            None
        }
    }

    fn print_row(&mut self, text_color: &str, label: &str, value: &str) {
        let line = format!(
            "{b}│ {c}{label:<19} {b}│{r} {value:<30} {b}│\r\n",
            b = color::B_WHT,
            c = text_color,
            r = color::T_RST,
            label = label,
            value = value,
        );
        self.send(&line);
    }

    fn show_help(&mut self) {
        let (b, r, mag, cyn, yel, grn) = (
            color::B_WHT,
            color::T_RST,
            color::C_MAG,
            color::C_CYN,
            color::C_YEL,
            color::C_GRN,
        );
        let text = format!(
            "\r\n{b}┌──────────────────────────────────────────────────────────────────────────────────────────────┐\r\n\
│{mag}                                   COMANDOS DISPONIBLES                                       {b}│\r\n\
├──────────────────────────────────────────────────────────────────────────────────────────────┤\r\n\
│ {cyn}❖ CONFIGURACIÓN BÁSICA       {b}│                                                               │\r\n\
├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n\
│ {cyn}WIFI-SSID <ssid>             {b}│{r} Configura el SSID del WiFi                                    {b}│\r\n\
│ {cyn}WIFI-PASS <password>         {b}│{r} Configura la contraseña del WiFi                              {b}│\r\n\
│ {cyn}MQTT-URI <uri>               {b}│{r} Configura la URI de MQTT (mqtts)                              {b}│\r\n\
│ {cyn}NETWORK <id_red>             {b}│{r} Configura el ID de la red a la que se conectará               {b}│\r\n\
│ {cyn}EDGE-ID <id_edge>            {b}│{r} Configura el ID del Edge al que se conectará                  {b}│\r\n\
│ {cyn}URL-BYPASS <url>             {b}│{r} Configura la URL para conexión Bypass (https)                 {b}│\r\n\
│ {cyn}NAME-DEVICE <name>           {b}│{r} Configura el nombre del dispositivo                           {b}│\r\n\
│ {cyn}SAMPLE-TIME <time>           {b}│{r} Configura la frecuencia de envío de datos (min)               {b}│\r\n\
│ {cyn}ENERGY-MODE <energy>         {b}│{r} Modo de energía [0 = Ahorro | 1 = Medio | 2 = Max]            {b}│\r\n\
│ {cyn}DELETE-LINKAGE               {b}│{r} Elimina el flag de linkage para una nueva conexión            {b}│\r\n\
│ {cyn}HEARTBEAT-BALANCE <time>     {b}│{r} Latidos recibidos en estado Balance (seg)                     {b}│\r\n\
│ {cyn}HEARTBEAT-NORMAL <time>      {b}│{r} Latidos recibidos en estado Normal (seg)                      {b}│\r\n\
│ {cyn}HEARTBEAT-SAFE <time>        {b}│{r} Latidos recibidos en estado Safe (seg)                        {b}│\r\n\
├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n\
│ {yel}❖ CALIBRACIÓN DE SENSORES    {b}│                                                               │\r\n\
├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n\
│ {yel}MQ135-R0 <resistance>        {b}│{r} Configura la resistencia (kΩ) del sensor MQ135                {b}│\r\n\
│ {yel}EMA-ALPHA-MQ135 <alpha>      {b}│{r} Configura el parámetro Alpha del filtro EMA del MQ135         {b}│\r\n\
│ {yel}EMA-ALPHA-DHT11 <alpha>      {b}│{r} Configura el parámetro Alpha del filtro EMA del DHT11         {b}│\r\n\
├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n\
│ {grn}❖ INTERFAZ                   {b}│                                                               │\r\n\
├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n\
│ {grn}SHOW                         {b}│{r} Muestra la configuración actual                               {b}│\r\n\
│ {grn}HELP                         {b}│{r} Muestra este mensaje de ayuda                                 {b}│\r\n\
│ {grn}EXIT                         {b}│{r} Salir del modo configuración                                  {b}│\r\n\
└──────────────────────────────┴───────────────────────────────────────────────────────────────┘\r\n\
{r}\r\n"
        );
        self.send(&text);
    }

    fn show_config(&mut self, s: &SettingsSnapshot) {
        let b = color::B_WHT;
        self.send(&format!(
            "\r\n{b}┌─────────────────────┬────────────────────────────────┐\r\n"
        ));
        self.send(&format!(
            "│{mag} PARAMETRO           {b}│{mag} VALOR ACTUAL                   {b}│\r\n",
            mag = color::C_MAG,
            b = b
        ));
        self.send(&format!(
            "├─────────────────────┼────────────────────────────────┤\r\n"
        ));

        self.print_row(color::C_CYN, "WiFi SSID", &s.wifi_ssid);
        self.print_row(color::C_CYN, "WiFi Password", &s.wifi_password);
        self.print_row(color::C_CYN, "MQTT URI", &s.mqtt_uri);

        self.send(&format!(
            "{b}├─────────────────────┼────────────────────────────────┤\r\n"
        ));

        self.print_row(color::C_CYN, "Red ID", &s.network_id);
        self.print_row(color::C_CYN, "Edge ID", &s.edge_id);
        self.print_row(color::C_CYN, "Bypass URL", &s.bypass_url);
        self.print_row(color::C_CYN, "Nombre Dispositivo", &s.device_name);

        self.send(&format!(
            "{b}├─────────────────────┼────────────────────────────────┤\r\n"
        ));

        self.print_row(
            color::C_YEL,
            "Heartbeat Balance",
            &format!("{} s", s.heartbeat_balance_secs),
        );
        self.print_row(
            color::C_YEL,
            "Heartbeat Normal",
            &format!("{} s", s.heartbeat_normal_secs),
        );
        self.print_row(
            color::C_YEL,
            "Heartbeat Safe",
            &format!("{} s", s.heartbeat_safe_secs),
        );

        self.send(&format!(
            "{b}├─────────────────────┼────────────────────────────────┤\r\n"
        ));

        self.print_row(
            color::C_GRN,
            "Sample Rate",
            &format!("{} min", s.sample_rate_min),
        );
        self.print_row(
            color::C_GRN,
            "Modo de Energia",
            s.energy_mode.display_label(),
        );
        self.print_row(color::C_GRN, "MQ135 R0", &format!("{:.2} kOhm", s.mq135_r0));
        self.print_row(
            color::C_GRN,
            "MQ135 Alpha EMA",
            &format!("{:.2}", s.mq135_alpha_ema),
        );
        self.print_row(
            color::C_GRN,
            "DHT11 Alpha EMA",
            &format!("{:.2}", s.dht11_alpha_ema),
        );

        let r = color::T_RST;
        self.send(&format!(
            "{b}└─────────────────────┴────────────────────────────────┘\r\n{r}\r\n"
        ));
    }

    fn show_menu(&mut self) {
        self.send(
            "\r\n\
┌──────────────────────────────────────────────────────────────┐\r\n\
│\x1b[1;36m                     MODO DE CONFIGURACIÓN                    \x1b[0m│\r\n\
├──────────────────────────────────────────────────────────────┤\r\n\
│ Use 'HELP' para ver comandos disponibles                     │\r\n\
│ Use 'SHOW' para ver la configuración actual                  │\r\n\
│ Use 'EXIT' para salir                                        │\r\n\
├──────────────────────────────────────────────────────────────┤\r\n\
│\x1b[1;33m Info: Los cambios se guardan automáticamente. Para salir del \x1b[0m│\r\n\
│\x1b[1;33m modo configuración deben estar todos los campos completos.   \x1b[0m│\r\n\
└──────────────────────────────────────────────────────────────┘\r\n\r\n",
        );
    }

    fn show_menu_change_settings(&mut self) {
        let (b, r, mag, grn, yel) = (
            color::B_WHT,
            color::T_RST,
            color::C_MAG,
            color::C_GRN,
            color::C_YEL,
        );
        self.send(&format!(
            "\r\n{b}┌────────────────────────────────────────────────────────────┐\r\n\
│{grn}      Se ha detectado una configuracion guardada en NVS     {b}│\r\n\
├────────────────────────────────────────────────────────────┤\r\n\
│{r} ¿Desea cambiar algun atributo de la configuracion?         {b}│\r\n\
│{mag} Tiene 20 seg para responder. Por omision se asume 'n'.     {b}│\r\n\
├────────────────────────────────────────────────────────────┤\r\n\
│{r}  > Ingrese '{yel}y{r}' para cambiar la configuracion               {b}│\r\n\
│{r}  > Ingrese '{yel}n{r}' para usar la configuracion actual           {b}│\r\n\
└────────────────────────────────────────────────────────────┘\r\n\
{r} > "
        ));
    }
}
