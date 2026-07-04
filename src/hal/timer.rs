/// Trait para temporizadores
pub trait Timer {
    type Error;

    /// Configura el timer con un periodo en microsegundos
    fn set_period_us(&mut self, period_us: u32) -> Result<(), Self::Error>;

    /// Inicia el timer
    fn start(&mut self) -> Result<(), Self::Error>;

    /// Detiene el timer
    fn stop(&mut self) -> Result<(), Self::Error>;

    /// Espera bloqueante hasta que el timer expire
    fn delay_ms(&mut self, ms: u32) -> Result<(), Self::Error>;

    /// Verifica si el timer ha expirado (no bloqueante)
    fn is_expired(&self) -> bool;

    /// Resetea el timer
    fn reset(&mut self) -> Result<(), Self::Error>;
}

/// Trait para generación de ticks de sistema
pub trait SystemClock {
    fn get_tick_ms(&self) -> u64;
    fn get_tick_us(&self) -> u64;
}
