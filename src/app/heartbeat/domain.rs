use crate::app::heartbeat::logic::{handler_heartbeat, run_fsm_heartbeat};
use crate::app::system_settings::domain::SystemSettings;
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use log::{error, info};
use std::sync::{Arc, RwLock};

pub enum HeartbeatResponse {
    Connected,
    Disconnected,
}

pub enum HeartbeatCommand {
    HeartbeatIncoming,
    State(StateForHeartbeat),
}

pub enum StateForHeartbeat {
    Normal,
    Balance,
    Safe,
    None,
}

pub enum WatchdogCommand {
    Start { interval_secs: u64, event: Event },
    Stop,
}

pub struct HeartbeatService {
    sender: Sender<HeartbeatResponse>,
    receiver: Receiver<HeartbeatCommand>,
    settings: Arc<RwLock<SystemSettings>>,
}

impl HeartbeatService {
    pub fn new(
        sender: Sender<HeartbeatResponse>,
        receiver: Receiver<HeartbeatCommand>,
        settings: Arc<RwLock<SystemSettings>>,
    ) -> Self {
        Self {
            sender,
            receiver,
            settings,
        }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>) {
        let (tx_to_core, rx) = bounded::<HeartbeatResponse>(10);
        let (tx_to_fsm, rx_from_heartbeat) = bounded::<Event>(10);
        let (tx_to_timer, rx_watchdog_heartbeat) = bounded::<WatchdogCommand>(10);
        let (tx_msg, rx_from_server) = bounded::<HeartbeatCommand>(10);
        let (tx_actions, rx_fsm) = bounded::<Vec<Action>>(10);

        let heartbeat_tx_to_fsm = tx_to_fsm.clone();
        executor
            .spawn(handler_heartbeat(
                tx_to_core,
                heartbeat_tx_to_fsm,
                tx_to_timer,
                rx_from_server,
                rx_fsm,
                Arc::clone(&self.settings),
            ))
            .detach();

        executor
            .spawn(run_fsm_heartbeat(tx_actions, rx_from_heartbeat))
            .detach();

        let watchdog_tx_to_fsm = tx_to_fsm.clone();
        executor
            .spawn(run_heartbeat_watchdog_timer(
                watchdog_tx_to_fsm,
                rx_watchdog_heartbeat,
            ))
            .detach();

        loop {
            match select(self.receiver.recv(), rx.recv()).await {
                Either::First(Ok(cmd)) => {
                    if let Err(e) = tx_msg.try_send(cmd) {
                        error!("no se pudo enviar mensaje de Heartbeat proveniente del edge. {e}");
                    }
                }
                Either::First(Err(e)) => {
                    error!("{e}")
                }

                Either::Second(Ok(cmd)) => {
                    if let Err(e) = self.sender.try_send(cmd) {
                        error!(
                            "no se pudo enviar el HeartbeatResponse proveniente de heartbeat_handler. {e}"
                        );
                    }
                }
                Either::Second(Err(e)) => {
                    error!("{e}")
                }
            }
        }
    }
}

/// Estructura principal que mantiene el estado de la FSM del Heartbeat.
#[derive(Debug, Clone)]
pub struct FsmHeartbeat {
    /// Estado interno actual de la lógica de heartbeat.
    state: State,
    /// Estado de conexión anterior (utilizado para detectar cambios y notificar).
    old_status: Status,
    /// Estado de conexión actual calculado.
    status: Status,
}

/// Estados internos de la máquina de estados.
/// Determinan la salud de la recepción de heartbeats.
#[derive(Debug, Clone, PartialEq)]
pub enum State {
    /// Esperando el primer heartbeat o reiniciando el ciclo.
    StartingWait,
    /// El servidor está activo y enviando heartbeats correctamente.
    ItsAlive,
    /// El servidor ha sido declarado muerto/desconectado tras múltiples fallos.
    DeadServer,
    /// Estado de transición/advertencia: El temporizador venció, pero aún no se declara muerte total.
    NotHeartbeatYet,
}

/// Estado de la conexión
#[derive(Debug, Clone, PartialEq)]
pub enum Status {
    Disconnected,
    Connected,
}

/// Eventos que alimentan a la FSM y provocan transiciones.
#[derive(Clone)]
pub enum Event {
    /// Se recibió un mensaje de Heartbeat desde el Edge.
    Heartbeat,
    /// El temporizador de vigilancia (Watchdog) expiró.
    Timeout,
    /// Comando interno para detener el temporizador.
    StopTimer,
}

