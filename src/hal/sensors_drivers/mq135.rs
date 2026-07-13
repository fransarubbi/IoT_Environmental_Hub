use crate::hal::sensors::{Sensor, SensorData, SensorError, SensorResult, SensorType, SensorValue};
use core::borrow::Borrow;
use esp_idf_hal::{
    adc::{
        AdcChannel,
        oneshot::{AdcChannelDriver, AdcDriver},
    },
    delay::Ets,
    sys::esp_timer_get_time,
};
use heapless::{String, Vec};

// Parámetros del circuito y constantes
const MQ135_RLOAD_KOHM: f32 = 10.0;
const MQ135_VCC: f32 = 5.0;
const MQ135_RATIO_CLEAN: f32 = 3.6;
const MQ135_RATIO_DIRTY: f32 = 1.0;
const MQ135_MEDIAN_WINDOW: usize = 9;

/// Driver para el sensor de calidad de aire MQ135.
/// Utiliza los genéricos T (el Pin del canal) y M (el Borrow del driver ADC).
pub struct Mq135<'d, T, M>
where
    T: AdcChannel,
    M: Borrow<AdcDriver<'d, T::AdcUnit>>,
{
    id: &'static str,
    adc_pin: AdcChannelDriver<'d, T, M>,
    r0_kohm: f32,
    ema_alpha: f32,
    ema_value: f32,
    ema_initialized: bool,
}

/// ─────────────────────────────────────────────
/// Implementación MANUAL de Send + Sync
/// ─────────────────────────────────────────────
/// esp-idf-hal usa punteros crudos para la calibración del ADC en C,
/// lo que rompe los traits Send/Sync por defecto. Como ESP-IDF maneja
/// la concurrencia internamente de forma segura, podemos forzarlos.
unsafe impl<'d, T, M> Send for Mq135<'d, T, M>
where
    T: AdcChannel,
    M: Borrow<AdcDriver<'d, T::AdcUnit>>,
{
}

unsafe impl<'d, T, M> Sync for Mq135<'d, T, M>
where
    T: AdcChannel,
    M: Borrow<AdcDriver<'d, T::AdcUnit>>,
{
}

impl<'d, T, M> Mq135<'d, T, M>
where
    T: AdcChannel,
    M: Borrow<AdcDriver<'d, T::AdcUnit>>,
{
    /// Crea una nueva instancia. Solo necesitamos el canal (que ya posee o pide prestado al driver principal).
    pub fn new(
        id: &'static str,
        adc_pin: AdcChannelDriver<'d, T, M>,
        r0_kohm: f32,
        ema_alpha: f32,
    ) -> Self {
        Self {
            id,
            adc_pin,
            r0_kohm,
            ema_alpha,
            ema_value: 0.0,
            ema_initialized: false,
        }
    }

    fn read_voltage_median(&mut self) -> SensorResult<f32> {
        let mut readings: [u16; MQ135_MEDIAN_WINDOW] = [0; MQ135_MEDIAN_WINDOW];

        for i in 0..MQ135_MEDIAN_WINDOW {
            // El propio adc_pin tiene el método read() en oneshot para 0.46+
            let raw = self
                .adc_pin
                .read()
                .map_err(|_| SensorError::CommunicationError)?;

            readings[i] = raw;
            Ets::delay_us(200);
        }

        readings.sort_unstable();
        let median_raw = readings[MQ135_MEDIAN_WINDOW / 2];

        // Conversión manual a voltaje.
        let voltage_v = (median_raw as f32 / 4095.0) * 3.3;

        if voltage_v <= 0.01 || voltage_v >= MQ135_VCC {
            let mut err_msg = String::<50>::new();
            let _ = err_msg.push_str("voltaje ADC fuera de rango");
            return Err(SensorError::OutOfRange(err_msg));
        }

        Ok(voltage_v)
    }

    fn voltage_to_rs(&self, voltage_v: f32) -> SensorResult<f32> {
        if voltage_v <= 0.0 {
            let mut err_msg = String::<50>::new();
            let _ = err_msg.push_str("voltaje <= 0");
            return Err(SensorError::OutOfRange(err_msg));
        }

        let rs = MQ135_RLOAD_KOHM * (MQ135_VCC - voltage_v) / voltage_v;
        Ok(rs)
    }

    fn rs_to_aqi_percent(&self, rs_kohm: f32) -> f32 {
        if rs_kohm <= 0.0 || self.r0_kohm <= 0.0 {
            return 0.0;
        }

        let ratio = rs_kohm / self.r0_kohm;
        let aqi = ((ratio - MQ135_RATIO_DIRTY) / (MQ135_RATIO_CLEAN - MQ135_RATIO_DIRTY)) * 100.0;

        aqi.clamp(0.0, 100.0)
    }
}

/// ──────────────────────────────────────
/// Implementación del Trait
/// ──────────────────────────────────────
impl<'d, T, M> Sensor for Mq135<'d, T, M>
where
    T: AdcChannel,
    M: Borrow<AdcDriver<'d, T::AdcUnit>>,
{
    fn init(&mut self) -> SensorResult<()> {
        self.ema_initialized = false;
        self.ema_value = 0.0;
        Ok(())
    }

    fn read(&mut self) -> SensorResult<SensorData> {
        let voltage = self.read_voltage_median()?;
        let rs = self.voltage_to_rs(voltage)?;
        let aqi_raw = self.rs_to_aqi_percent(rs);

        if !self.ema_initialized {
            self.ema_value = aqi_raw;
            self.ema_initialized = true;
        } else {
            self.ema_value = (self.ema_alpha * aqi_raw) + ((1.0 - self.ema_alpha) * self.ema_value);
        }

        let timestamp_ms = (unsafe { esp_timer_get_time() } / 1000) as u64;

        let value = SensorValue {
            name: "air_quality",
            value: self.ema_value,
            unit: "%",
            timestamp: timestamp_ms,
        };

        let mut values = Vec::<SensorValue, 2>::new();
        let _ = values.push(value);

        Ok(SensorData {
            sensor_id: self.id,
            sensor_type: SensorType::MQ135,
            values,
        })
    }

    fn sensor_type(&self) -> SensorType {
        SensorType::MQ135
    }

    fn id(&self) -> &'static str {
        self.id
    }
}
