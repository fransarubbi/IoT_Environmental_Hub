//! Módulo OTA (Over-The-Air).
//! Gestiona la comprobación de versiones y descarga de nuevo firmware.

use esp_idf_svc::http::client::{
    Client as HttpClient, Configuration as HttpConfig, EspHttpConnection,
};
use esp_idf_svc::ota::EspOta;
use log::{error, info, warn};
use std::cmp::Ordering;

// FFI para adjuntar los certificados raíz globales y permitir HTTPS
use esp_idf_sys::esp_crt_bundle_attach;

const URL_VERSION: &str =
    "https://raw.githubusercontent.com/fransarubbi/IoT_Environmental_Hub/master/version.txt";
const URL_BIN_TEMPLATE: &str =
    "https://github.com/fransarubbi/IoT_Environmental_Hub/releases/download/v{}/firmware.bin";

pub struct EspIdfOtaManager;

impl EspIdfOtaManager {
    pub fn new() -> Self {
        Self {}
    }

    /// Helper privado para configurar el cliente HTTPS
    fn create_https_client() -> Result<HttpClient<EspHttpConnection>, String> {
        let config = HttpConfig {
            crt_bundle_attach: Some(esp_crt_bundle_attach),
            timeout: Some(std::time::Duration::from_secs(10)),
            ..Default::default()
        };

        let connection = EspHttpConnection::new(&config)
            .map_err(|e| format!("fallo al crear HTTP Connection: {}", e))?;

        Ok(HttpClient::wrap(connection))
    }
}

impl OtaService for EspIdfOtaManager {
    fn check_update(&self, current_version: &str) -> Result<Option<String>, String> {
        info!("chequeando versión del repo...");

        let mut client = Self::create_https_client()?;

        // Hacer la petición GET
        let request = client
            .get(URL_VERSION)
            .map_err(|e| format!("error en request GET: {}", e))?;

        let mut response = request
            .submit()
            .map_err(|e| format!("error enviando request: {}", e))?;

        if response.status() != 200 {
            return Err(format!("HTTP Status code: {}", response.status()));
        }

        // Leer la respuesta
        let mut version_buf = [0u8; 32];
        let bytes_read = response
            .read(&mut version_buf)
            .map_err(|e| format!("error leyendo respuesta: {}", e))?;

        // Convertir los bytes a un String de Rust y limpiarlo
        let remote_version = String::from_utf8_lossy(&version_buf[..bytes_read])
            .trim()
            .to_string();

        info!(
            "comparando. Version remota = '{}' vs version local = '{}'",
            remote_version, current_version
        );

        if is_newer_version(&remote_version, current_version) {
            warn!("actualización encontrada (v{}).", remote_version);
            Ok(Some(remote_version))
        } else {
            info!("sistema actualizado (remoto <= local). No se requiere OTA.");
            Ok(None)
        }
    }

    fn perform_update(&self, target_version: &str) -> Result<(), String> {
        let ota_url = URL_BIN_TEMPLATE.replace("{}", target_version);
        info!("iniciando descarga de firmware desde {}", ota_url);

        let mut client = Self::create_https_client()?;

        // Iniciar la petición del binario
        let request = client
            .get(&ota_url)
            .map_err(|e| format!("error en request GET (Binario): {}", e))?;
        let mut response = request
            .submit()
            .map_err(|e| format!("error enviando request (Binario): {}", e))?;

        if response.status() != 200 {
            return Err(format!(
                "error HTTP descargando binario: {}",
                response.status()
            ));
        }

        // Preparar la partición OTA usando esp-idf-svc
        let mut ota = EspOta::new().map_err(|e| format!("fallo inicializando EspOta: {}", e))?;
        let mut ota_update = ota
            .initiate_update()
            .map_err(|e| format!("fallo iniciando partición OTA: {}", e))?;

        // Descargar y escribir en fragmentos
        let mut buffer = [0u8; 4096];
        loop {
            let bytes_read = response
                .read(&mut buffer)
                .map_err(|e| format!("error leyendo stream del binario: {}", e))?;

            if bytes_read == 0 {
                break; // Fin del archivo
            }

            ota_update
                .write(&buffer[..bytes_read])
                .map_err(|e| format!("error escribiendo a flash OTA: {}", e))?;
        }

        // Finalizar y validar
        ota_update
            .complete()
            .map_err(|e| format!("error marcando partición OTA como completada: {}", e))?;

        info!("OTA completo y validado exitosamente.");
        Ok(())
    }
}
