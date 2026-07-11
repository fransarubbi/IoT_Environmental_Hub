//! Módulo HTTPS.

use crate::svc::http::Http;
use embedded_svc::http::client::Client as HttpClient;
use esp_idf_svc::http::client::{Configuration as HttpConfig, EspHttpConnection};
use esp_idf_svc::sys::esp_crt_bundle_attach;
use heapless::String;
use log::{error, info};
use std::time::Duration;

pub struct EspIdfBypassManager {
    url: String<60>,
}

impl EspIdfBypassManager {
    /// Constructor. Equivale a extraer la URL de settings.
    pub fn new(url: &str) -> Self {
        let mut u = String::<60>::new();
        let _ = u.push_str(&url[..url.len().min(60)]);
        Self { url: u }
    }
}

impl Http for EspIdfBypassManager {
    /// Crea una conexión HTTPS efímera, envía el paquete y libera recursos.
    /// Reemplaza por completo a `create_https_send_and_delete` en C.
    fn send_payload(&mut self, payload: &[u8]) -> Result<(), String<50>> {
        // Configuración del cliente
        let config = HttpConfig {
            crt_bundle_attach: Some(esp_crt_bundle_attach),
            timeout: Some(Duration::from_secs(5)),
            ..Default::default()
        };

        let connection = EspHttpConnection::new(&config).map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("Fallo HTTP conn: {}", e));
            s
        })?;
        let mut client = HttpClient::wrap(connection);

        // Configurar Headers y preparar POST
        let headers = [("Content-Type", "application/x-msgpack")];
        let mut request = client.post(&self.url, &headers).map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("Error POST: {}", e));
            s
        })?;

        // Escribir el payload
        request.write(payload).map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("Error payload: {}", e));
            s
        })?;

        // Enviar y esperar respuesta
        let response = request.submit().map_err(|e| {
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("Error submit: {}", e));
            s
        })?;

        let status = response.status();
        if (200..300).contains(&status) {
            info!("Alerta enviada. Status: {}", status);
            Ok(())
        } else {
            error!("Fallo envío. HTTP Status: {}", status);
            let mut s = String::<50>::new();
            let _ = core::fmt::write(&mut s, format_args!("HTTP error: {}", status));
            Err(s)
        }
    }
}
