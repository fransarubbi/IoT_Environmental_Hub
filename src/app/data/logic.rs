use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use heapless::{Deque, String};
use log::{error, info};
use std::str::FromStr;
use std::sync::{Arc, RwLock};

use crate::{
    app::data::domain::*,
    app::fsm::domain::StateGeneral,
    app::monitor::logic::{Monitor, monitor_read},
    app::system_settings::domain::SystemSettings,
    bsp::wifi::get_unix_epoch,
    hal::sensors::*,
};

pub struct DataService {
    sender: Sender<DataServiceResponse>,
    receiver: Receiver<DataServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
}

impl DataService {
    pub fn new(
        sender: Sender<DataServiceResponse>,
        receiver: Receiver<DataServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
    ) -> Self {
        info!("creando DataService...");
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
        let (tx_to_service, rx) = bounded::<InternalEvent>(3);
        let (tx_scan, rx_scan) = bounded::<PeriodicCommand>(2);
        let (tx_sample, rx_sample) = bounded::<PeriodicCommand>(2);
        let (tx_sweeper, rx_sweeper) = bounded::<PeriodicCommand>(2);

        let mut data_backup: Deque<DataCache, 25> = Deque::new();
        let mut monitor_backup: Deque<Monitor, 25> = Deque::new();
        let mut alert_backup: Deque<AlertCache, 5> = Deque::new();

        let mut state = StateGeneral::InitSystem;
        let set_guard = self.settings.read().unwrap();
        let mut alert_analysis_temp = EmaDetector::new(set_guard.temp_alpha_ema());
        let mut alert_analysis_air = EmaDetector::new(set_guard.air_alpha_ema());
        drop(set_guard);

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

        executor
            .spawn(generic_timer(tx_to_service.clone(), rx_scan))
            .detach();
        executor
            .spawn(generic_timer(tx_to_service.clone(), rx_sample))
            .detach();
        executor
            .spawn(generic_timer(tx_to_service.clone(), rx_sweeper))
            .detach();

        // Closure que lee los sensores y actualiza el caché (sólo si no hubo error)
        let mut update_sensors = |cache: &mut DataCache| {
            if let Ok(dht_data) = dht11.read() {
                cache.dht11_temp = Some(dht_data.values[0].value);
                cache.dht11_hum = Some(dht_data.values[1].value);
            }
            if let Ok(mq_data) = mq135.read() {
                cache.mq135_aqi = Some(mq_data.values[0].value);
            }
            cache.last_updated = get_unix_epoch();
        };

        // Extrae los datos del caché y arma el paquete Report
        let send_report = |cache: &DataCache, sender: &Sender<DataServiceResponse>| {
            if let (
                Some(mq135_aqi),
                Some(dht11_temp),
                Some(dht11_hum),
                Some(pulse_counter),
                Some(pulse_max_duration),
            ) = (
                cache.mq135_aqi,
                cache.dht11_temp,
                cache.dht11_hum,
                cache.pulse_counter,
                cache.pulse_max_duration,
            ) {
                let _ = sender.try_send(DataServiceResponse::Report {
                    pulse_counter,
                    pulse_max_duration,
                    mq135_aqi,
                    dht11_temp,
                    dht11_hum,
                });
            }
        };

        // Poppers para el Sweeper (Devuelven true si la cola quedó/estaba vacía)
        let pop_and_send_data =
            |backup: &mut Deque<DataCache, 25>, sender: &Sender<DataServiceResponse>| -> bool {
                if let Some(data) = backup.pop_front() {
                    send_report(&data, sender);
                }
                backup.is_empty()
            };

        let pop_and_send_alerts =
            |backup: &mut Deque<AlertCache, 5>, sender: &Sender<DataServiceResponse>| -> bool {
                if let Some(alert) = backup.pop_front() {
                    if let (Some(initial_air_quality), Some(actual_air_quality)) =
                        (alert.initial_air_quality, alert.actual_air_quality)
                    {
                        let _ = sender.try_send(DataServiceResponse::AlertAir {
                            initial_air_quality,
                            actual_air_quality,
                        });
                    }
                    if let (Some(initial_temp), Some(actual_temp)) =
                        (alert.initial_temp, alert.actual_temp)
                    {
                        let _ = sender.try_send(DataServiceResponse::AlertTemp {
                            initial_temp,
                            actual_temp,
                        });
                    }
                }
                backup.is_empty()
            };

        let pop_and_send_monitor =
            |backup: &mut Deque<Monitor, 25>, sender: &Sender<DataServiceResponse>| -> bool {
                if let Some(m) = backup.pop_front() {
                    let _ = sender.try_send(DataServiceResponse::Monitor {
                        timestamp: m.timestamp,
                        uptime_sec: m.uptime_sec,
                        heap_free: m.heap_free,
                        heap_min_free: m.heap_min_free,
                        heap_largest_block: m.heap_largest_block,
                    });
                }
                backup.is_empty()
            };

        info!("iniciando DataService...");
        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                // Comandos Externos
                Either::First(Ok(DataServiceCommand::State(fsm_state))) => {
                    state = fsm_state;
                    let settings = self.settings.read().unwrap();
                    let energy_secs = get_scan(settings.energy_mode());
                    let sample_secs = settings.sample_rate() as u64;

                    // helper para comandos de timer
                    let start = |secs, ev| PeriodicCommand::Start {
                        interval_secs: secs,
                        event: ev,
                    };

                    match state {
                        StateGeneral::InitSystem => {
                            info!("DataService. Se recibio el estado: InitSystem.");
                            let _ = tx_scan.try_send(PeriodicCommand::Stop);
                            let _ = tx_sample.try_send(PeriodicCommand::Stop);
                            let _ = tx_sweeper.try_send(PeriodicCommand::Stop);
                        }
                        StateGeneral::Store => {
                            info!("DataService. Se recibio el estado: Store.");
                            let _ = tx_scan.try_send(start(energy_secs, InternalEvent::ScanTick));
                            let _ = tx_sample
                                .try_send(start(sample_secs * 2, InternalEvent::SampleTick));
                            let _ = tx_sweeper.try_send(PeriodicCommand::Stop);
                        }
                        StateGeneral::Normal => {
                            info!("DataService. Se recibio el estado: Normal.");
                            let _ = tx_scan.try_send(start(energy_secs, InternalEvent::ScanTick));
                            let _ =
                                tx_sample.try_send(start(sample_secs, InternalEvent::SampleTick));
                            let _ = tx_sweeper.try_send(start(5, InternalEvent::SweeperTick));
                        }
                        StateGeneral::Bypass => {
                            info!("DataService. Se recibio el estado: Bypass.");
                            let _ = tx_scan.try_send(start(energy_secs, InternalEvent::ScanTick));
                            let _ = tx_sample.try_send(PeriodicCommand::Stop);
                            let _ = tx_sweeper.try_send(PeriodicCommand::Stop);

                            if alert_cache.is_some_complete() {
                                let _ = self.sender.try_send(DataServiceResponse::AlertAir {
                                    initial_air_quality: alert_cache.initial_air_quality.unwrap(),
                                    actual_air_quality: alert_cache.actual_air_quality.unwrap(),
                                });
                                let _ = self.sender.try_send(DataServiceResponse::AlertTemp {
                                    initial_temp: alert_cache.initial_temp.unwrap(),
                                    actual_temp: alert_cache.actual_temp.unwrap(),
                                });
                            }
                        }
                        // Estados combinados con jitter y frecuencias configurables
                        StateGeneral::Alert { frequency, jitter }
                        | StateGeneral::Data { frequency, jitter }
                        | StateGeneral::Monitor { frequency, jitter } => {
                            info!("DataService. Se recibio la phase: {:#?}.", state);
                            let sweeper_time = frequency + random_jitter(jitter);
                            let _ = tx_scan.try_send(start(energy_secs, InternalEvent::ScanTick));
                            let _ = tx_sample.try_send(PeriodicCommand::Stop);
                            let _ = tx_sweeper
                                .try_send(start(sweeper_time as u64, InternalEvent::SweeperTick));
                        }
                        StateGeneral::Safe { frequency, jitter } => {
                            info!("DataService. Se recibio el estado: Safe.");
                            let sweeper_time = frequency + random_jitter(jitter);
                            let _ = tx_scan.try_send(start(energy_secs, InternalEvent::ScanTick));
                            let _ = tx_sample
                                .try_send(start(sample_secs * 2, InternalEvent::SampleTick));
                            let _ = tx_sweeper
                                .try_send(start(sweeper_time as u64, InternalEvent::SweeperTick));
                        }
                        _ => {}
                    }
                }
                Either::First(Err(e)) => error!("{e}"),

                // Eventos internos (timers)
                Either::Second(Ok(event)) => match event {
                    InternalEvent::ScanTick => {
                        info!("DataService. Evento de ScanTick");
                        update_sensors(&mut data_cache);

                        // Procesar EMA Temperatura
                        if let Some(temp) = data_cache.dht11_temp {
                            if let Some(AlertEvent::Triggered {
                                normal_value,
                                current_value,
                            }) = alert_analysis_temp.process(temp)
                            {
                                match state {
                                    StateGeneral::Store => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando señal a la fsm...",
                                            state
                                        );
                                        alert_cache.actual_temp = Some(current_value);
                                        alert_cache.initial_temp = Some(normal_value);
                                        let _ = self
                                            .sender
                                            .try_send(DataServiceResponse::AnAlertWasGenerated);
                                    }
                                    StateGeneral::Bypass => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando alerta...",
                                            state
                                        );
                                        let _ = self.sender.try_send(
                                            DataServiceResponse::BypassAlertTemp {
                                                initial_temp: normal_value,
                                                actual_temp: current_value,
                                            },
                                        );
                                    }
                                    _ => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando alerta...",
                                            state
                                        );
                                        let _ =
                                            self.sender.try_send(DataServiceResponse::AlertTemp {
                                                initial_temp: normal_value,
                                                actual_temp: current_value,
                                            });
                                    }
                                }
                            }
                        }

                        // Procesar EMA Calidad Aire
                        if let Some(aqi) = data_cache.mq135_aqi {
                            if let Some(AlertEvent::Triggered {
                                normal_value,
                                current_value,
                            }) = alert_analysis_air.process(aqi)
                            {
                                match state {
                                    StateGeneral::Store => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando señal a la fsm...",
                                            state
                                        );
                                        alert_cache.actual_air_quality = Some(current_value);
                                        alert_cache.initial_air_quality = Some(normal_value);
                                        let _ = self
                                            .sender
                                            .try_send(DataServiceResponse::AnAlertWasGenerated);
                                    }
                                    StateGeneral::Bypass => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando alerta...",
                                            state
                                        );
                                        let _ = self.sender.try_send(
                                            DataServiceResponse::BypassAlertAir {
                                                initial_air_quality: normal_value,
                                                actual_air_quality: current_value,
                                            },
                                        );
                                    }
                                    _ => {
                                        info!(
                                            "DataService. Se generó una alerta en estado: {:#?}. Enviando alerta...",
                                            state
                                        );
                                        let _ =
                                            self.sender.try_send(DataServiceResponse::AlertAir {
                                                initial_air_quality: normal_value,
                                                actual_air_quality: current_value,
                                            });
                                    }
                                }
                            }
                        }
                    }

                    InternalEvent::SampleTick => {
                        info!("DataService. Evento de SampleTick");
                        let now = get_unix_epoch();
                        // Refrescar si el dato es obsoleto (> 15s)
                        if (now - data_cache.last_updated) > 15 {
                            update_sensors(&mut data_cache);
                        }

                        if let Ok(ky_data) = ky037.read() {
                            data_cache.pulse_counter = Some(ky_data.values[0].value);
                            data_cache.pulse_max_duration = Some(ky_data.values[1].value);
                        } else {
                            data_cache.pulse_counter = Some(0.0);
                            data_cache.pulse_max_duration = Some(0.0);
                        }

                        match state {
                            StateGeneral::Normal => {
                                info!("DataService. Haciendo sampleo en estado {:#?}.", state);
                                send_report(&data_cache, &self.sender);
                                let m = monitor_read().await;
                                let _ = self.sender.try_send(DataServiceResponse::Monitor {
                                    timestamp: m.timestamp,
                                    uptime_sec: m.uptime_sec,
                                    heap_free: m.heap_free,
                                    heap_min_free: m.heap_min_free,
                                    heap_largest_block: m.heap_largest_block,
                                });
                            }
                            StateGeneral::Store => {
                                info!("DataService. Haciendo sampleo en estado {:#?}.", state);
                                if data_cache.is_some_complete() {
                                    let _ = data_backup.push_back(data_cache.clone());
                                }
                                let _ = monitor_backup.push_back(monitor_read().await);
                            }
                            _ => {}
                        }
                    }

                    InternalEvent::SweeperTick => {
                        info!("DataService. Evento de SweeperTick.");
                        let mut reschedule_sweeper = false;
                        let mut next_interval = 5;

                        match state {
                            StateGeneral::Normal => {
                                info!("DataService. Haciendo limpieza en estado {:#?}.", state);
                                if data_backup.is_empty() {
                                    info!("DataService. No hay datos en la cola data_backup.");
                                    next_interval = 20;
                                } else {
                                    info!("DataService. Enviando datos de la cola data_backup.");
                                    pop_and_send_data(&mut data_backup, &self.sender);
                                    next_interval = 5;
                                }
                                pop_and_send_alerts(&mut alert_backup, &self.sender);
                                pop_and_send_monitor(&mut monitor_backup, &self.sender);
                                reschedule_sweeper = true;
                            }
                            StateGeneral::Safe { frequency, jitter } => {
                                info!("DataService. Haciendo limpieza en estado {:#?}.", state);
                                let alert_empty =
                                    pop_and_send_alerts(&mut alert_backup, &self.sender);
                                let data_empty = pop_and_send_data(&mut data_backup, &self.sender);
                                let monitor_empty =
                                    pop_and_send_monitor(&mut monitor_backup, &self.sender);

                                if alert_empty && data_empty && monitor_empty {
                                    info!("DataService. Enviando notificación de colas vacias.");
                                    let _ =
                                        self.sender.try_send(DataServiceResponse::EmptyQueueSafe);
                                }
                                next_interval = frequency as u64 + random_jitter(jitter) as u64;
                                reschedule_sweeper = true;
                            }
                            StateGeneral::Alert { frequency, jitter } => {
                                info!("DataService. Haciendo limpieza en estado {:#?}.", state);
                                if alert_backup.is_empty() {
                                    info!(
                                        "DataService. Enviando notificación de cola alert_backup vacía."
                                    );
                                    let _ = self.sender.try_send(
                                        DataServiceResponse::EmptyQueuePhase {
                                            state: String::from_str("balance").unwrap(),
                                            phase: String::from_str("alert").unwrap(),
                                        },
                                    );
                                } else {
                                    info!("DataService. Enviando datos de la cola alert_backup.");
                                    pop_and_send_alerts(&mut alert_backup, &self.sender);
                                }
                                next_interval = frequency as u64 + random_jitter(jitter) as u64;
                                reschedule_sweeper = true;
                            }
                            StateGeneral::Data { frequency, jitter } => {
                                info!("DataService. Haciendo limpieza en estado {:#?}.", state);
                                if data_backup.is_empty() {
                                    info!(
                                        "DataService. Enviando notificación de cola data_backup vacía."
                                    );
                                    let _ = self.sender.try_send(
                                        DataServiceResponse::EmptyQueuePhase {
                                            state: String::from_str("balance").unwrap(),
                                            phase: String::from_str("data").unwrap(),
                                        },
                                    );
                                } else {
                                    info!("DataService. Enviando datos de la cola data_backup.");
                                    pop_and_send_data(&mut data_backup, &self.sender);
                                }
                                next_interval = frequency as u64 + random_jitter(jitter) as u64;
                                reschedule_sweeper = true;
                            }
                            StateGeneral::Monitor { frequency, jitter } => {
                                info!("DataService. Haciendo limpieza en estado {:#?}.", state);
                                if monitor_backup.is_empty() {
                                    info!(
                                        "DataService. Enviando notificación de cola monitor_backup vacía."
                                    );
                                    let _ = self.sender.try_send(
                                        DataServiceResponse::EmptyQueuePhase {
                                            state: String::from_str("balance").unwrap(),
                                            phase: String::from_str("monitor").unwrap(),
                                        },
                                    );
                                } else {
                                    info!("DataService. Enviando datos de la cola monitor_backup.");
                                    pop_and_send_monitor(&mut monitor_backup, &self.sender);
                                }
                                next_interval = frequency as u64 + random_jitter(jitter) as u64;
                                reschedule_sweeper = true;
                            }
                            _ => {}
                        }

                        if reschedule_sweeper {
                            let _ = tx_sweeper.try_send(PeriodicCommand::Start {
                                interval_secs: next_interval,
                                event: InternalEvent::SweeperTick,
                            });
                        }
                    }
                },
                Either::Second(Err(e)) => error!("{e}"),
            }
        }
    }
}
