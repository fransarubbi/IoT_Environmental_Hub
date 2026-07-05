/// Abstracción sobre el UART físico, para desacoplar el parser del driver concreto.
pub trait UartIo {
    /// Envío bloqueante de bytes crudos
    fn send(&mut self, bytes: &[u8]);

    /// Lee un byte. `None` como timeout equivale a bloqueo indefinido
    /// `Some(d)` espera como máximo `d`. Devuelve `None` si no llegó
    /// ningún byte dentro del timeout.
    fn read_byte(&mut self, timeout: Option<Duration>) -> Option<u8>;

    /// Descarta cualquier byte pendiente en el buffer de recepción
    fn flush_input(&mut self);
}
