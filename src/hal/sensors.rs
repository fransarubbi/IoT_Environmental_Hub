use heapless::{String, Vec};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum SensorError {
    #[error("Error de comunicación con el sensor")]
    CommunicationError,

    #[error("Error de lectura: {0}")]
    ReadError(String<50>),

    #[error("Sensor no inicializado")]
    NotInitialized,

    #[error("Timeout: {0}ms")]
    Timeout(u32),

    #[error("CRC/Checksum inválido")]
    InvalidChecksum,

    #[error("Valor fuera de rango: {0}")]
    OutOfRange(String<50>),
}

pub type SensorResult<T> = Result<T, SensorError>;

pub trait Sensor: Send + Sync {
    fn init(&mut self) -> SensorResult<()>;
    fn read(&mut self) -> SensorResult<SensorData>;
    fn sensor_type(&self) -> SensorType;
    fn id(&self) -> &'static str;
}

#[derive(Debug, Clone)]
pub struct SensorData {
    pub sensor_id: &'static str,
    pub sensor_type: SensorType,
    pub values: Vec<SensorValue, 2>,
}

#[derive(Debug, Clone)]
pub struct SensorValue {
    pub name: &'static str,
    pub value: f32,
    pub unit: &'static str,
    pub timestamp: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SensorType {
    DHT11,
    MQ135,
    KY037,
}
