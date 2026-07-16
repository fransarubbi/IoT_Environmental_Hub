use std::sync::{Arc, RwLock};

use crate::{
    app::{
        fsm::domain::{ACTION_VECTOR_CAPACITY, Action, Event, FsmState, StateGeneral, Transition},
        message::domain::SerializedMessage,
        system_settings::domain::SystemSettings,
    },
    svc::http::Http,
};
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, Either3, select, select3};
use embassy_time::{Duration, Timer as EmbassyTimer};
use esp_idf_hal::reset::restart;
use heapless::{String, Vec};
use log::{error, info};

enum BypassCommand {
    Start,
    Stop,
}

pub enum FsmServiceResponse {
    CheckFirmware,
    NotifyFirmware(String<6>),
    SubscribeInitialTopic,
    LinkageProtocol,
    InitSystem,
    EntryStore(StateGeneral),
    EntryCooling,
    EntryUpdate,
    EntryNormal(StateGeneral),
    EntrySafe((StateGeneral, u32, u32)),
    EntryBypass(StateGeneral),
    EntryAlert((u32, u32)),
    EntryData((u32, u32)),
    EntryMonitor((u32, u32)),
    EntryInHandshake,
    EntryOutHandshake,
    EntryInitBalance(StateGeneral, u32),
    UpdateBalanceEpoch(u32),
    UpdateLinkageFlag,
}

pub enum FsmServiceCommand {
    NotUpdateFirmware, // Incluye error tambien
    UpdateFirmware(String<6>),
    LinkageOk,
    Handshake(String<15>),
    Safe((u32, u32)),
    Normal,
    HealthyConnection,
    CriticalConnection,
    UnavailableConnection,
    Phase((u32, String<10>, u32, u32)),
    Balance((u32, u32)),
    AnAlertWasGenerated,
    EdgeDisconnected,
    TimeoutInitSystem,
    TimeoutCooling,
    TimeoutBypass,
    TimeoutInitBalance,
    TimeoutHandshake,
    TimeoutAllBalance,
    BypassAlert(SerializedMessage),
}

#[derive(Clone, PartialEq, Eq)]
pub struct StateMemory {
    pub old: StateGeneral,
    pub new: StateGeneral,
}

#[derive(Clone)]
enum InternalEvent {
    Timeout,
}

enum PeriodicCommand {
    Start {
        interval_secs: u64,
        event: InternalEvent,
    },
    Stop,
}

pub struct FsmService<H: Http> {
    sender: Sender<FsmServiceResponse>,
    receiver: Receiver<FsmServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
    http_service: H,
}

impl<H: Http> FsmService<H> {
    pub fn new(
        sender: Sender<FsmServiceResponse>,
        receiver: Receiver<FsmServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
        http_service: H,
    ) -> Self {
        info!("creando FsmService...");
        Self {
            sender,
            receiver,
            settings,
            http_service,
        }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>)
    where
        H: 'a,
    {
        let (tx_actions, rx_actions) = bounded::<Vec<Action, ACTION_VECTOR_CAPACITY>>(10);
        let (tx_event, rx_event) = bounded::<Event>(10);
        let (tx_response, rx_response) = bounded::<FsmServiceResponse>(10);
        let (tx_command, rx_command) = bounded::<FsmServiceCommand>(10);

        executor.spawn(run_fsm(tx_actions, rx_event)).detach();
        executor
            .spawn(handler_events_and_actions(
                tx_event,
                tx_response,
                rx_actions,
                rx_command,
                Arc::clone(&self.settings),
                self.http_service,
                executor,
            ))
            .detach();

        info!("iniciando FsmService...");
        loop {
            match select(self.receiver.recv(), rx_response.recv()).await {
                Either::First(Ok(cmd)) => {
                    if let Err(e) = tx_command.try_send(cmd) {
                        error!("no se pudo enviar mensaje para generar, mensaje descartado. {e}");
                    }
                }
                Either::First(Err(_)) => {
                    error!("el canal receiver se ha cerrado.");
                    break;
                }

                Either::Second(Ok(msg)) => {
                    if let Err(e) = self.sender.try_send(msg) {
                        error!(
                            "no se pudo enviar mensaje MessageServiceResponse, mensaje descartado. {e}"
                        );
                    }
                }
                Either::Second(Err(_)) => {
                    error!("el canal interno rx_command se ha cerrado.");
                    break;
                }
            }
        }
    }
}

