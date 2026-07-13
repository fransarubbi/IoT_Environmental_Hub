use crate::hal::sensors::{Sensor, SensorData, SensorError, SensorResult, SensorType, SensorValue};
use core::sync::atomic::{AtomicU32, Ordering};
use esp_idf_hal::gpio::{Input, InterruptType, PinDriver};
use heapless::Vec;

// ─────────────────────────────────────────────
// Estado Global Atómico (Lock-free)
// ─────────────────────────────────────────────
// Estas variables viven estáticamente en memoria.
static COUNTER: AtomicU32 = AtomicU32::new(0);
static MAX_DURATION_US: AtomicU32 = AtomicU32::new(0);
static START_TIME_US: AtomicU32 = AtomicU32::new(0);

/// Driver para el sensor de sonido KY-037.
pub struct Ky037<'d> {
    id: &'static str,
    _pin: PinDriver<'d, Input>,
}

impl<'d> Ky037<'d> {
    pub fn new(id: &'static str, mut pin: PinDriver<'d, Input>) -> SensorResult<Self> {
        // Configuramos para que dispare en cualquier flanco
        pin.set_interrupt_type(InterruptType::AnyEdge)
            .map_err(|_| SensorError::CommunicationError)?;

        // Guardamos el número de pin casteado a i32 (c_int) para usarlo en la función FFI nativa
        let pin_num = pin.pin() as i32;

        unsafe {
            pin.subscribe(move || {
                // FFI Directo para máxima velocidad
                let level = esp_idf_hal::sys::gpio_get_level(pin_num);

                // Casteamos a u32 (el truncamiento es intencional y seguro)
                // Usamos '| 1' para garantizar que 'now' nunca sea 0, ya que usamos 0
                // como valor centinela de "no hay pulso activo". Esto inserta un error
                // máximo de 1 microsegundo, lo cual es irrelevante para el KY-037.
                let now = (esp_idf_hal::sys::esp_timer_get_time() as u32) | 1;

                if level == 1 {
                    // Flanco de subida: Guardamos el tiempo
                    START_TIME_US.store(now, Ordering::Relaxed);
                } else {
                    // Flanco de bajada: Recuperamos y ponemos a 0 en una instrucción
                    let start = START_TIME_US.swap(0, Ordering::Relaxed);

                    if start > 0 {
                        // wrapping_sub maneja la resta incluso si el timer de 32bits dio la vuelta
                        let duration = now.wrapping_sub(start);

                        COUNTER.fetch_add(1, Ordering::Relaxed);
                        MAX_DURATION_US.fetch_max(duration, Ordering::Relaxed);
                    }
                }
            })
            .map_err(|_| SensorError::CommunicationError)?;
        }

        // Habilitamos la interrupción
        pin.enable_interrupt()
            .map_err(|_| SensorError::CommunicationError)?;

        Ok(Self { id, _pin: pin })
    }
}

/// ─────────────────────────────────────────────
/// Implementación de la Interfaz Común
/// ─────────────────────────────────────────────
impl<'d> Sensor for Ky037<'d> {
    fn init(&mut self) -> SensorResult<()> {
        COUNTER.store(0, Ordering::SeqCst);
        MAX_DURATION_US.store(0, Ordering::SeqCst);
        START_TIME_US.store(0, Ordering::SeqCst);
        Ok(())
    }

    fn read(&mut self) -> SensorResult<SensorData> {
        let total_counter = COUNTER.swap(0, Ordering::SeqCst);
        let max_duration_us = MAX_DURATION_US.swap(0, Ordering::SeqCst);

        let max_duration_ms = (max_duration_us as f32) / 1000.0;
        let timestamp_ms = (unsafe { esp_idf_hal::sys::esp_timer_get_time() } / 1000) as u64;

        let val_counter = SensorValue {
            name: "sound_pulses",
            value: total_counter as f32,
            unit: "count",
            timestamp: timestamp_ms,
        };

        let val_duration = SensorValue {
            name: "max_pulse_duration",
            value: max_duration_ms,
            unit: "ms",
            timestamp: timestamp_ms,
        };

        let mut values = Vec::<SensorValue, 2>::new();
        let _ = values.push(val_counter);
        let _ = values.push(val_duration);

        Ok(SensorData {
            sensor_id: self.id,
            sensor_type: SensorType::KY037,
            values,
        })
    }

    fn sensor_type(&self) -> SensorType {
        SensorType::KY037
    }

    fn id(&self) -> &'static str {
        self.id
    }
}
