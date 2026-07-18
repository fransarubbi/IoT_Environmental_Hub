use crate::app::heartbeat::domain::{
    ACTION_VECTOR_CAPACITY, Action, Event, FsmHeartbeat, HeartbeatCommand, HeartbeatResponse,
    State, StateForHeartbeat, Status, Transition, WatchdogCommand,
};
use crate::app::system_settings::domain::SystemSettings;
use async_channel::{Receiver, Sender};
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use heapless::Vec;
use log::{error, info, warn};
use std::sync::{Arc, RwLock};

pub async fn handler_heartbeat(
    tx_to_service: Sender<HeartbeatResponse>,
    tx_to_fsm: Sender<Event>,
    tx_to_timer: Sender<WatchdogCommand>,
    rx_service: Receiver<HeartbeatCommand>,
    rx_fsm: Receiver<Vec<Action, ACTION_VECTOR_CAPACITY>>,
    settings: Arc<RwLock<SystemSettings>>,
) {
    let mut fsm_state = StateForHeartbeat::None;
    info!("Iniciando handler de Heartbeat...");

    loop {
        match select(rx_service.recv(), rx_fsm.recv()).await {
            Either::First(Ok(cmd)) => match cmd {
                HeartbeatCommand::HeartbeatIncoming => {
                    info!("Heartbeat. Se recibió un latido desde el Edge.");
                    if let Err(e) = tx_to_fsm.try_send(Event::Heartbeat) {
                        error!("No se pudo enviar evento Heartbeat a la FSM. {e}");
                    }
                }
                HeartbeatCommand::State(state) => {
                    fsm_state = state;
                    info!(
                        "Heartbeat. Nuevo contexto desde FSM principal: {:#?}.",
                        fsm_state
                    );

                    // Si el sistema principal deja de esperar latidos, detenemos el watchdog
                    if matches!(fsm_state, StateForHeartbeat::None) {
                        let _ = tx_to_timer.try_send(WatchdogCommand::Stop);
                    }
                }
            },
            Either::First(Err(e)) => {
                error!("{e}");
            }
            Either::Second(Ok(cmd)) => {
                for action in cmd {
                    match action {
                        Action::OnEntry(state) => match state {
                            State::StartingWait | State::NotHeartbeatYet | State::ItsAlive => {
                                let timer: u32 = match fsm_state {
                                    StateForHeartbeat::Normal => {
                                        settings.read().unwrap().heartbeat_normal_mode()
                                    }
                                    StateForHeartbeat::Balance => {
                                        settings.read().unwrap().heartbeat_balance_mode()
                                    }
                                    StateForHeartbeat::Safe => {
                                        settings.read().unwrap().heartbeat_safe_mode()
                                    }
                                    _ => 0,
                                };

                                // PROTECCIÓN CONTRA TIMERS DE 0 SEGUNDOS
                                if timer > 0 {
                                    if let Err(e) = tx_to_timer.try_send(WatchdogCommand::Start {
                                        interval_secs: timer as u64,
                                        event: Event::Timeout,
                                    }) {
                                        error!("Error al iniciar watchdog: {e}");
                                    }
                                } else {
                                    warn!("Ignorando inicio de timer: fsm_state es None o 0.");
                                }
                            }
                            _ => {}
                        },
                        Action::SendStatusConditional(old_status, new_status) => {
                            if old_status != new_status {
                                if new_status == Status::Connected {
                                    info!("Heartbeat FSM: Estado cambiado a CONECTADO.");
                                    if let Err(e) =
                                        tx_to_service.try_send(HeartbeatResponse::Connected)
                                    {
                                        error!("Error enviando Connected al Core: {e}");
                                    }
                                }
                                if new_status == Status::Disconnected {
                                    warn!(
                                        "Heartbeat FSM: Estado cambiado a DESCONECTADO (Muerte de Edge)."
                                    );
                                    if let Err(e) =
                                        tx_to_service.try_send(HeartbeatResponse::Disconnected)
                                    {
                                        error!("Error enviando Disconnected al Core: {e}");
                                    }
                                }
                            }
                        }
                        Action::StopTimer => {
                            if let Err(e) = tx_to_timer.try_send(WatchdogCommand::Stop) {
                                error!("No se pudo detener el watchdog. {e}");
                            }
                        }
                        _ => {}
                    }
                }
            }
            Either::Second(Err(e)) => {
                error!("{e}");
            }
        }
    }
}

pub async fn run_fsm_heartbeat(
    tx_actions: Sender<Vec<Action, ACTION_VECTOR_CAPACITY>>,
    rx_from_heartbeat: Receiver<Event>,
) {
    info!("Iniciando FsmHeartbeat...");
    let mut state = FsmHeartbeat::new();

    loop {
        if let Ok(event) = rx_from_heartbeat.recv().await {
            let transition = state.step(event);

            match transition {
                Transition::Valid(valid) => {
                    state = valid.get_change_state();
                    if let Err(e) = tx_actions.try_send(valid.get_actions()) {
                        error!("No se pudo enviar la acción a heartbeat_handler. {e}");
                    }
                }
                Transition::Invalid(invalid) => {
                    warn!(
                        "FSM Heartbeat transición inválida: {}",
                        invalid.get_invalid()
                    );
                }
            }
        }
    }
}

pub async fn run_heartbeat_watchdog_timer(tx: Sender<Event>, rx_cmd: Receiver<WatchdogCommand>) {
    let mut current_duration = 0;
    let mut current_event = Event::Timeout;
    let mut running = false;

    loop {
        if running {
            // --- ESTADO RUNNING ---
            let timer_fut = EmbassyTimer::after(Duration::from_secs(current_duration));
            let cmd_fut = rx_cmd.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    // ¡Expiró el tiempo de forma natural!
                    warn!("¡Watchdog de latidos EXPIRÓ! Avisando a la FSM...");
                    let _ = tx.send(current_event.clone()).await;
                    running = false; // Pasamos a IDLE esperando nuevas órdenes
                }
                Either::Second(Ok(WatchdogCommand::Start {
                    interval_secs,
                    event,
                })) => {
                    info!(
                        "Watchdog REINICIADO por nuevo latido: {} seg",
                        interval_secs
                    );
                    current_duration = interval_secs;
                    current_event = event;
                }
                Either::Second(Ok(WatchdogCommand::Stop)) => {
                    info!("Watchdog DETENIDO.");
                    running = false;
                }
                Either::Second(Err(_)) => break,
            }
        } else {
            // --- ESTADO IDLE ---
            match rx_cmd.recv().await {
                Ok(WatchdogCommand::Start {
                    interval_secs,
                    event,
                }) => {
                    info!("Watchdog INICIADO: {} seg", interval_secs);
                    current_duration = interval_secs;
                    current_event = event;
                    running = true;
                }
                Ok(WatchdogCommand::Stop) => {
                    // Ignorar stops adicionales si ya estamos en IDLE
                }
                Err(_) => break,
            }
        }
    }
}
