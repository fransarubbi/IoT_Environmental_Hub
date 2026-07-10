use crate::app::heartbeat::domain::{
    Action, Event, FsmHeartbeat, HeartbeatCommand, HeartbeatResponse, State, StateForHeartbeat,
    Status, Transition, WatchdogCommand,
};
use crate::app::system_settings::domain::SystemSettings;
use async_channel::{Receiver, Sender};
use embassy_futures::select::{Either, select};
use log::{error, warn};
use std::sync::{Arc, RwLock};

pub async fn handler_heartbeat(
    tx_to_service: Sender<HeartbeatResponse>,
    tx_to_fsm: Sender<Event>,
    tx_to_timer: Sender<WatchdogCommand>,
    rx_service: Receiver<HeartbeatCommand>,
    rx_fsm: Receiver<Vec<Action>>,
    settings: Arc<RwLock<SystemSettings>>,
) {
    let mut fsm_state = StateForHeartbeat::None;

    loop {
        match select(rx_service.recv(), rx_fsm.recv()).await {
            Either::First(Ok(cmd)) => match cmd {
                HeartbeatCommand::HeartbeatIncoming => {
                    if let Err(e) = tx_to_fsm.try_send(Event::Heartbeat) {
                        error!("no se pudo enviar evento Heartbeat a la FSM heartbeat. {e}");
                    }
                }
                HeartbeatCommand::State(state) => {
                    fsm_state = state;
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
                                if let Err(e) = tx_to_timer.try_send(WatchdogCommand::Start {
                                    interval_secs: timer as u64,
                                    event: Event::Timeout,
                                }) {
                                    error!(
                                        "no se pudo enviar evento de inicio de timer al watchdog del heartbeat. {e}"
                                    );
                                }
                            }
                            _ => {}
                        },
                        Action::SendStatusConditional(old_status, new_status) => {
                            if old_status != new_status {
                                if new_status == Status::Connected {
                                    if let Err(e) =
                                        tx_to_service.try_send(HeartbeatResponse::Connected)
                                    {
                                        error!(
                                            "no se pudo enviar Connected al HeartbeatService.{e}"
                                        );
                                    }
                                }
                                if new_status == Status::Disconnected {
                                    if let Err(e) =
                                        tx_to_service.try_send(HeartbeatResponse::Disconnected)
                                    {
                                        error!(
                                            "no se pudo enviar Disconnected al HeartbeatService. {e}"
                                        );
                                    }
                                }
                            }
                        }
                        Action::StopTimer => {
                            if let Err(e) = tx_to_timer.try_send(WatchdogCommand::Stop) {
                                error!("no se pudo parar el timer watchdog del heartbeat. {e}");
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
    tx_actions: Sender<Vec<Action>>,
    rx_from_heartbeat: Receiver<Event>,
) {
    let mut state = FsmHeartbeat::new();

    loop {
        if let Ok(event) = rx_from_heartbeat.recv().await {
            let transition = state.step(event);

            match transition {
                Transition::Valid(valid) => {
                    state = valid.get_change_state();
                    if let Err(e) = tx_actions.try_send(valid.get_actions()) {
                        error!("no se pudo enviar la acción a heartbeat_handler. {e}");
                    }
                }
                Transition::Invalid(invalid) => {
                    warn!(
                        "fsm Heartbeat transición inválida: {}",
                        invalid.get_invalid()
                    );
                }
            }
        }
    }
}
