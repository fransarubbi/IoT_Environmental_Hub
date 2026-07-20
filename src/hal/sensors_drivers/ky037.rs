use crate::hal::sensors::{Sensor, SensorData, SensorError, SensorResult, SensorType, SensorValue};
use core::sync::atomic::{AtomicU32, Ordering};
use esp_idf_hal::gpio::{Input, PinDriver};
use esp_idf_hal::sys::{
    ESP_INTR_FLAG_IRAM, esp_timer_get_time, gpio_get_level, gpio_install_isr_service,
    gpio_int_type_t_GPIO_INTR_ANYEDGE, gpio_intr_disable, gpio_intr_enable, gpio_isr_handler_add,
    gpio_isr_handler_remove, gpio_set_intr_type,
};
use heapless::Vec;

static PULSES: AtomicU32 = AtomicU32::new(0);
static MAX_DURATION_US: AtomicU32 = AtomicU32::new(0);
static PULSE_START: AtomicU32 = AtomicU32::new(0);

#[unsafe(link_section = ".iram1.text")]
unsafe extern "C" fn ky037_raw_isr(arg: *mut core::ffi::c_void) {
    let pin_num = arg as i32;

    let level = unsafe { gpio_get_level(pin_num) };
    let now = unsafe { esp_timer_get_time() as u32 };

    if level == 1 {
        // Flanco de subida, guardamos inicio. (| 1 asegura que nunca sea 0)
        PULSE_START.store(now | 1, Ordering::Relaxed);
    } else {
        // Flanco de bajada
        let start = PULSE_START.swap(0, Ordering::Relaxed);
        if start > 0 {
            let duration = now.wrapping_sub(start);

            PULSES.fetch_add(1, Ordering::Relaxed);

            let current_max = MAX_DURATION_US.load(Ordering::Relaxed);
            if duration > current_max {
                MAX_DURATION_US.store(duration, Ordering::Relaxed);
            }
        }
    }
}

pub struct Ky037<'d> {
    id: &'static str,
    _pin: PinDriver<'d, Input>,
    pin_num: i32,
}

impl<'d> Ky037<'d> {
    pub fn new(id: &'static str, pin: PinDriver<'d, Input>) -> SensorResult<Self> {
        let pin_num = pin.pin() as i32;

        unsafe {
            let res = gpio_set_intr_type(pin_num, gpio_int_type_t_GPIO_INTR_ANYEDGE);
            if res != 0 {
                return Err(SensorError::CommunicationError);
            }

            let _ = gpio_install_isr_service(ESP_INTR_FLAG_IRAM as i32);

            gpio_isr_handler_remove(pin_num);

            let res = gpio_isr_handler_add(
                pin_num,
                Some(ky037_raw_isr),
                pin_num as *mut core::ffi::c_void,
            );
            if res != 0 {
                return Err(SensorError::CommunicationError);
            }

            let res = gpio_intr_enable(pin_num);
            if res != 0 {
                return Err(SensorError::CommunicationError);
            }
        }

        Ok(Self {
            id,
            _pin: pin,
            pin_num,
        })
    }
}

// Limpieza segura al destruir el objeto
impl<'d> Drop for Ky037<'d> {
    fn drop(&mut self) {
        unsafe {
            gpio_intr_disable(self.pin_num);
            gpio_isr_handler_remove(self.pin_num);
        }
    }
}

impl<'d> Sensor for Ky037<'d> {
    fn init(&mut self) -> SensorResult<()> {
        PULSES.store(0, Ordering::SeqCst);
        MAX_DURATION_US.store(0, Ordering::SeqCst);
        PULSE_START.store(0, Ordering::SeqCst);
        Ok(())
    }

    fn read(&mut self) -> SensorResult<SensorData> {
        let total_pulses = PULSES.swap(0, Ordering::SeqCst);
        let max_duration_us = MAX_DURATION_US.swap(0, Ordering::SeqCst);

        let max_duration_ms = (max_duration_us as f32) / 1000.0;
        let timestamp_ms = (unsafe { esp_idf_hal::sys::esp_timer_get_time() } / 1000) as u64;

        let val_counter = SensorValue {
            name: "sound_pulses",
            value: total_pulses as f32,
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