/// Acciones o Efectos Secundarios (Side Effects).
/// Instrucciones que la FSM genera para que el Runtime las ejecute.
#[derive(Debug, Clone, PartialEq)]
pub enum Action {
    /// Solicita enviar una notificación de cambio de estado (ej. de Conectado a Desconectado)
    /// solo si los estados difieren.
    SendStatusConditional(Status, Status),
    /// Indica que se ha entrado a un nuevo estado (útil para iniciar timers asociados a ese estado).
    OnEntry(State),
    /// No se requiere ninguna acción.
    Nothing,
    /// Solicita detener el temporizador.
    StopTimer,
}

/// Resultado de una transición válida.
/// Contiene el nuevo estado de la FSM y las acciones a ejecutar.
pub struct TransitionValid {
    change_state: FsmHeartbeat,
    action: Vec<Action>,
}

impl TransitionValid {
    pub fn get_change_state(&self) -> FsmHeartbeat {
        self.change_state.clone()
    }
    pub fn get_actions(&self) -> Vec<Action> {
        self.action.clone()
    }
}

/// Resultado de una transición inválida (error de lógica o evento inesperado).
pub struct TransitionInvalid {
    invalid: String,
}

impl TransitionInvalid {
    pub fn get_invalid(&self) -> &str {
        &self.invalid
    }
}

pub enum Transition {
    Valid(TransitionValid),
    Invalid(TransitionInvalid),
}

impl FsmHeartbeat {
    /// Crea una nueva instancia de la FSM en estado inicial desconectado.
    pub fn new() -> Self {
        Self {
            state: State::StartingWait,
            old_status: Status::Disconnected,
            status: Status::Disconnected,
        }
    }

    /// Lógica interna de despacho de eventos.
    /// Mapea el par `(Estado Actual, Evento)` a una función de transición específica.
    pub fn step_inner(&self, event: Event) -> Transition {
        match (&self.state, event) {
            (State::StartingWait, Event::Heartbeat) => {
                let next_fsm = self.clone();
                state_starting_wait_event_heartbeat(next_fsm)
            }
            (State::StartingWait, Event::Timeout) => {
                let next_fsm = self.clone();
                state_starting_wait_event_timeout(next_fsm)
            }
            (State::ItsAlive, Event::Heartbeat) => {
                let next_fsm = self.clone();
                state_its_alive_event_heartbeat(next_fsm)
            }
            (State::ItsAlive, Event::Timeout) => {
                let next_fsm = self.clone();
                state_its_alive_event_timeout(next_fsm)
            }
            (State::NotHeartbeatYet, Event::Heartbeat) => {
                let next_fsm = self.clone();
                state_not_heartbeat_yet_event_heartbeat(next_fsm)
            }
            (State::NotHeartbeatYet, Event::Timeout) => {
                let next_fsm = self.clone();
                state_not_heartbeat_yet_event_timeout(next_fsm)
            }
            (State::DeadServer, Event::Heartbeat) => {
                let next_fsm = self.clone();
                state_dead_server_event_heartbeat(next_fsm)
            }
            _ => invalid(),
        }
    }

    /// Función principal de transición (API Pública).
    ///
    /// Ejecuta la transición interna y calcula automáticamente las acciones `OnEntry`
    /// si el estado ha cambiado.
    pub fn step(&self, event: Event) -> Transition {
        let transition = self.step_inner(event);

        match transition {
            Transition::Valid(mut valid) => {
                let entry_action = compute_on_entry(self, &valid.change_state);
                if entry_action != Action::Nothing {
                    valid.action.push(entry_action);
                }
                Transition::Valid(valid)
            }
            invalid => invalid,
        }
    }
}

// --- Funciones de Transición Específicas ---

/// Transición: StartingWait + Heartbeat -> ItsAlive.
/// Se establece conexión exitosa.
fn state_starting_wait_event_heartbeat(mut next_fsm: FsmHeartbeat) -> Transition {
    let old_status = next_fsm.old_status.clone();
    next_fsm.state = State::ItsAlive;
    next_fsm.old_status = Status::Connected;
    next_fsm.status = Status::Connected;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![
            Action::SendStatusConditional(old_status, next_fsm.status), // (Disconnected, Connected)
            Action::StopTimer,
        ],
    };
    Transition::Valid(valid)
}

