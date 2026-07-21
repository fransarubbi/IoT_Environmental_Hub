use heapless::String;

/// Contrato que cualquier implementador de OTA debe cumplir
pub trait Ota {
    /// Comprueba el repositorio y devuelve `Some(version)` si hay una actualización.
    fn check_update(&self, current_version: &str) -> Result<Option<String<6>>, String<20>>;

    /// Descarga y aplica el firmware de una versión específica.
    fn perform_update(&self, target_version: &str) -> Result<(), String<20>>;
}
