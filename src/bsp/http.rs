//! Módulo HTTPS.

use esp_idf_svc::http::client::{Configuration as HttpConfig, EspHttpConnection};
use embedded_svc::http::client::Client as HttpClient; 
use esp_idf_svc::sys::esp_crt_bundle_attach;
use std::time::Duration; 
use log::{error, info};
use crate::svc::http::Http;



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

impl Http for EspIdfBypassManager {

    /// Crea una conexión HTTPS efímera, envía el paquete y libera recursos.
    /// Reemplaza por completo a `create_https_send_and_delete` en C.
    fn send_payload(&mut self, payload: &[u8]) -> Result<(), String> {
        
        // Configuración del cliente
        let config = HttpConfig {
            crt_bundle_attach: Some(esp_crt_bundle_attach),
            timeout: Some(Duration::from_secs(5)),
            ..Default::default()
        };

        let connection = EspHttpConnection::new(&config)
            .map_err(|e| format!("Fallo al crear HTTP Connection: {}", e))?;
        let mut client = HttpClient::wrap(connection);

        // Configurar Headers y preparar POST
        let headers = [("Content-Type", "application/x-msgpack")];
        let mut request = client
            .post(&self.url, &headers)
            .map_err(|e| format!("Error en request POST: {}", e))?;

        // Escribir el payload
        request
            .write(payload)
            .map_err(|e| format!("Error escribiendo payload en la red: {}", e))?;

        // Enviar y esperar respuesta
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
