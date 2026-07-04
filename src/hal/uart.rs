/// Trait que abstrae comunicación UART
pub trait Uart {
    type Error;

    /// Inicializa UART con baudrate
    fn init(&mut self, baudrate: u32) -> Result<(), Self::Error>;

    /// Envía un byte
    fn send_byte(&mut self, byte: u8) -> Result<(), Self::Error>;

    /// Envía un buffer de bytes
    fn send_buffer(&mut self, buffer: &[u8]) -> Result<(), Self::Error> {
        for &byte in buffer {
            self.send_byte(byte)?;
        }
        Ok(())
    }

    /// Recibe un byte (bloqueante)
    fn receive_byte(&mut self) -> Result<u8, Self::Error>;

    /// Recibe múltiples bytes con timeout
    fn receive_buffer(&mut self, buffer: &mut [u8], timeout_ms: u32) -> Result<usize, Self::Error>;

    /// Verifica si hay datos disponibles
    fn data_available(&self) -> bool;
}
