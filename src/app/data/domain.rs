use anyhow::anyhow;
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::join::join;
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use esp_idf_hal::sys::esp_random;
use heapless::{Deque, String};
use log::{error, info};
use std::sync::{Arc, RwLock};

use crate::app::fsm::domain::StateForDataService;
use crate::app::system_settings::domain::{EnergyMode, SystemSettings};
use crate::bsp::wifi::get_unix_epoch;
use crate::hal::sensors::*;

const SCAN_TIME_IN_LOW_MODE: u64 = 20;
const SCAN_TIME_IN_NORMAL_MODE: u64 = 10;
const SCAN_TIME_IN_PERFORMANCE_MODE: u64 = 5;

pub struct DataService {
    sender: Sender<DataServiceResponse>,
    receiver: Receiver<DataServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
}

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
    // empty queue safe
    // empty queue phase
    // alerts
}

pub enum DataServiceCommand {
    State(StateForDataService),
}

#[derive(Clone)]
struct DataCache {
    last_updated: u64,
    pulse_counter: Option<f32>,
    pulse_max_duration: Option<f32>,
    mq135_aqi: Option<f32>,
    dht11_temp: Option<f32>,
    dht11_hum: Option<f32>,
}

impl DataCache {
    fn is_some_complete(&self) -> bool {
        if self.dht11_temp.is_some()
            && self.dht11_hum.is_some()
            && self.mq135_aqi.is_some()
            && self.pulse_counter.is_some()
            && self.pulse_max_duration.is_some()
        {
            true
        } else {
            false
        }
    }
}

struct AlertCache {
    initial_air_quality: Option<f32>,
    actual_air_quality: Option<f32>,
    initial_temp: Option<f32>,
    actual_temp: Option<f32>,
}

struct MonitorCache {
    // aun no determino que campos seran
}

enum PeriodicCommand {
    Start {
        interval_secs: u64,
        event: InternalEvent,
    },
    Stop,
}

#[derive(Clone)]
enum InternalEvent {
    ScanTick,
    SampleTick,
    SweeperTick,
}

impl DataService {
    pub fn new(
        sender: Sender<DataServiceResponse>,
        receiver: Receiver<DataServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
    ) -> Self {
        Self {
            sender,
            receiver,
            settings,
        }
    }

