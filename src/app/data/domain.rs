use async_channel::{Receiver, Sender};
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use esp_idf_hal::sys::esp_random;
use heapless::String;
use log::info;

use crate::{app::fsm::domain::StateForDataService, app::system_settings::domain::EnergyMode};

const SCAN_TIME_IN_LOW_MODE: u64 = 20;
const SCAN_TIME_IN_NORMAL_MODE: u64 = 10;
const SCAN_TIME_IN_PERFORMANCE_MODE: u64 = 5;
const BETA_ERROR: f32 = 0.05;
const K_SENSIBILIDAD: f32 = 2.0;
const UMBRAL_MINIMO_ABS: f32 = 10.0;
const HYSTERESIS: f32 = 0.8;

pub enum DataServiceResponse {
    Report {
        pulse_counter: f32,
        pulse_max_duration: f32,
        mq135_aqi: f32,
        dht11_temp: f32,
        dht11_hum: f32,
    },
    AlertAir {
        initial_air_quality: f32,
        actual_air_quality: f32,
    },
    AlertTemp {
        initial_temp: f32,
        actual_temp: f32,
    },
    Monitor {
        timestamp: u64,
        uptime_sec: u64,
        heap_free: u32,
        heap_min_free: u32,
        heap_largest_block: u32,
    },
    EmptyQueueSafe,
    AnAlertWasGenerated,
    EmptyQueuePhase {
        state: String<15>,
        phase: String<10>,
    },
    BypassAlertAir {
        initial_air_quality: f32,
        actual_air_quality: f32,
    },
    BypassAlertTemp {
        initial_temp: f32,
        actual_temp: f32,
    },
}

pub enum DataServiceCommand {
    State(StateForDataService),
}

#[derive(Clone)]
pub struct DataCache {
    pub last_updated: u64,
    pub pulse_counter: Option<f32>,
    pub pulse_max_duration: Option<f32>,
    pub mq135_aqi: Option<f32>,
    pub dht11_temp: Option<f32>,
    pub dht11_hum: Option<f32>,
}

impl DataCache {
    pub fn is_some_complete(&self) -> bool {
        self.dht11_temp.is_some()
            && self.dht11_hum.is_some()
            && self.mq135_aqi.is_some()
            && self.pulse_counter.is_some()
            && self.pulse_max_duration.is_some()
    }
}

#[derive(Clone)]
pub struct AlertCache {
    pub initial_air_quality: Option<f32>,
    pub actual_air_quality: Option<f32>,
    pub initial_temp: Option<f32>,
    pub actual_temp: Option<f32>,
}

impl AlertCache {
    pub fn is_some_complete(&self) -> bool {
        self.initial_air_quality.is_some()
            && self.actual_air_quality.is_some()
            && self.initial_temp.is_some()
            && self.actual_temp.is_some()
    }
}

pub enum PeriodicCommand {
    Start {
        interval_secs: u64,
        event: InternalEvent,
    },
    Stop,
}

#[derive(Clone)]
pub enum InternalEvent {
    ScanTick,
    SampleTick,
    SweeperTick,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DetectorState {
    Init,
    Normal,
    Alert,
}

#[derive(Debug, Clone, Copy)]
pub enum AlertEvent {
    Triggered {
        normal_value: f32,
        current_value: f32,
    },
    Resolved {
        normal_value: f32,
        current_value: f32,
    },
}

pub struct EmaDetector {
    state: DetectorState,
    alpha_ema: f32,
    ema_value: f32,
    ema_error: f32,
    value_before_alert: f32,
}

impl EmaDetector {
    pub fn new(alpha_ema: f32) -> Self {
        Self {
            state: DetectorState::Init,
            alpha_ema,
            ema_value: 0.0,
            ema_error: 0.0,
            value_before_alert: 0.0,
        }
    }

    pub fn process(&mut self, current_value: f32) -> Option<AlertEvent> {
        let error_actual = current_value - self.ema_value;
        let error_abs = error_actual.abs();
        let umbral_alerta_dinamico = (K_SENSIBILIDAD * self.ema_error) + UMBRAL_MINIMO_ABS;
        let mut event_to_emit = None;

        match self.state {
            DetectorState::Init => {
                self.ema_value = current_value;
                self.ema_error = 0.0;
                self.state = DetectorState::Normal;
            }
            DetectorState::Normal => {
                if error_abs > umbral_alerta_dinamico {
                    self.state = DetectorState::Alert;
                    self.value_before_alert = self.ema_value;
                    event_to_emit = Some(AlertEvent::Triggered {
                        normal_value: self.value_before_alert,
                        current_value,
                    });
                } else {
                    self.ema_error =
                        (BETA_ERROR * error_abs) + ((1.0 - BETA_ERROR) * self.ema_error);
                }
            }
            DetectorState::Alert => {
                if error_abs < (umbral_alerta_dinamico * HYSTERESIS) {
                    self.state = DetectorState::Normal;
                    event_to_emit = Some(AlertEvent::Resolved {
                        normal_value: self.value_before_alert,
                        current_value,
                    });
                    self.ema_error =
                        (BETA_ERROR * error_abs) + ((1.0 - BETA_ERROR) * self.ema_error);
                }
            }
        }
        if self.state != DetectorState::Init {
            self.ema_value =
                (self.alpha_ema * current_value) + ((1.0 - self.alpha_ema) * self.ema_value);
        }
        event_to_emit
    }
}

pub fn get_scan(energy: EnergyMode) -> u64 {
    match energy {
        EnergyMode::LOW => SCAN_TIME_IN_LOW_MODE,
        EnergyMode::NORMAL => SCAN_TIME_IN_NORMAL_MODE,
        EnergyMode::PERFORMANCE => SCAN_TIME_IN_PERFORMANCE_MODE,
    }
}

pub fn random_jitter(n: u32) -> u32 {
    let rand_val = unsafe { esp_random() } as u64;
    let product = rand_val * ((n + 1) as u64);
    (product >> 32) as u32
}

pub async fn generic_timer(
    tx_to_service: Sender<InternalEvent>,
    rx_cmd: Receiver<PeriodicCommand>,
) {
    loop {
        let (mut interval, mut current_event) = match rx_cmd.recv().await {
            Ok(PeriodicCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(PeriodicCommand::Stop) => continue,
            Err(_) => break,
        };

        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_cmd.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx_to_service.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer periódico detenido.");
                        break;
                    }
                    PeriodicCommand::Start {
                        interval_secs,
                        event,
                    } => {
                        info!("timer periódico actualizado sin detener el hilo.");
                        interval = interval_secs;
                        current_event = event;
                    }
                },
                Either::Second(Err(_)) => return,
            }
        }
    }
}
