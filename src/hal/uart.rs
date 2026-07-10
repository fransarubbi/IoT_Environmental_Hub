use embassy_time::Duration;
use esp_idf_svc::hal::uart::UartDriver;
use esp_idf_svc::hal::delay::{BLOCK, TickType};


/// Abstracción sobre el UART físico, para desacoplar el parser del driver concreto.
pub trait Uart {
    fn send(&mut self, bytes: &[u8]);
    fn read_byte(&mut self, timeout: Option<Duration>) -> Option<u8>;
    fn flush_input(&mut self);
}


/// Implementación concreta del trait Uart para el ESP32
pub struct EspIdfUartManager<'a> {
    driver: UartDriver<'a>,
}

impl<'a> EspIdfUartManager<'a> {
    pub fn new(driver: UartDriver<'a>) -> Self {
        Self { driver }
    }
}

impl<'a> Uart for EspIdfUartManager<'a> {
    fn send(&mut self, bytes: &[u8]) {
        // Ignoramos errores de hardware al escribir en la consola
        let _ = self.driver.write(bytes);
    }

    fn read_byte(&mut self, timeout: Option<Duration>) -> Option<u8> {
        let mut buf = [0u8; 1];
        
        // Convertimos el timeout de Rust a los Ticks nativos de FreeRTOS
        let delay = match timeout {
            Some(d) => TickType::new_millis(d.as_millis() as u64).into(),
            None => BLOCK, // BLOCK bloquea el hilo indefinidamente hasta que llegue un byte
        };

        match self.driver.read(&mut buf, delay) {
            Ok(len) if len > 0 => Some(buf[0]),
            _ => None,
        }
    }

    fn flush_input(&mut self) {
        let _ = self.driver.clear_rx();
    }
}