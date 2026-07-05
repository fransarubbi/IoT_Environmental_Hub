/// Comandos de control para la máquina de estados del Bypass
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum HttpCommand {
    Start,
    Stop,
}

/// Contrato para enviar payloads de emergencia.
pub trait Http {
    fn send_payload(&mut self, payload: &[u8]) -> Result<(), String>;
}