    pub async fn run<'a>(
        self,
        executor: &'a LocalExecutor<'a>,
        mut dht11: impl Sensor + 'a,
        mut mq135: impl Sensor + 'a,
        mut ky037: impl Sensor + 'a,
    ) {
        let (tx_to_service, rx) = bounded::<InternalEvent>(10);
        let (tx_to_scan, rx_scan) = bounded::<PeriodicCommand>(5);
        let (tx_to_sample, rx_sample) = bounded::<PeriodicCommand>(5);
        let (tx_to_sweeper, rx_sweeper) = bounded::<PeriodicCommand>(5);

        let mut data_backup: Deque<DataCache, 25> = Deque::new();
        let mut monitor_backup: Deque<MonitorCache, 25> = Deque::new();
        let mut alert_backup: Deque<AlertCache, 5> = Deque::new();

        let mut state = StateForDataService::None;

        let mut data_cache = DataCache {
            mq135_aqi: None,
            dht11_temp: None,
            dht11_hum: None,
            pulse_counter: None,
            pulse_max_duration: None,
            last_updated: 0,
        };

        let mut alert_cache = AlertCache {
            initial_air_quality: None,
            actual_air_quality: None,
            initial_temp: None,
            actual_temp: None,
        };

        let tx = tx_to_service.clone();
        executor.spawn(scan_timer(tx, rx_scan)).detach();

        let tx = tx_to_service.clone();
        executor.spawn(sample_timer(tx, rx_sample)).detach();

        let tx = tx_to_service.clone();
        executor.spawn(sweeper_timer(tx, rx_sweeper)).detach();

        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                Either::First(Ok(cmd)) => match cmd {
                    DataServiceCommand::State(fsm_state) => {
                        state = fsm_state;

                        match state {
                            StateForDataService::Store => {
                                let sample_time = 2 * self.settings.read().unwrap().sample_rate();
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());

                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }
                                if let Err(e) = tx_to_sample.try_send(PeriodicCommand::Start {
                                    interval_secs: sample_time as u64,
                                    event: InternalEvent::SampleTick,
                                }) {
                                    error!("no se pudo notificar a sample_timer. {e}");
                                }

                                // guardar data en data_backup
                                // guardar monitor en monitor_backup
                                // SI HAY ALERTAS, NOTIFICAR A LA FSM DE QUE EXISTE LA ALERTA
                            }
                            StateForDataService::Alert { frequency, jitter } => {
                                let ran_jitter = random_jitter(jitter);
                                let sweeper_timer = frequency + ran_jitter;
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: sweeper_timer as u64,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }

                                // send_alert()
                                // NO DATA y NO MONITOR
                                // vaciar alert_backup y notificar a la fsm (la FSM generara la peticion del mensaje EmptyQueue)
                            }
                            StateForDataService::Data { frequency, jitter } => {
                                let ran_jitter = random_jitter(jitter);
                                let sweeper_timer = frequency + ran_jitter;
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: sweeper_timer as u64,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }

                                // send_alert()
                                // send_data()
                                // NO MONITOR
                                // vaciar data_backup y notificar a la fsm (la FSM generara la peticion del mensaje EmptyQueue)
                            }
                            StateForDataService::Monitor { frequency, jitter } => {
                                let ran_jitter = random_jitter(jitter);
                                let sweeper_timer = frequency + ran_jitter;
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: sweeper_timer as u64,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }

                                // send_alert()
                                // send_data()
                                // send_monitor()
                                // vaciar monitor_backup y notificar a la fsm (la FSM generara la peticion del mensaje EmptyQueue)
                            }
                            StateForDataService::Normal => {
                                let sample_time = self.settings.read().unwrap().sample_rate();
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: 5,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }
                                if let Err(e) = tx_to_sample.try_send(PeriodicCommand::Start {
                                    interval_secs: sample_time as u64,
                                    event: InternalEvent::SampleTick,
                                }) {
                                    error!("no se pudo notificar a sample_timer. {e}");
                                }

                                // activar sweeper_task(). Se activa cada 5 seg y si detecta que no hay data para enviar, se configura
                                // para despertarse cada 20 seg. Si cuando vuelve encuentra datos, se configura de nuevo cada 5 seg.

                                // send_alert()
                                // send_data()
                                // send_monitor( 2*sample_rate )
                            }
                            StateForDataService::Bypass => {
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }

                                // enviar la alerta que quedo cacheada
                                // send_alert()
                                // NO DATA y NO MONITOR
                            }
                            StateForDataService::Safe { frequency, jitter } => {
                                let sample_time = 2 * self.settings.read().unwrap().sample_rate();
                                let ran_jitter = random_jitter(jitter);
                                let sweeper_timer = frequency + ran_jitter;
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: sweeper_timer as u64,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }
                                if let Err(e) = tx_to_sample.try_send(PeriodicCommand::Start {
                                    interval_secs: sample_time as u64,
                                    event: InternalEvent::SampleTick,
                                }) {
                                    error!("no se pudo notificar a sample_timer. {e}");
                                }

                                // send_alert()
                                // NO DATA y NO MONITOR
                                // vaciar las 3 colas. Notificar a la fsm (la FSM generara la peticion del mensaje EmptyQueue)
                            }
                            StateForDataService::Cooling | StateForDataService::UpdateScore => {
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Stop) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }
                                let energy = get_scan(self.settings.read().unwrap().energy_mode());
                                if let Err(e) = tx_to_scan.try_send(PeriodicCommand::Start {
                                    interval_secs: energy as u64,
                                    event: InternalEvent::ScanTick,
                                }) {
                                    error!("no se pudo notificar a scan_timer. {e}");
                                }

                                // NO DATA y NO MONITOR
                                // Notificar a la FSM que ocurrio una alerta.
                            }
                            StateForDataService::None => {
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Stop) {
                                    error!("no se pudo notificar a sweeper_timer. {e}");
                                }

                                // NO ALERT, NO DATA y NO MONITOR
                            }
                        }
                    }
                },
                Either::First(Err(e)) => {
                    error!("{e}")
                }

                Either::Second(Ok(cmd)) => match cmd {
                    InternalEvent::SampleTick => {
                        match state {
                            StateForDataService::Normal => {
                                // Si el dato cacheado es reciente (menos de 15 seg), usamos ese.
                                // Sino, pedimos uno nuevo.
                                let now = get_unix_epoch();
                                if (now - data_cache.last_updated) <= 15 {
                                    if let (
                                        Some(mq135_aqi),
                                        Some(dht11_temp),
                                        Some(dht11_hum),
                                        Some(pulse_counter),
                                        Some(pulse_max_duration),
                                    ) = (
                                        data_cache.mq135_aqi,
                                        data_cache.dht11_temp,
                                        data_cache.dht11_hum,
                                        data_cache.pulse_counter,
                                        data_cache.pulse_max_duration,
                                    ) {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::Report {
                                                pulse_counter,
                                                pulse_max_duration,
                                                mq135_aqi,
                                                dht11_temp,
                                                dht11_hum,
                                            })
                                        {
                                            error!("no se pudo enviar Report. {e}");
                                        }
                                    } else {
                                        // generar datos nuevos
                                        let dht11_res = dht11.read();
                                        let mq135_res = mq135.read();
                                        let ky037_res = ky037.read();

                                        if let Ok(dht_data) = dht11_res {
                                            data_cache.dht11_temp = Some(dht_data.values[0].value);
                                            data_cache.dht11_hum = Some(dht_data.values[1].value);
                                        }

                                        if let Ok(mq_data) = mq135_res {
                                            data_cache.mq135_aqi = Some(mq_data.values[0].value);
                                        }

                                        if let Ok(ky_data) = ky037_res {
                                            data_cache.pulse_counter =
                                                Some(ky_data.values[0].value);
                                            data_cache.pulse_max_duration =
                                                Some(ky_data.values[1].value);
                                        }

                                        data_cache.last_updated = get_unix_epoch();

                                        if let (
                                            Some(mq135_aqi),
                                            Some(dht11_temp),
                                            Some(dht11_hum),
                                            Some(pulse_counter),
                                            Some(pulse_max_duration),
                                        ) = (
                                            data_cache.mq135_aqi,
                                            data_cache.dht11_temp,
                                            data_cache.dht11_hum,
                                            data_cache.pulse_counter,
                                            data_cache.pulse_max_duration,
                                        ) {
                                            if let Err(e) =
                                                self.sender.try_send(DataServiceResponse::Report {
                                                    pulse_counter,
                                                    pulse_max_duration,
                                                    mq135_aqi,
                                                    dht11_temp,
                                                    dht11_hum,
                                                })
                                            {
                                                error!("no se pudo enviar Report. {e}");
                                            }
                                        }
                                    }
                                } else {
                                    // generar datos nuevos
                                    let dht11_res = dht11.read();
                                    let mq135_res = mq135.read();
                                    let ky037_res = ky037.read();

                                    if let Ok(dht_data) = dht11_res {
                                        data_cache.dht11_temp = Some(dht_data.values[0].value);
                                        data_cache.dht11_hum = Some(dht_data.values[1].value);
                                    }

                                    if let Ok(mq_data) = mq135_res {
                                        data_cache.mq135_aqi = Some(mq_data.values[0].value);
                                    }

                                    if let Ok(ky_data) = ky037_res {
                                        data_cache.pulse_counter = Some(ky_data.values[0].value);
                                        data_cache.pulse_max_duration =
                                            Some(ky_data.values[1].value);
                                    }

                                    data_cache.last_updated = get_unix_epoch();

                                    if let (
                                        Some(mq135_aqi),
                                        Some(dht11_temp),
                                        Some(dht11_hum),
                                        Some(pulse_counter),
                                        Some(pulse_max_duration),
                                    ) = (
                                        data_cache.mq135_aqi,
                                        data_cache.dht11_temp,
                                        data_cache.dht11_hum,
                                        data_cache.pulse_counter,
                                        data_cache.pulse_max_duration,
                                    ) {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::Report {
                                                pulse_counter,
                                                pulse_max_duration,
                                                mq135_aqi,
                                                dht11_temp,
                                                dht11_hum,
                                            })
                                        {
                                            error!("no se pudo enviar Report. {e}");
                                        }
                                    }
                                }
                            }
                            StateForDataService::Store => {
                                // Si el dato cacheado es reciente, se guarda ese en la cola.
                                // Si es viejo, se crea un dato nuevo y se inserta ese en la cola.
                                let now = get_unix_epoch();
                                if (now - data_cache.last_updated) <= 15 {
                                    if data_cache.is_some_complete() {
                                        if let Err(_) = data_backup.push_back(data_cache.clone()) {
                                            error!("no se pudo insertar data_cache en data_backup");
                                        }
                                    } else {
                                        // generar datos nuevos
                                        let dht11_res = dht11.read();
                                        let mq135_res = mq135.read();
                                        let ky037_res = ky037.read();

                                        if let Ok(dht_data) = dht11_res {
                                            data_cache.dht11_temp = Some(dht_data.values[0].value);
                                            data_cache.dht11_hum = Some(dht_data.values[1].value);
                                        }

                                        if let Ok(mq_data) = mq135_res {
                                            data_cache.mq135_aqi = Some(mq_data.values[0].value);
                                        }

                                        if let Ok(ky_data) = ky037_res {
                                            data_cache.pulse_counter =
                                                Some(ky_data.values[0].value);
                                            data_cache.pulse_max_duration =
                                                Some(ky_data.values[1].value);
                                        }

                                        data_cache.last_updated = get_unix_epoch();

                                        if let Err(_) = data_backup.push_back(data_cache.clone()) {
                                            error!("no se pudo insertar data_cache en data_backup");
                                        }
                                    }
                                } else {
                                    // generar datos nuevos
                                    let dht11_res = dht11.read();
                                    let mq135_res = mq135.read();
                                    let ky037_res = ky037.read();

                                    if let Ok(dht_data) = dht11_res {
                                        data_cache.dht11_temp = Some(dht_data.values[0].value);
                                        data_cache.dht11_hum = Some(dht_data.values[1].value);
                                    }

                                    if let Ok(mq_data) = mq135_res {
                                        data_cache.mq135_aqi = Some(mq_data.values[0].value);
                                    }

                                    if let Ok(ky_data) = ky037_res {
                                        data_cache.pulse_counter = Some(ky_data.values[0].value);
                                        data_cache.pulse_max_duration =
                                            Some(ky_data.values[1].value);
                                    }

                                    data_cache.last_updated = get_unix_epoch();

                                    if let Err(_) = data_backup.push_back(data_cache.clone()) {
                                        error!("no se pudo insertar data_cache en data_backup");
                                    }
                                }
                            }
                            _ => {}
                        }
                    }
                    InternalEvent::ScanTick => {
                        // Escaneo de sensores
                        let dht11_res = dht11.read();
                        let mq135_res = mq135.read();
                        let ky037_res = ky037.read();

                        if let Ok(dht_data) = dht11_res {
                            data_cache.dht11_temp = Some(dht_data.values[0].value);
                            data_cache.dht11_hum = Some(dht_data.values[1].value);
                        }

                        if let Ok(mq_data) = mq135_res {
                            data_cache.mq135_aqi = Some(mq_data.values[0].value);
                        }

                        if let Ok(ky_data) = ky037_res {
                            data_cache.pulse_counter = Some(ky_data.values[0].value);
                            data_cache.pulse_max_duration = Some(ky_data.values[1].value);
                        }

                        data_cache.last_updated = get_unix_epoch();

                        // Invocar a las funciones de procesamiento de alertas (dht11 y mq135)
                    }
                    InternalEvent::SweeperTick => match state {
                        StateForDataService::Normal => {
                            if data_backup.is_empty() {
                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: 20,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo enviar start a sweeper_timer. {e}");
                                }
                            } else {
                                if let Some(data) = data_backup.pop_front() {
                                    if let (
                                        Some(mq135_aqi),
                                        Some(dht11_temp),
                                        Some(dht11_hum),
                                        Some(pulse_counter),
                                        Some(pulse_max_duration),
                                    ) = (
                                        data.mq135_aqi,
                                        data.dht11_temp,
                                        data.dht11_hum,
                                        data.pulse_counter,
                                        data.pulse_max_duration,
                                    ) {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::Report {
                                                pulse_counter,
                                                pulse_max_duration,
                                                mq135_aqi,
                                                dht11_temp,
                                                dht11_hum,
                                            })
                                        {
                                            error!("no se pudo enviar Report. {e}");
                                        }
                                    }
                                }

                                if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                    interval_secs: 5,
                                    event: InternalEvent::SweeperTick,
                                }) {
                                    error!("no se pudo enviar start a sweeper_timer. {e}");
                                }
                            }

                            if !alert_backup.is_empty() {
                                if let Some(alert) = alert_backup.pop_front() {
                                    if let (Some(initial_air_quality), Some(actual_air_quality)) =
                                        (alert.initial_air_quality, alert.actual_air_quality)
                                    {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::AlertAir {
                                                initial_air_quality,
                                                actual_air_quality,
                                            })
                                        {
                                            error!("no se pudo enviar AlertAir. {e}");
                                        }
                                    }
                                    if let (Some(initial_temp), Some(actual_temp)) =
                                        (alert.initial_temp, alert.actual_temp)
                                    {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::AlertTemp {
                                                initial_temp,
                                                actual_temp,
                                            })
                                        {
                                            error!("no se pudo enviar AlertTemp. {e}");
                                        }
                                    }
                                }
                            }

                            // Falta Monitor!
                        }
                        StateForDataService::Safe { frequency, jitter } => {
                            let mut flag: u8 = 0x00;
                            if alert_backup.is_empty() {
                                flag = flag | 0x01;
                            } else {
                                if let Some(alert) = alert_backup.pop_front() {
                                    if let (Some(initial_air_quality), Some(actual_air_quality)) =
                                        (alert.initial_air_quality, alert.actual_air_quality)
                                    {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::AlertAir {
                                                initial_air_quality,
                                                actual_air_quality,
                                            })
                                        {
                                            error!("no se pudo enviar AlertAir. {e}");
                                        }
                                    }
                                    if let (Some(initial_temp), Some(actual_temp)) =
                                        (alert.initial_temp, alert.actual_temp)
                                    {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::AlertTemp {
                                                initial_temp,
                                                actual_temp,
                                            })
                                        {
                                            error!("no se pudo enviar AlertTemp. {e}");
                                        }
                                    }
                                }
                            }

                            if data_backup.is_empty() {
                                flag = flag | 0x02;
                            } else {
                                if let Some(data) = data_backup.pop_front() {
                                    if let (
                                        Some(mq135_aqi),
                                        Some(dht11_temp),
                                        Some(dht11_hum),
                                        Some(pulse_counter),
                                        Some(pulse_max_duration),
                                    ) = (
                                        data.mq135_aqi,
                                        data.dht11_temp,
                                        data.dht11_hum,
                                        data.pulse_counter,
                                        data.pulse_max_duration,
                                    ) {
                                        if let Err(e) =
                                            self.sender.try_send(DataServiceResponse::Report {
                                                pulse_counter,
                                                pulse_max_duration,
                                                mq135_aqi,
                                                dht11_temp,
                                                dht11_hum,
                                            })
                                        {
                                            error!("no se pudo enviar Report. {e}");
                                        }
                                    }
                                }
                            }

                            if monitor_backup.is_empty() {
                                flag = flag | 0x04;
                            } else {
                                // Sacar de la cola monitor y enviar
                            }

                            if flag == 0x07 {
                                // Enviar notificacion de FSM que EmptyQueueSafe
                            }

                            let mut time = random_jitter(jitter);
                            time = time + frequency;
                            if let Err(e) = tx_to_sweeper.try_send(PeriodicCommand::Start {
                                interval_secs: time as u64,
                                event: InternalEvent::SweeperTick,
                            }) {
                                error!("no se pudo enviar start a sweeper_task. {e}");
                            }
                        }
                        StateForDataService::Alert { frequency, jitter } => {}
                        StateForDataService::Data { frequency, jitter } => {}
                        StateForDataService::Monitor { frequency, jitter } => {}
                        _ => {}
                    },
                },
                Either::Second(Err(e)) => {
                    error!("{e}")
                }
            }
        }
    }
}

