// *todo*

//! Módulo HTTPS.

use async_channel::Receiver;
use esp_idf_svc::http::client::{
    Client as HttpClient, Configuration as HttpConfig, EspHttpConnection,
};
use esp_idf_sys::esp_crt_bundle_attach;
use log::{error, info, warn};

pub struct EspIdfBypassManager {
    url: String,
}

impl EspIdfBypassManager {
    /// Constructor. Equivale a extraer la URL de settings.
    pub fn new(url: &str) -> Self {
        Self {
            url: url.to_string(),
        }
    }
}

impl BypassService for EspIdfBypassManager {
    /// Crea una conexión HTTPS efímera, envía el paquete y libera recursos.
    /// Reemplaza por completo a `create_https_send_and_delete` en C.
    fn send_payload(&mut self, payload: &[u8]) -> Result<(), String> {
        // 1. Configuración del cliente (Reemplaza esp_http_client_config_t)
        let config = HttpConfig {
            crt_bundle_attach: Some(esp_crt_bundle_attach),
            timeout: Some(std::time::Duration::from_secs(5)),
            ..Default::default()
        };

        let connection = EspHttpConnection::new(&config)
            .map_err(|e| format!("Fallo al crear HTTP Connection: {}", e))?;
        let mut client = HttpClient::wrap(connection);

        // 2. Configurar Headers y preparar POST
        let headers = [("Content-Type", "application/x-msgpack")];
        let mut request = client
            .post(&self.url, &headers)
            .map_err(|e| format!("Error en request POST: {}", e))?;

        // 3. Escribir el payload (Reemplaza esp_http_client_set_post_field)
        request
            .write_all(payload)
            .map_err(|e| format!("Error escribiendo payload en la red: {}", e))?;

        // 4. Enviar y esperar respuesta (Reemplaza esp_http_client_perform)
        let response = request
            .submit()
            .map_err(|e| format!("Error finalizando request POST: {}", e))?;

        let status = response.status();
        if (200..300).contains(&status) {
            info!("Alerta enviada. Status: {}", status);
            Ok(())
        } else {
            error!("Fallo envío. HTTP Status: {}", status);
            Err(format!("HTTP HTTP error: {}", status))
        }
    }
}
