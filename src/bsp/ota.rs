//! Módulo OTA (Over-The-Air).
//! Gestiona la comprobación de versiones y descarga de nuevo firmware.

use crate::svc::ota::*;
use async_channel::{Receiver, Sender};
use embedded_svc::http::client::Client as HttpClient;
use esp_idf_svc::http::client::{Configuration as HttpConfig, EspHttpConnection};
use esp_idf_svc::ota::EspOta;
use esp_idf_svc::sys::esp_crt_bundle_attach;
use heapless::String;
use log::{debug, error, info, warn};

const URL_VERSION: &str =
    "https://raw.githubusercontent.com/fransarubbi/IoT_Environmental_Hub/master/version.txt";
const URL_BIN_TEMPLATE: &str =
    "https://github.com/fransarubbi/IoT_Environmental_Hub/releases/download/v{}/firmware.bin";

pub const CURRENT_FIRMWARE_VERSION: &str = "0.21.0";

pub enum OtaResponse {
    NoUpdateAvailable,
    UpdatedSuccesful,
}

pub enum OtaCommand {
    CheckFirmware,
}

pub struct EspIdfOtaManager {
    sender: Sender<OtaResponse>,
    receiver: Receiver<OtaCommand>,
}

impl EspIdfOtaManager {
    pub fn new(sender: Sender<OtaResponse>, receiver: Receiver<OtaCommand>) -> Self {
        Self { sender, receiver }
    }