async fn handler_events_and_actions<'a, H: Http + 'a>(
    tx_events: Sender<Event>,
    tx_response: Sender<FsmServiceResponse>,
    rx_action: Receiver<Vec<Action, ACTION_VECTOR_CAPACITY>>,
    rx_extern_events: Receiver<FsmServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
    http_service: H,
    executor: &'a LocalExecutor<'a>,
) {
    let (tx, rx_timer) = bounded::<PeriodicCommand>(5);
    let (tx_timer, rx) = bounded::<InternalEvent>(5);
    let (tx_cmd, control_rx) = bounded::<BypassCommand>(5);
    let (tx_payload, rx_data) = bounded::<SerializedMessage>(5);

    executor.spawn(internal_timer(tx_timer, rx_timer)).detach();
    executor
        .spawn(run_bypass(http_service, control_rx, rx_data))
        .detach();

    let mut state = StateMemory {
        old: StateGeneral::InitSystem,
        new: StateGeneral::InitSystem,
    };
    let mut firmware_version = String::new();
    let mut frequency: u32 = 0;
    let mut jitter: u32 = 0;
    let mut duration: u32 = 0;
    info!("iniciando handler de FSM...");
    loop {
        match select3(rx_action.recv(), rx_extern_events.recv(), rx.recv()).await {
            Either3::First(Ok(vec_action)) => {
                for action in vec_action {
                    match action {
                        Action::OnEntryCheckFirmware => {
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::CheckFirmware)
                            {
                                error!("no se pudo enviar CheckFirmware desde el handler. {e}");
                            }
                        }
                        Action::OnEntryNotifyFirmware => {
                            if !firmware_version.is_empty() {
                                if let Err(e) = tx_response.try_send(
                                    FsmServiceResponse::NotifyFirmware(firmware_version.clone()),
                                ) {
                                    error!(
                                        "no se pudo enviar NotifyFirmware desde el handler. {e}"
                                    );
                                }
                            }
                        }
                        Action::OnEntryRestart => {
                            info!("reiniciando sistema...");
                            restart();
                        }
                        Action::OnEntryInitSystem => {
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::InitSystem) {
                                error!("no se pudo enviar InitSystem desde el handler. {e}");
                            }
                        }
                        Action::OnEntryLinkageProtocol => {
                            let linkage = settings.read().unwrap().linkage_flag();
                            if linkage {
                                if let Err(e) = tx_events.try_send(Event::EventLinkageOk) {
                                    error!(
                                        "no se pudo enviar evento EventLinkageOk desde handler. {e}"
                                    );
                                }
                            } else {
                                if let Err(e) =
                                    tx_response.try_send(FsmServiceResponse::SubscribeInitialTopic)
                                {
                                    error!(
                                        "no se pudo enviar SubscribeInitialTopic desde el handler. {e}"
                                    );
                                }
                                if let Err(e) = tx.try_send(PeriodicCommand::Start {
                                    interval_secs: 10,
                                    event: InternalEvent::Timeout,
                                }) {
                                    error!("no se pudo enviar PeriodicCommand desde handler. {e}");
                                }
                            }
                        }
                        Action::OnEntryStore => {
                            state.old = state.new;
                            state.new = StateGeneral::Store;
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryStore(state.old.clone()))
                            {
                                error!("no se pudo enviar EntryStore desde el handler. {e}");
                            }
                        }
                        Action::OnEntryCooling => {
                            state.old = state.new;
                            state.new = StateGeneral::Cooling;
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::EntryCooling) {
                                error!("no se pudo enviar EntryCooling desde el handler. {e}");
                            }
                        }
                        Action::OnEntryUpdateScore => {
                            state.old = state.new;
                            state.new = StateGeneral::UpdateScore;
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::EntryUpdate) {
                                error!("no se pudo enviar EntryUpdate desde el handler. {e}");
                            }
                        }
                        Action::OnEntryNormal => {
                            state.old = state.new;
                            state.new = StateGeneral::Normal;
                            if state.old == StateGeneral::Bypass {
                                if let Err(e) = tx_cmd.try_send(BypassCommand::Stop) {
                                    error!("no se pudo enviar BypassCommand desde el handler. {e}");
                                }
                            }
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryNormal(state.old.clone()))
                            {
                                error!("no se pudo enviar EntryNormal desde el handler. {e}");
                            }
                        }
                        Action::OnEntrySafe => {
                            state.old = state.new;
                            state.new = StateGeneral::Safe { frequency, jitter };
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::EntrySafe((
                                state.old.clone(),
                                frequency,
                                jitter,
                            ))) {
                                error!("no se pudo enviar EntrySafe desde el handler. {e}");
                            }
                        }
                        Action::OnEntryBypass => {
                            state.old = state.new;
                            state.new = StateGeneral::Bypass;
                            if let Err(e) = tx_cmd.try_send(BypassCommand::Start) {
                                error!("no se pudo enviar BypassCommand desde el handler. {e}");
                            }
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryBypass(state.old.clone()))
                            {
                                error!("no se pudo enviar EntryBypass desde el handler. {e}");
                            }
                        }
                        Action::OnEntryAlert => {
                            state.old = state.new;
                            state.new = StateGeneral::Alert { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryAlert((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryAlert desde el handler. {e}");
                            }
                        }
                        Action::OnEntryData => {
                            state.old = state.new;
                            state.new = StateGeneral::Data { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryData((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryData desde el handler. {e}");
                            }
                        }
                        Action::OnEntryMonitor => {
                            state.old = state.new;
                            state.new = StateGeneral::Monitor { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryMonitor((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryMonitor desde el handler. {e}");
                            }
                        }
                        Action::OnEntryInHandshake => {
                            state.old = state.new;
                            state.new = StateGeneral::InHandshake;
                            if let Err(e) =
                                tx_response.try_send(FsmServiceResponse::EntryInHandshake)
                            {
                                error!("no se pudo enviar EntryInHandshake desde el handler. {e}");
                            }
                        }
                        Action::OnEntryOutHandshake => {
                            state.old = state.new;
                            state.new = StateGeneral::OutHandshake;
                            if let Err(e) =
                                tx_response.try_send(FsmServiceResponse::EntryOutHandshake)
                            {
                                error!("no se pudo enviar EntryOutHandshake desde el handler. {e}");
                            }
                        }
                        Action::OnEntryInitBalance => {
                            if state.old == StateGeneral::Bypass {
                                if let Err(e) = tx_cmd.try_send(BypassCommand::Stop) {
                                    error!("no se pudo enviar BypassCommand desde el handler. {e}");
                                }
                            }
                            if let Err(e) = tx_response.try_send(
                                FsmServiceResponse::EntryInitBalance(state.old.clone(), duration),
                            ) {
                                error!("no se pudo enviar EntryInitBalance desde el handler. {e}");
                            }
                        }
                    }
                }
            }
            Either3::First(Err(e)) => {
                error!("el canal rx_action se ha cerrado. {e}");
                break;
            }

            Either3::Second(Ok(event)) => match event {
                FsmServiceCommand::NotUpdateFirmware => {
                    if let Err(e) = tx_events.try_send(Event::EventNotUpdate) {
                        error!("no se pudo enviar evento EventNotUpdate desde handler. {e}");
                    }
                }
                FsmServiceCommand::UpdateFirmware(version) => {
                    firmware_version = version;
                    if let Err(e) = tx_events.try_send(Event::EventUpdateSuccessful) {
                        error!("no se pudo enviar evento EventUpdateSuccessful desde handler. {e}");
                    }
                }
                FsmServiceCommand::LinkageOk => {
                    if let Err(e) = tx.try_send(PeriodicCommand::Stop) {
                        error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                    }
                    if let Err(e) = tx_response.try_send(FsmServiceResponse::UpdateLinkageFlag) {
                        error!("no se pudo enviar UpdateLinkageFlag desde handler. {e}");
                    }
                    if let Err(e) = tx_events.try_send(Event::EventLinkageOk) {
                        error!("no se pudo enviar evento EventLinkageOk desde handler. {e}");
                    }
                }
                FsmServiceCommand::Handshake(handshake) => {
                    if handshake == "in_handshake" {
                        if let Err(e) = tx_events.try_send(Event::EventToInHandshake) {
                            error!(
                                "no se pudo enviar evento EventToInHandshake desde handler. {e}"
                            );
                        }
                    } else if handshake == "out_handshake" {
                        if let Err(e) = tx_events.try_send(Event::EventToOutHandshake) {
                            error!(
                                "no se pudo enviar evento EventToOutHandshake desde handler. {e}"
                            );
                        }
                    }
                }
                FsmServiceCommand::Safe(safe) => {
                    frequency = safe.0;
                    jitter = safe.1;
                    if let Err(e) = tx_events.try_send(Event::EventToSafe) {
                        error!("no se pudo enviar evento EventToSafe desde handler. {e}");
                    }
                }
                FsmServiceCommand::Normal => {
                    if let Err(e) = tx_events.try_send(Event::EventToNormal) {
                        error!("no se pudo enviar evento EventToNormal desde handler. {e}");
                    }
                }
                FsmServiceCommand::HealthyConnection => {
                    if let Err(e) = tx_events.try_send(Event::EventGoodScore) {
                        error!("no se pudo enviar evento EventGoodScore desde handler. {e}");
                    }
                }
                FsmServiceCommand::CriticalConnection => {
                    if let Err(e) = tx_events.try_send(Event::EventLowScore) {
                        error!("no se pudo enviar evento EventLowScore desde handler. {e}");
                    }
                }
                FsmServiceCommand::UnavailableConnection => {
                    if let Err(e) = tx_events.try_send(Event::EventBadScore) {
                        error!("no se pudo enviar evento EventBadScore desde handler. {e}");
                    }
                }
                FsmServiceCommand::Phase(phase) => {
                    let epoch = settings.read().unwrap().balance_epoch();
                    if phase.0 == epoch {
                        if phase.1 == "alert" {
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToAlert) {
                                error!("no se pudo enviar evento EventToAlert desde handler. {e}");
                            }
                        } else if phase.1 == "data" {
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToData) {
                                error!("no se pudo enviar evento EventToData desde handler. {e}");
                            }
                        } else if phase.1 == "monitor" {
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToMonitor) {
                                error!(
                                    "no se pudo enviar evento EventToMonitor desde handler. {e}"
                                );
                            }
                        }
                    } else if phase.0 > epoch {
                        // DECIRLE A SETTINGS QUE ACTUALICE?
                        if let Err(e) = tx_events.try_send(Event::EventNewerEpoch) {
                            error!("no se pudo enviar evento EventNewerEpoch desde handler. {e}");
                        }
                    }
                }
                FsmServiceCommand::Balance(balance) => {
                    let epoch = settings.read().unwrap().balance_epoch();
                    if balance.0 > epoch {
                        if let Err(e) = tx_events.try_send(Event::EventInitBalance) {
                            error!("no se pudo enviar evento EventInitBalance desde handler. {e}");
                        }
                        if let Err(e) =
                            tx_response.try_send(FsmServiceResponse::UpdateBalanceEpoch(balance.0))
                        {
                            error!("no se pudo enviar UpdateBalanceEpoch desde handler. {e}");
                        }
                        duration = balance.1;
                    }
                }
                FsmServiceCommand::AnAlertWasGenerated => {
                    if let Err(e) = tx_events.try_send(Event::EventAlertGenerated) {
                        error!("no se pudo enviar evento EventAlertGenerated desde handler. {e}");
                    }
                }
                FsmServiceCommand::EdgeDisconnected => {
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutInitSystem => {
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutCooling => {
                    if let Err(e) = tx_events.try_send(Event::EventTimeoutCooling) {
                        error!("no se pudo enviar evento EventTimeoutCooling desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutBypass => {
                    if let Err(e) = tx_events.try_send(Event::EventTimeoutBypass) {
                        error!("no se pudo enviar evento EventTimeoutBypass desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutInitBalance => {
                    if let Err(e) = tx_events.try_send(Event::EventToSafe) {
                        error!("no se pudo enviar evento EventToSafe desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutHandshake => {
                    if let Err(e) = tx_events.try_send(Event::EventToSafe) {
                        error!("no se pudo enviar evento EventToSafe desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutAllBalance => {
                    if let Err(e) = tx_events.try_send(Event::EventToNormal) {
                        error!("no se pudo enviar evento EventToNormal desde handler. {e}");
                    }
                }
                FsmServiceCommand::BypassAlert(msg) => {
                    if let Err(e) = tx_payload.try_send(msg) {
                        error!("no se pudo enviar Payload desde el handler. {e}");
                    }
                }
            },
            Either3::Second(Err(e)) => {
                error!("el canal rx_command se ha cerrado. {e}");
                break;
            }

            Either3::Third(Ok(msg)) => match msg {
                InternalEvent::Timeout => {
                    if let Err(e) = tx_response.try_send(FsmServiceResponse::LinkageProtocol) {
                        error!("no se pudo enviar LinkageProtocol desde el handler. {e}");
                    }
                }
            },
            Either3::Third(Err(e)) => {
                error!("{e}");
            }
        }
    }
}

/// Tarea asíncrona que ejecuta la lógica pura de la Máquina de Estados.
///
/// Mantiene el estado persistente (`FsmState`) y avanza tras recibir eventos.
///
/// * `tx_actions`: Canal para emitir los efectos secundarios que deben ejecutarse.
/// * `rx_event`: Canal de entrada de eventos (triggers).
async fn run_fsm(
    tx_actions: Sender<Vec<Action, ACTION_VECTOR_CAPACITY>>,
    rx_event: Receiver<Event>,
) {
    info!("iniciando run_fsm...");
    let mut state = FsmState::new();

    match state.step(Event::EventStart) {
        Transition::Valid(t) => {
            state = t.change_state();
        }
        Transition::Invalid(t) => error!("fsm transición inválida {}", t.get_invalid()),
    }

    loop {
        if let Ok(event) = rx_event.recv().await {
            match state.step(event) {
                Transition::Valid(t) => {
                    state = t.change_state();
                    if let Err(e) = tx_actions.try_send(t.actions()) {
                        error!("no se pudo enviar vector de acciones desde run_fsm. {e}");
                    }
                }
                Transition::Invalid(t) => error!("fsm transición inválida {}", t.get_invalid()),
            }
        }
    }
}

async fn internal_timer(tx: Sender<InternalEvent>, rx_cmd: Receiver<PeriodicCommand>) {
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
                    let _ = tx.send(current_event.clone()).await;
                }
                Either::Second(Ok(cmd)) => match cmd {
                    PeriodicCommand::Stop => {
                        info!("timer heartbeat detenido.");
                        break;
                    }
                    PeriodicCommand::Start {
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

/// Tarea Worker asíncrona para el modo Bypass.
/// Utiliza canales (`Receiver`) en lugar de `xQueueReceive` y `xTaskNotifyWait`.
async fn run_bypass<S: Http>(
    mut service: S,
    control_rx: Receiver<BypassCommand>,
    rx_data: Receiver<SerializedMessage>,
) {
    info!("iniciando Bypass...");
    loop {
        // Espera pasiva y asíncrona hasta recibir start
        if let Ok(BypassCommand::Start) = control_rx.recv().await {
            info!("modo Bypass activo.");
            loop {
                match select(control_rx.recv(), rx_data.recv()).await {
                    Either::First(Ok(msg)) => match msg {
                        BypassCommand::Stop => {
                            info!("modo Bypass detenido.");
                            break;
                        }
                        _ => {}
                    },
                    Either::First(Err(e)) => {
                        error!("{e}");
                    }

                    Either::Second(Ok(msg)) => {
                        if let Err(e) = service.send_payload(&msg.get_payload()) {
                            error!("{e}");
                        }
                    }
                    Either::Second(Err(e)) => {
                        error!("{e}");
                    }
                }
            }
        }
    }
}
