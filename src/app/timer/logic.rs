use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer as EmbassyTimer};
use log::{error, info};

pub struct TimerService {
    sender: Sender<TimerResponse>,
    receiver: Receiver<TimerCommand>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum TimerResponse {
    InitSystemReady,
    BypassReady,
    InitBalanceReady,
    HandshakeReady,
    AllBalanceReady,
}

#[derive(Clone, Debug, PartialEq)]
pub enum TimerCommand {
    InitSystemStart,
    BypassStart,
    InitBalanceStart,
    HandshakeStart,
    AllBalanceStart(u32),

    InitSystemStop,
    BypassStop,
    InitBalanceStop,
    HandshakeStop,
    AllBalanceStop,
}

enum OneShotCommand {
    Start {
        timeout_secs: u64,
        event: TimerResponse,
    },
    Cancel,
}

/// Estado para rastrear qué está haciendo cada worker One-Shot
#[derive(Clone, Debug, PartialEq)]
enum OsState {
    Idle,
    Busy(TimerResponse),
}

struct InternalWorkers;

impl InternalWorkers {
    async fn run_one_shot(
        id: u8,
        rx_cmd: Receiver<OneShotCommand>,
        tx_internal: Sender<(u8, TimerResponse)>,
    ) {
        loop {
            // --- ESTADO IDLE ---
            let (mut current_duration, mut current_event) = match rx_cmd.recv().await {
                Ok(OneShotCommand::Start {
                    timeout_secs,
                    event,
                }) => (timeout_secs, event),
                Ok(OneShotCommand::Cancel) => continue, // Ignorar cancelaciones en IDLE
                Err(_) => break,                        // Canal cerrado
            };

            // --- ESTADO RUNNING ---
            loop {
                let timer_fut = EmbassyTimer::after(Duration::from_secs(current_duration));
                let cmd_fut = rx_cmd.recv();

                match select(timer_fut, cmd_fut).await {
                    Either::First(_) => {
                        info!("timer one-shot {} expirado. Avisando al director...", id);
                        let _ = tx_internal.send((id, current_event)).await;
                        break; // Volver a IDLE
                    }
                    Either::Second(Ok(cmd)) => match cmd {
                        OneShotCommand::Cancel => {
                            info!("timer one-shot {} cancelado. Volviendo a dormir...", id);
                            break; // Volver a IDLE
                        }
                        OneShotCommand::Start {
                            timeout_secs,
                            event,
                        } => {
                            info!("timer one-shot {} reiniciado con nuevos valores.", id);
                            current_duration = timeout_secs;
                            current_event = event;
                        }
                    },
                    Either::Second(Err(_)) => return,
                }
            }
        }
    }
}

impl TimerService {
    pub fn new(sender: Sender<TimerResponse>, receiver: Receiver<TimerCommand>) -> Self {
        info!("creando TimerService...");
        Self { sender, receiver }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>, buffer_size: usize) {
        // Canales de control interno hacia los workers
        let (tx_os1, rx_os1) = bounded(buffer_size);
        let (tx_os2, rx_os2) = bounded(buffer_size);

        // Canal por donde los One-Shot avisan que terminaron para que el Director libere el espacio
        let (tx_internal_resp, rx_internal_resp) = bounded::<(u8, TimerResponse)>(buffer_size);

        // Lanzamos exactamente 2 One-Shots y 1 Periódico encapsulados
        executor
            .spawn(InternalWorkers::run_one_shot(
                1,
                rx_os1,
                tx_internal_resp.clone(),
            ))
            .detach();
        executor
            .spawn(InternalWorkers::run_one_shot(2, rx_os2, tx_internal_resp))
            .detach();

        // Control de estado de los slots
        let mut os1_state = OsState::Idle;
        let mut os2_state = OsState::Idle;

        info!("iniciando TimerService...");

        loop {
            match select(self.receiver.recv(), rx_internal_resp.recv()).await {
                Either::First(Ok(cmd)) => match cmd {
                    // --- COMANDOS ONE-SHOT (START) ---
                    TimerCommand::InitSystemStart => Self::dispatch_start(
                        60,
                        TimerResponse::InitSystemReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::BypassStart => Self::dispatch_start(
                        60,
                        TimerResponse::BypassReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::InitBalanceStart => Self::dispatch_start(
                        60,
                        TimerResponse::InitBalanceReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::HandshakeStart => Self::dispatch_start(
                        120,
                        TimerResponse::HandshakeReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::AllBalanceStart(time) => Self::dispatch_start(
                        time as u64,
                        TimerResponse::AllBalanceReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),

                    // --- COMANDOS ONE-SHOT (STOP) ---
                    TimerCommand::InitSystemStop => Self::dispatch_stop(
                        TimerResponse::InitSystemReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::BypassStop => Self::dispatch_stop(
                        TimerResponse::BypassReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::InitBalanceStop => Self::dispatch_stop(
                        TimerResponse::InitBalanceReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::HandshakeStop => Self::dispatch_stop(
                        TimerResponse::HandshakeReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                    TimerCommand::AllBalanceStop => Self::dispatch_stop(
                        TimerResponse::AllBalanceReady,
                        &mut os1_state,
                        &mut os2_state,
                        &tx_os1,
                        &tx_os2,
                    ),
                },
                Either::First(Err(_)) => break, // El canal principal se cerró

                Either::Second(Ok((id, resp))) => {
                    // Marcamos el slot como libre para que pueda recibir nuevos timers
                    if id == 1 {
                        os1_state = OsState::Idle;
                    } else if id == 2 {
                        os2_state = OsState::Idle;
                    }

                    let _ = self.sender.send(resp).await;
                }
                Either::Second(Err(_)) => break,
            }
        }
    }

    /// Busca un worker libre y le asigna la tarea
    fn dispatch_start(
        timeout: u64,
        event: TimerResponse,
        os1: &mut OsState,
        os2: &mut OsState,
        tx1: &Sender<OneShotCommand>,
        tx2: &Sender<OneShotCommand>,
    ) {
        if *os1 == OsState::Idle {
            *os1 = OsState::Busy(event.clone());
            let _ = tx1.try_send(OneShotCommand::Start {
                timeout_secs: timeout,
                event,
            });
        } else if *os2 == OsState::Idle {
            *os2 = OsState::Busy(event.clone());
            let _ = tx2.try_send(OneShotCommand::Start {
                timeout_secs: timeout,
                event,
            });
        } else {
            error!("todos los workers one-shot están ocupados.");
        }
    }

    /// Busca qué worker tiene el evento actual y le envía la orden de cancelar
    fn dispatch_stop(
        target_event: TimerResponse,
        os1: &mut OsState,
        os2: &mut OsState,
        tx1: &Sender<OneShotCommand>,
        tx2: &Sender<OneShotCommand>,
    ) {
        if *os1 == OsState::Busy(target_event.clone()) {
            let _ = tx1.try_send(OneShotCommand::Cancel);
            *os1 = OsState::Idle;
        } else if *os2 == OsState::Busy(target_event) {
            let _ = tx2.try_send(OneShotCommand::Cancel);
            *os2 = OsState::Idle;
        }
    }
}