    pub async fn run(self) {
        loop {
            if let Ok(cmd) = self.receiver.recv().await {
                match cmd {
                    OtaCommand::CheckFirmware => {
                        debug!("OTA. Se recibió comando de CheckFirmware.");
                        match self.check_update(CURRENT_FIRMWARE_VERSION) {
                            Ok(update) => {
                                if update.is_some() {
                                    match self.perform_update(&update.unwrap()) {
                                        Ok(_) => {
                                            debug!("OTA. Actualización exitosa.");
                                            if let Err(e) =
                                                self.sender.try_send(OtaResponse::UpdatedSuccesful)
                                            {
                                                error!("no se pudo enviar UpdatedSuccesful. {e}");
                                            }
                                        }
                                        Err(e) => error!("{e}"),
                                    }
                                } else {
                                    debug!("OTA. No hay actualizaciones disponibles.");
                                    if let Err(e) =
                                        self.sender.try_send(OtaResponse::NoUpdateAvailable)
                                    {
                                        error!("no se pudo enviar NoUpdateAvailable. {e}");
                                    }
                                }
                            }
                            Err(e) => {
                                debug!("fallo al checkear actualizaciones. {e}");
                                if let Err(e) = self.sender.try_send(OtaResponse::NoUpdateAvailable)
                                {
                                    error!("no se pudo enviar NoUpdateAvailable. {e}");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /// Helper privado para configurar el cliente HTTP
    fn create_http_client() -> Result<HttpClient<EspHttpConnection>, String<100>> {
        let config = HttpConfig {
            crt_bundle_attach: Some(esp_crt_bundle_attach),
            timeout: Some(std::time::Duration::from_secs(10)),
            buffer_size: Some(4096),
            buffer_size_tx: Some(1024),
            ..Default::default()
        };

        let connection = EspHttpConnection::new(&config).map_err(|e| {
            let mut s = String::<100>::new();
            let _ = core::fmt::write(
                &mut s,
                format_args!("fallo al crear HTTP Connection: {}", e),
            );
            s
        })?;

        Ok(HttpClient::wrap(connection))
    }
}

impl Ota for EspIdfOtaManager {
    fn check_update(&self, current_version: &str) -> Result<Option<String<6>>, String<20>> {
        info!("chequeando versión del repo...");

        let mut client = Self::create_http_client().map_err(|e| {
            let mut s = String::<20>::new();
            let _ = core::fmt::write(&mut s, format_args!("{}", &e.as_str()[..e.len().min(20)]));
            s
        })?;

        // Hacer la petición GET
        let request = client.get(URL_VERSION).map_err(|_| {
            let mut s = String::<20>::new();
            let _ = core::fmt::write(&mut s, format_args!("GET err"));
            s
        })?;

        let mut response = request.submit().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("submit err");
            s
        })?;

        if response.status() != 200 {
            return Err(String::<20>::try_from("HTTP error").unwrap_or_default());
        }

        // Leer la respuesta
        let mut version_buf = [0u8; 32];
        let bytes_read = response.read(&mut version_buf).map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("read err");
            s
        })?;

        // Convertir los bytes a un heapless String y limpiarlo
        let raw = core::str::from_utf8(&version_buf[..bytes_read]).unwrap_or("");
        let trimmed = raw.trim();
        let mut remote_version = String::<6>::new();
        let _ = remote_version.push_str(&trimmed[..trimmed.len().min(6)]);

        info!(
            "comparando versiones. Version remota: {remote_version} - Version local: {current_version}"
        );

        if is_newer_version(&remote_version, current_version) {
            warn!("actualización encontrada (v{}).", remote_version);
            Ok(Some(remote_version))
        } else {
            info!("sistema actualizado (remoto = local). No se requiere OTA.");
            Ok(None)
        }
    }

    fn perform_update(&self, target_version: &str) -> Result<(), String<20>> {
        // Construimos la URL reemplazando {} con la versión
        let mut ota_url = String::<150>::new();
        if let Some(pos) = URL_BIN_TEMPLATE.find("{}") {
            let _ = ota_url.push_str(&URL_BIN_TEMPLATE[..pos]);
            let _ = ota_url.push_str(target_version);
            let _ = ota_url.push_str(&URL_BIN_TEMPLATE[pos + 2..]);
        }
        info!("iniciando descarga de firmware desde {}", ota_url);

        let mut client = Self::create_http_client().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("HTTP client err");
            s
        })?;

        // Iniciar la petición del binario
        let request = client.get(ota_url.as_str()).map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("GET bin err");
            s
        })?;

        let mut response = request.submit().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("submit bin err");
            s
        })?;

        if response.status() != 200 {
            return Err(String::<20>::try_from("HTTP bin err").unwrap_or_default());
        }

        // Preparar la partición OTA usando esp-idf-svc
        let mut ota = EspOta::new().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("EspOta init err");
            s
        })?;
        let mut ota_update = ota.initiate_update().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("OTA init upd err");
            s
        })?;

        // Descargar y escribir en fragmentos
        let mut buffer = [0u8; 4096];
        loop {
            let bytes_read = response.read(&mut buffer).map_err(|_| {
                let mut s = String::<20>::new();
                let _ = s.push_str("read stream err");
                s
            })?;

            if bytes_read == 0 {
                break; // Fin del archivo
            }

            ota_update.write(&buffer[..bytes_read]).map_err(|_| {
                let mut s = String::<20>::new();
                let _ = s.push_str("write flash err");
                s
            })?;
        }

        // Finalizar y validar
        ota_update.complete().map_err(|_| {
            let mut s = String::<20>::new();
            let _ = s.push_str("complete OTA err");
            s
        })?;

        info!("OTA completo y validado exitosamente.");
        Ok(())
    }
}

/// Compara versiones semánticas (ej. "1.2.3" vs "1.2.0")
/// Retorna `true` si `remote` es mayor que `local`
pub fn is_newer_version(remote: &str, local: &str) -> bool {
    let mut r_parts = remote.split('.');
    let mut l_parts = local.split('.');

    for _ in 0..3 {
        let r_num: u32 = r_parts.next().unwrap_or("0").parse().unwrap_or(0);
        let l_num: u32 = l_parts.next().unwrap_or("0").parse().unwrap_or(0);

        match r_num.cmp(&l_num) {
            core::cmp::Ordering::Greater => return true,
            core::cmp::Ordering::Less => return false,
            core::cmp::Ordering::Equal => continue,
        }
    }
    false
}
