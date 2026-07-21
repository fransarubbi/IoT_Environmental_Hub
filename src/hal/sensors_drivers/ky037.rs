use crate::hal::sensors::{Sensor, SensorData, SensorError, SensorResult, SensorType, SensorValue};
use esp_idf_hal::pcnt::PcntUnitDriver;
use heapless::Vec;

pub struct Ky037<'d> {
    id: &'static str,
    unit: PcntUnitDriver<'d>,
}

unsafe impl<'d> Send for Ky037<'d> {}
unsafe impl<'d> Sync for Ky037<'d> {}

impl<'d> Ky037<'d> {
    pub fn new(id: &'static str, unit: PcntUnitDriver<'d>) -> SensorResult<Self> {
        Ok(Self { id, unit })
    }
}

impl<'d> Sensor for Ky037<'d> {
    fn init(&mut self) -> SensorResult<()> {
        self.unit
            .clear_count()
            .map_err(|_| SensorError::CommunicationError)?;
        Ok(())
    }

    fn read(&mut self) -> SensorResult<SensorData> {
        // Obtenemos el conteo y limpiamos el registro inmediatamente
        let count_val = self.unit.get_count().unwrap_or(0);
        let _ = self.unit.clear_count();

        let total_pulses = count_val.abs() as f32;
        let timestamp_ms = (unsafe { esp_idf_hal::sys::esp_timer_get_time() } / 1000) as u64;

        let val_counter = SensorValue {
            name: "sound_pulses",
            value: total_pulses,
            unit: "count",
            timestamp: timestamp_ms,
        };

        let val_duration = SensorValue {
            name: "max_pulse_duration",
            value: 0.0,
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
