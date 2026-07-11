use core::time::Duration;

use esp_idf_hal::delay::{Ets, TickType};
use esp_idf_hal::gpio::{InputPin, OutputPin};
use esp_idf_hal::rmt::config::{ReceiveConfig, RxChannelConfig};
use esp_idf_hal::rmt::{PinState, RxChannelDriver, Symbol};
use esp_idf_hal::sys;
use esp_idf_hal::units::Hertz;

use crate::bsp::wifi::get_unix_epoch;
use crate::hal::sensors::{Sensor, SensorData, SensorError, SensorResult, SensorType, SensorValue};

/// Driver concreto para el DHT11 utilizando el periférico RMT
pub struct Dht11RmtDriver<'a> {
    rx_channel: RxChannelDriver<'a>,
    pin_num: u8,
    id: &'static str,
}

impl<'a> Dht11RmtDriver<'a> {
    pub fn new<T>(pin: T, sensor_id: &'static str) -> Result<Self, anyhow::Error>
    where
        T: InputPin + OutputPin + 'a,
    {
        // Guardamos el número de pin ANTES de mover `pin` dentro del canal
        // RMT (su ownership queda consumida por `RxChannelDriver::new`),
        // porque lo necesitamos para bit-banguear la señal de start "a
        // mano" con las funciones crudas `gpio_*` más abajo.
        let pin_num = pin.pin();

        // Config a nivel de CANAL: se pasa una única vez, al crearlo.
        let channel_config = RxChannelConfig {
            resolution: Hertz(1_000_000), // 1 tick = 1 µs, para leer los t0/t1 directo en µs
            ..Default::default()
        };

        let rx_channel = RxChannelDriver::new(pin, &channel_config)
            .map_err(|e| anyhow::anyhow!("error inicializando RMT: {:?}", e))?;

        Ok(Self {
            rx_channel,
            pin_num,
            id: sensor_id,
        })
    }
}

/// Implementación del Trait
impl<'a> Sensor for Dht11RmtDriver<'a> {
    fn init(&mut self) -> SensorResult<()> {
        Ok(())
    }

    fn sensor_type(&self) -> SensorType {
        SensorType::DHT11
    }

    fn id(&self) -> &'static str {
        self.id
    }

    fn read(&mut self) -> SensorResult<SensorData> {
        // Señal de inicio
        unsafe {
            sys::gpio_set_direction(self.pin_num as i32, sys::gpio_mode_t_GPIO_MODE_OUTPUT_OD);
            sys::gpio_set_level(self.pin_num as i32, 0); // Línea LOW
        }

        Ets::delay_us(20_000); // 20ms

        unsafe {
            sys::gpio_set_level(self.pin_num as i32, 1); // Línea HIGH
            sys::gpio_set_direction(self.pin_num as i32, sys::gpio_mode_t_GPIO_MODE_INPUT);
        }

        // Recepcion RMT
        let receive_config = ReceiveConfig {
            signal_range_min: Duration::from_micros(1),
            signal_range_max: Duration::from_micros(900),
            timeout: Some(TickType::new_millis(100).ticks()),
            ..Default::default()
        };

        let mut symbols = [Symbol::default(); 80];

        let num_symbols = self
            .rx_channel
            .receive(&mut symbols, &receive_config)
            .map_err(|_| SensorError::Timeout(100))?;

        // Decodificacion de la trama
        let mut bits = 0u64;
        let mut bit_count = 0u32;

        for symbol in symbols.iter().take(num_symbols) {
            let p0 = symbol.level0();
            let p1 = symbol.level1();

            let t0 = p0.ticks.ticks();
            let t1 = p1.ticks.ticks();
            let v0_high = matches!(p0.pin_state, PinState::High);
            let v1_high = matches!(p1.pin_state, PinState::High);

            if t0 == 0 && t1 == 0 {
                break;
            }

            if !v0_high && t0 > 30 && t0 < 75 && v1_high {
                if t1 > 15 && t1 < 45 {
                    bits <<= 1;
                    bit_count += 1;
                } else if t1 >= 45 && t1 < 95 {
                    bits = (bits << 1) | 1;
                    bit_count += 1;
                } else if bit_count > 0 {
                    bit_count = 0;
                }
            } else if bit_count > 0 && bit_count < 40 {
                bit_count = 0;
            }

            if bit_count == 40 {
                break;
            }
        }

        if bit_count != 40 {
            let mut err_msg = heapless::String::<50>::new();
            let _ = err_msg.push_str("trama incompleta o corrompida");
            return Err(SensorError::ReadError(err_msg));
        }

        // Extraccion de datos y checksum
        let hum_i = ((bits >> 32) & 0xFF) as u8;
        let hum_d = ((bits >> 24) & 0xFF) as u8;
        let temp_i = ((bits >> 16) & 0xFF) as u8;
        let temp_d = ((bits >> 8) & 0xFF) as u8;
        let checksum = (bits & 0xFF) as u8;

        let expected_checksum = hum_i
            .wrapping_add(hum_d)
            .wrapping_add(temp_i)
            .wrapping_add(temp_d);

        if checksum != expected_checksum {
            return Err(SensorError::InvalidChecksum);
        }

        let temperature = temp_i as f32 + (temp_d as f32 * 0.1);
        let humidity = hum_i as f32 + (hum_d as f32 * 0.1);

        let ts = get_unix_epoch();
        let mut values = heapless::Vec::<SensorValue, 50>::new();
        let _ = values.push(SensorValue {
            name: "temperatura",
            value: temperature,
            unit: "C",
            timestamp: ts,
        });
        let _ = values.push(SensorValue {
            name: "humedad",
            value: humidity,
            unit: "%",
            timestamp: ts,
        });

        Ok(SensorData {
            sensor_id: self.id,
            sensor_type: SensorType::DHT11,
            values,
        })
    }
}

unsafe impl<'a> Send for Dht11RmtDriver<'a> {}
unsafe impl<'a> Sync for Dht11RmtDriver<'a> {}