/// Transición: StartingWait + Timeout -> NotHeartbeatYet.
/// Primer fallo al esperar.
fn state_starting_wait_event_timeout(mut next_fsm: FsmHeartbeat) -> Transition {
    next_fsm.state = State::NotHeartbeatYet;
    let valid = TransitionValid {
        change_state: next_fsm,
        action: vec![],
    };
    Transition::Valid(valid)
}

/// Transición: ItsAlive + Heartbeat -> StartingWait.
/// Reinicia el ciclo de espera tras recibir un latido válido.
fn state_its_alive_event_heartbeat(mut next_fsm: FsmHeartbeat) -> Transition {
    next_fsm.state = State::StartingWait;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![Action::StopTimer],
    };
    Transition::Valid(valid)
}

/// Transición: ItsAlive + Timeout -> NotHeartbeatYet.
/// El servidor estaba vivo, pero se agotó el tiempo esperando el siguiente latido.
fn state_its_alive_event_timeout(mut next_fsm: FsmHeartbeat) -> Transition {
    next_fsm.state = State::NotHeartbeatYet;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![],
    };
    Transition::Valid(valid)
}

/// Transición: NotHeartbeatYet + Heartbeat -> StartingWait.
/// Recuperación exitosa antes de declarar muerte total.
fn state_not_heartbeat_yet_event_heartbeat(mut next_fsm: FsmHeartbeat) -> Transition {
    next_fsm.state = State::StartingWait;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![Action::StopTimer],
    };
    Transition::Valid(valid)
}

/// Transición: NotHeartbeatYet + Timeout -> DeadServer.
/// Fallo definitivo. Se marca el servidor como desconectado.
fn state_not_heartbeat_yet_event_timeout(mut next_fsm: FsmHeartbeat) -> Transition {
    let old_status = next_fsm.old_status.clone();
    next_fsm.state = State::DeadServer;
    next_fsm.old_status = Status::Disconnected;
    next_fsm.status = Status::Disconnected;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![Action::SendStatusConditional(old_status, next_fsm.status)], // (Connected, Disconnected)
    };
    Transition::Valid(valid)
}

/// Transición: DeadServer + Heartbeat -> StartingWait.
/// El servidor revivió tras haber estado muerto.
fn state_dead_server_event_heartbeat(mut next_fsm: FsmHeartbeat) -> Transition {
    next_fsm.state = State::StartingWait;
    let valid = TransitionValid {
        change_state: next_fsm.clone(),
        action: vec![],
    };
    Transition::Valid(valid)
}

/// Helper para generar una transición inválida genérica.
fn invalid() -> Transition {
    let invalid = TransitionInvalid {
        invalid: "Invalid state".to_string(),
    };
    Transition::Invalid(invalid)
}

/// Calcula la acción `OnEntry` comparando el estado anterior y el nuevo.
/// Si el estado cambia, genera la acción para inicializar los recursos del nuevo estado.
fn compute_on_entry(old: &FsmHeartbeat, new: &FsmHeartbeat) -> Action {
    if old.state != new.state {
        let state = new.state.clone();
        return Action::OnEntry(state);
    }
    Action::Nothing
}

/// Tarea asíncrona del Temporizador de Vigilancia (Watchdog).
///
/// Gestiona la espera. Si no recibe un comando `StopTimer` o una reinicialización antes
/// de que expire `duration`, envía un evento `Event::Timeout` a la FSM para indicar fallo.
pub async fn run_heartbeat_watchdog_timer(tx: Sender<Event>, rx_cmd: Receiver<WatchdogCommand>) {
    loop {
        // --- ESTADO IDLE ---
        let (mut interval, mut current_event) = match rx_cmd.recv().await {
            Ok(WatchdogCommand::Start {
                interval_secs,
                event,
            }) => (interval_secs, event),
            Ok(WatchdogCommand::Stop) => continue,
            Err(_) => break,
        };

        // --- ESTADO RUNNING ---
        loop {
            let timer_fut = EmbassyTimer::after(Duration::from_secs(interval));
            let cmd_fut = rx_cmd.recv();

            match select(timer_fut, cmd_fut).await {
                Either::First(_) => {
                    let _ = tx.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    WatchdogCommand::Stop => {
                        info!("timer heartbeat detenido.");
                        break; // Volver a IDLE
                    }
                    WatchdogCommand::Start {
                        interval_secs,
                        event,
                    } => {
                        info!("timer heartbeat actualizado sin detener el hilo.");
                        interval = interval_secs;
                        current_event = event;
                    }
                },
                Either::Second(Err(_)) => return,
            }
        }
    }
}