async fn scan_timer(tx_to_service: Sender<InternalEvent>, rx_scan: Receiver<PeriodicCommand>) {
    loop {
        // --- ESTADO IDLE ---
        let (mut interval, mut current_event) = match rx_scan.recv().await {
            Ok(PeriodicCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(PeriodicCommand::Stop) => continue,
            Err(_) => break,
        };

        // --- ESTADO RUNNING ---
        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_scan.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx_to_service.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer periódico detenido.");
                        break; // Volver a IDLE
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

async fn sample_timer(tx_to_service: Sender<InternalEvent>, rx_sample: Receiver<PeriodicCommand>) {
    loop {
        // --- ESTADO IDLE ---
        let (mut interval, mut current_event) = match rx_sample.recv().await {
            Ok(PeriodicCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(PeriodicCommand::Stop) => continue,
            Err(_) => break,
        };

        // --- ESTADO RUNNING ---
        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_sample.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx_to_service.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer periódico detenido.");
                        break; // Volver a IDLE
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

async fn sweeper_timer(tx_to_service: Sender<InternalEvent>, rx_sample: Receiver<PeriodicCommand>) {
    loop {
        // --- ESTADO IDLE ---
        let (mut interval, mut current_event) = match rx_sample.recv().await {
            Ok(PeriodicCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(PeriodicCommand::Stop) => continue,
            Err(_) => break,
        };

        // --- ESTADO RUNNING ---
        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_sample.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx_to_service.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer periódico detenido.");
                        break; // Volver a IDLE
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

fn get_scan(energy: EnergyMode) -> u64 {
    match energy {
        EnergyMode::LOW => SCAN_TIME_IN_LOW_MODE,
        EnergyMode::NORMAL => SCAN_TIME_IN_NORMAL_MODE,
        EnergyMode::PERFORMANCE => SCAN_TIME_IN_PERFORMANCE_MODE,
    }
}

/// Genera un número aleatorio entre 0..N (inclusivo).
///
/// Utiliza aritmética de punto fijo para evitar la costosa operación de módulo (`%`),
/// mapeando el rango [0, u32::MAX] a [0, N].
pub fn random_jitter(n: u32) -> u32 {
    // Obtenemos el número aleatorio de hardware (0 a 0xFFFFFFFF)
    // Es unsafe solo porque es una llamada a una función externa de C
    let rand_val = unsafe { esp_random() } as u64;

    // Sumamos 1 a 'n' para que el rango sea inclusivo [0, N]
    let product = rand_val * ((n + 1) as u64);

    // Shift de 32 bits a la derecha para truncar el resultado
    (product >> 32) as u32
}
