use std::sync::{Arc, RwLock};

use crate::{
    app::{
        fsm::domain::{ACTION_VECTOR_CAPACITY, Action, Event, FsmState, StateGeneral, Transition},
        message::domain::MessageFromEdge,
        pool::pool::CORE_DATA_POOL,
        system_settings::domain::SystemSettings,
    },
    svc::http::Http,
};
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, Either4, select, select4};
use embassy_time::{Duration, Timer as EmbassyTimer};
use heapless::{String, Vec};
use log::{error, info};

enum BypassCommand {
    Start,
    Stop,
}

pub enum FsmServiceResponse {
    CheckFirmware,
    NotifyFirmware,
    SubscribeInitialTopic,
    LinkageProtocol,
    InitSystem,
    EntryStore(StateGeneral),
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
    SendHandshake((u32, String<15>)),
    GenerateHubState(String<20>),
}

pub enum FsmServiceCommand {
    NotUpdateFirmware, // Incluye error tambien
    UpdateFirmware,
    LinkageOk,
    Handshake(String<15>),
    Phase((u32, String<10>, u32, u32)),
    AnAlertWasGenerated,
    EdgeDisconnected,
    TimeoutInitSystem,
    TimeoutCooling,
    TimeoutBypass,
    TimeoutInitBalance,
    TimeoutHandshake,
    TimeoutAllBalance,
    BypassAlert(usize),
    NetworkDropped,
    MqttConnected,
    EdgeState(usize),
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

#[derive(PartialEq, Eq)]
enum MqttStatus {
    Connected,
    Disconnected,
}

pub struct FsmService<H: Http> {
    sender: Sender<FsmServiceResponse>,
    receiver: Receiver<FsmServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
    http_service: H,
    free_pool_index_tx: Sender<usize>,
}

impl<H: Http> FsmService<H> {
    pub fn new(
        sender: Sender<FsmServiceResponse>,
        receiver: Receiver<FsmServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
        http_service: H,
        free_pool_index_tx: Sender<usize>,
    ) -> Self {
        info!("creando FsmService...");
        Self {
            sender,
            receiver,
            settings,
            http_service,
            free_pool_index_tx,
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
                self.free_pool_index_tx.clone(),
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
    free_pool_index_tx: Sender<usize>,
) {
    let (tx, rx_timer) = bounded::<PeriodicCommand>(3);
    let (tx_timer, rx) = bounded::<InternalEvent>(3);
    let (tx_cmd, control_rx) = bounded::<BypassCommand>(3);
    let (tx_payload, rx_data) = bounded::<usize>(3);

    let (tx_to_msg_timer, rx_msg_timer) = bounded::<PeriodicCommand>(3);
    let (tx_msg_timer, rx_from_msg) = bounded::<InternalEvent>(3);

    executor.spawn(internal_timer(tx_timer, rx_timer)).detach();
    executor
        .spawn(internal_timer(tx_msg_timer, rx_msg_timer))
        .detach();
    executor
        .spawn(run_bypass(
            http_service,
            control_rx,
            rx_data,
            free_pool_index_tx.clone(),
        ))
        .detach();

    let mut state = StateMemory {
        old: StateGeneral::InitSystem,
        new: StateGeneral::InitSystem,
    };
    let mut handshake_flag = String::new();
    let mut frequency: u32 = 0;
    let mut jitter: u32 = 0;
    let mut duration: u32 = 0;
    let mut mqtt_status = MqttStatus::Disconnected;
    info!("iniciando handler de FSM...");
    loop {
        match select4(
            rx_action.recv(),
            rx_extern_events.recv(),
            rx.recv(),
            rx_from_msg.recv(),
        )
        .await
        {
            Either4::First(Ok(vec_action)) => {
                for action in vec_action {
                    match action {
                        Action::OnEntryCheckFirmware => {
                            info!("FSM. Entrando a estado CheckFirmware.");
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::CheckFirmware)
                            {
                                error!("no se pudo enviar CheckFirmware desde el handler. {e}");
                            }
                        }
                        Action::OnEntryNotifyFirmware => {
                            info!("FSM. Entrando a estado NotifyHardware.");
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::NotifyFirmware)
                            {
                                error!("no se pudo enviar NotifyFirmware desde el handler. {e}");
                            }
                        }
                        Action::OnEntryInitSystem => {
                            info!("FSM. Entrando a estado InitSystem.");
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::InitSystem) {
                                error!("no se pudo enviar InitSystem desde el handler. {e}");
                            }
                        }
                        Action::OnEntryLinkageProtocol => {
                            info!("FSM. Entrando a estado LinkageProtocol.");
                            let linkage = settings.read().unwrap().linkage_flag();
                            if linkage {
                                info!("FSM. El dispositivo ya estaba linkeado a un Edge.");
                                if let Err(e) = tx_events.try_send(Event::EventLinkageOk) {
                                    error!(
                                        "no se pudo enviar evento EventLinkageOk desde handler. {e}"
                                    );
                                }
                            } else {
                                info!("FSM. El dispositivo no esta linkeado.");
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
                            info!("FSM. Entrando a estado Store.");
                            state.old = state.new;
                            state.new = StateGeneral::Store;
                            if state.old == StateGeneral::Bypass {
                                if let Err(e) = tx_cmd.try_send(BypassCommand::Stop) {
                                    error!("no se pudo enviar BypassCommand desde el handler. {e}");
                                }
                            }
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryStore(state.old.clone()))
                            {
                                error!("no se pudo enviar EntryStore desde el handler. {e}");
                            }
                            if let Err(e) = tx_to_msg_timer.try_send(PeriodicCommand::Stop) {
                                error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                            }
                        }
                        Action::OnEntryNormal => {
                            info!("FSM. Entrando a estado Normal.");
                            state.old = state.new;
                            state.new = StateGeneral::Normal;
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryNormal(state.old.clone()))
                            {
                                error!("no se pudo enviar EntryNormal desde el handler. {e}");
                            }
                            if let Err(e) = tx_to_msg_timer.try_send(PeriodicCommand::Start {
                                interval_secs: 15,
                                event: InternalEvent::Timeout,
                            }) {
                                error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                            }
                        }
                        Action::OnEntrySafe => {
                            info!("FSM. Entrando a estado Safe.");
                            state.old = state.new;
                            state.new = StateGeneral::Safe { frequency, jitter };
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::EntrySafe((
                                state.old.clone(),
                                frequency,
                                jitter,
                            ))) {
                                error!("no se pudo enviar EntrySafe desde el handler. {e}");
                            }
                            if let Err(e) = tx_to_msg_timer.try_send(PeriodicCommand::Start {
                                interval_secs: 15,
                                event: InternalEvent::Timeout,
                            }) {
                                error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                            }
                        }
                        Action::OnEntryBypass => {
                            info!("FSM. Entrando a estado Bypass.");
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
                            if let Err(e) = tx_to_msg_timer.try_send(PeriodicCommand::Stop) {
                                error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                            }
                        }
                        Action::OnEntryAlert => {
                            info!("FSM. Entrando a estado Alert.");
                            state.old = state.new;
                            state.new = StateGeneral::Alert { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryAlert((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryAlert desde el handler. {e}");
                            }
                        }
                        Action::OnEntryData => {
                            info!("FSM. Entrando a estado Data.");
                            state.old = state.new;
                            state.new = StateGeneral::Data { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryData((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryData desde el handler. {e}");
                            }
                        }
                        Action::OnEntryMonitor => {
                            info!("FSM. Entrando a estado Monitor.");
                            state.old = state.new;
                            state.new = StateGeneral::Monitor { frequency, jitter };
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::EntryMonitor((frequency, jitter)))
                            {
                                error!("no se pudo enviar EntryMonitor desde el handler. {e}");
                            }
                        }
                        Action::OnEntryInHandshake => {
                            info!("FSM. Entrando a estado InHandshake.");
                            state.old = state.new;
                            state.new = StateGeneral::InHandshake;
                            let epoch = settings.read().unwrap().balance_epoch();
                            if let Err(e) =
                                tx_response.try_send(FsmServiceResponse::EntryInHandshake)
                            {
                                error!("no se pudo enviar EntryInHandshake desde el handler. {e}");
                            }
                            let flag = heapless::String::<15>::try_from(
                                handshake_flag.to_string().as_str(),
                            )
                            .unwrap_or_default();
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::SendHandshake((epoch, flag)))
                            {
                                error!("no se pudo enviar SendHandshake desde el handler. {e}");
                            }
                        }
                        Action::OnEntryOutHandshake => {
                            info!("FSM. Entrando a estado OutHandshake.");
                            state.old = state.new;
                            state.new = StateGeneral::OutHandshake;
                            let epoch = settings.read().unwrap().balance_epoch();
                            if let Err(e) =
                                tx_response.try_send(FsmServiceResponse::EntryOutHandshake)
                            {
                                error!("no se pudo enviar EntryOutHandshake desde el handler. {e}");
                            }
                            let flag = heapless::String::<15>::try_from(
                                handshake_flag.to_string().as_str(),
                            )
                            .unwrap_or_default();
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::SendHandshake((epoch, flag)))
                            {
                                error!("no se pudo enviar SendHandshake desde el handler. {e}");
                            }
                        }
                        Action::OnEntryInitBalance => {
                            info!("FSM. Entrando a estado InitBalance.");
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
                            if let Err(e) = tx_to_msg_timer.try_send(PeriodicCommand::Start {
                                interval_secs: 15,
                                event: InternalEvent::Timeout,
                            }) {
                                error!("no se pudo enviar PeriodicCommand desde el handler. {e}");
                            }
                        }
                    }
                }
            }
            Either4::First(Err(e)) => {
                error!("el canal rx_action se ha cerrado. {e}");
                break;
            }

            Either4::Second(Ok(event)) => match event {
                FsmServiceCommand::NotUpdateFirmware => {
                    info!("FSM. Se recibió comando NotUpdateFirmware.");
                    if let Err(e) = tx_events.try_send(Event::EventNotUpdate) {
                        error!("no se pudo enviar evento EventNotUpdate desde handler. {e}");
                    }
                }
                FsmServiceCommand::UpdateFirmware => {
                    info!("FSM. Se recibió comando UpdateFirmware.");
                    if state.new != StateGeneral::InitSystem {
                        if let Err(e) = tx_response.try_send(FsmServiceResponse::NotifyFirmware) {
                            error!("no se pudo enviar NotifyFirmware desde el handler. {e}");
                        }
                    } else {
                        if let Err(e) = tx_events.try_send(Event::EventUpdateSuccessful) {
                            error!(
                                "no se pudo enviar evento EventUpdateSuccessful desde handler. {e}"
                            );
                        }
                    }
                }
                FsmServiceCommand::LinkageOk => {
                    info!("FSM. Se recibió comando LinkageOk.");
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
                    if handshake == "in" {
                        info!("FSM. Se recibió comando Handshake de inicio.");
                        handshake_flag = handshake;
                        if state.new == StateGeneral::InHandshake {
                            let flag = heapless::String::<15>::try_from(
                                handshake_flag.to_string().as_str(),
                            )
                            .unwrap_or_default();
                            let epoch = settings.read().unwrap().balance_epoch();
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::SendHandshake((epoch, flag)))
                            {
                                error!("no se pudo enviar SendHandshake desde el handler. {e}");
                            }
                        } else {
                            if let Err(e) = tx_events.try_send(Event::EventToInHandshake) {
                                error!(
                                    "no se pudo enviar evento EventToInHandshake desde handler. {e}"
                                );
                            }
                        }
                    } else if handshake == "out" {
                        info!("FSM. Se recibió comando Handshake de salida.");
                        handshake_flag = handshake;

                        if state.new == StateGeneral::OutHandshake {
                            let flag = heapless::String::<15>::try_from(
                                handshake_flag.to_string().as_str(),
                            )
                            .unwrap_or_default();
                            let epoch = settings.read().unwrap().balance_epoch();
                            if let Err(e) = tx_response
                                .try_send(FsmServiceResponse::SendHandshake((epoch, flag)))
                            {
                                error!("no se pudo enviar SendHandshake desde el handler. {e}");
                            }
                        } else {
                            if let Err(e) = tx_events.try_send(Event::EventToOutHandshake) {
                                error!(
                                    "no se pudo enviar evento EventToOutHandshake desde handler. {e}"
                                );
                            }
                        }
                    }
                }
                FsmServiceCommand::EdgeState(idx) => {
                    // Leer los datos del pool
                    let (state_str, balance_epoch, durat, evt_frequency, evt_jitter) = {
                        let slot = crate::app::pool::pool::CORE_DATA_POOL[idx].lock().unwrap();
                        if let Some(MessageFromEdge::State(s)) = &slot.from_edge {
                            (
                                s.state.clone(),
                                s.balance_epoch,
                                s.duration,
                                s.frequency,
                                s.jitter,
                            )
                        } else {
                            continue; // Si estaba vacío, ignoramos
                        }
                    };

                    {
                        CORE_DATA_POOL[idx].lock().unwrap().from_edge = None;
                    }
                    free_pool_index_tx.try_send(idx).unwrap();

                    if state_str == "normal" {
                        if state.new == StateGeneral::Normal {
                            continue;
                        }
                        info!("FSM. Ingresando evento para ir a Normal...");
                        if let Err(e) = tx_events.try_send(Event::EventToNormal) {
                            error!("no se pudo enviar evento EventToNormal desde handler. {e}");
                        }
                        continue;
                    }
                    if state_str == "balance" {
                        if state.new == StateGeneral::Normal
                            || state.new == StateGeneral::Bypass
                            || state.new == StateGeneral::Store
                            || state.new == StateGeneral::InitSystem
                        {
                            info!("FSM. Ingresando evento para ir a InitBalance...");
                            let epoch = settings.read().unwrap().balance_epoch();
                            if balance_epoch >= epoch {
                                if let Err(e) = tx_events.try_send(Event::EventInitBalance) {
                                    error!(
                                        "no se pudo enviar evento EventInitBalance desde handler. {e}"
                                    );
                                }
                                if let Err(e) = tx_response
                                    .try_send(FsmServiceResponse::UpdateBalanceEpoch(balance_epoch))
                                {
                                    error!(
                                        "no se pudo enviar UpdateBalanceEpoch desde handler. {e}"
                                    );
                                }
                                duration = durat;
                            }
                        }
                        continue;
                    }
                    if state_str == "safe" {
                        match state.new {
                            StateGeneral::Safe {
                                frequency: _,
                                jitter: _,
                            } => {
                                continue;
                            }
                            _ => {
                                info!("FSM. Ingresando evento para ir a Safe...");
                                frequency = evt_frequency;
                                jitter = evt_jitter;
                                if let Err(e) = tx_events.try_send(Event::EventToSafe) {
                                    error!(
                                        "no se pudo enviar evento EventToSafe desde handler. {e}"
                                    );
                                }
                            }
                        }
                    }
                }
                FsmServiceCommand::Phase(phase) => {
                    let epoch = settings.read().unwrap().balance_epoch();
                    if phase.0 == epoch {
                        if phase.1 == "alert" {
                            info!("FSM. Se recibió comando de fase de alerta.");
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToAlert) {
                                error!("no se pudo enviar evento EventToAlert desde handler. {e}");
                            }
                        } else if phase.1 == "data" {
                            info!("FSM. Se recibió comando de fase de data.");
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToData) {
                                error!("no se pudo enviar evento EventToData desde handler. {e}");
                            }
                        } else if phase.1 == "monitor" {
                            info!("FSM. Se recibió comando de fase de monitor.");
                            frequency = phase.2;
                            jitter = phase.3;
                            if let Err(e) = tx_events.try_send(Event::EventToMonitor) {
                                error!(
                                    "no se pudo enviar evento EventToMonitor desde handler. {e}"
                                );
                            }
                        }
                    } else if phase.0 > epoch {
                        info!("FSM. Se recibió un mensaje de fase con epoch mas nuevo.");
                        if let Err(e) = tx_events.try_send(Event::EventNewerEpoch) {
                            error!("no se pudo enviar evento EventNewerEpoch desde handler. {e}");
                        }
                        if let Err(e) =
                            tx_response.try_send(FsmServiceResponse::UpdateBalanceEpoch(phase.0))
                        {
                            error!("no se pudo enviar UpdateBalanceEpoch desde handler. {e}");
                        }
                    }
                }
                FsmServiceCommand::AnAlertWasGenerated => {
                    info!("FSM. Se recibió la notificación de que se generó una alerta.");
                    if let Err(e) = tx_events.try_send(Event::EventAlertGenerated) {
                        error!("no se pudo enviar evento EventAlertGenerated desde handler. {e}");
                    }
                }
                FsmServiceCommand::EdgeDisconnected => {
                    info!("FSM. Se recibió comando de Edge muerto por falta de latidos.");
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutInitSystem => {
                    info!("FSM. Se recibió timeout en InitSystem.");
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutCooling => {
                    info!("FSM. Se recibió comando de timeout en Cooling.");
                    if let Err(e) = tx_events.try_send(Event::EventTimeoutCooling) {
                        error!("no se pudo enviar evento EventTimeoutCooling desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutBypass => {
                    info!("FSM. Se recibió comando de timeout Bypass.");
                    if let Err(e) = tx_events.try_send(Event::EventTimeoutBypass) {
                        error!("no se pudo enviar evento EventTimeoutBypass desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutInitBalance => {
                    info!("FSM. Se recibió comando de fase de monitor.");
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutHandshake => {
                    info!("FSM. Se recibió comando de timeout en Handshake.");
                    if let Err(e) = tx_events.try_send(Event::EventEdgeIsDead) {
                        error!("no se pudo enviar evento EventEdgeIsDead desde handler. {e}");
                    }
                }
                FsmServiceCommand::TimeoutAllBalance => {
                    info!("FSM. Se recibió comando de timeout de todo el protocolo de balanceo.");
                    if let Err(e) = tx_events.try_send(Event::EventToNormal) {
                        error!("no se pudo enviar evento EventToNormal desde handler. {e}");
                    }
                }
                FsmServiceCommand::BypassAlert(idx) => {
                    info!("FSM. Se recibió mensaje de alerta para Bypass.");
                    if let Err(e) = tx_payload.try_send(idx) {
                        error!("no se pudo enviar Payload desde el handler. {e}");
                        {
                            CORE_DATA_POOL[idx].lock().unwrap().serialized = None;
                        }
                        free_pool_index_tx.try_send(idx).unwrap();
                    }
                }
                FsmServiceCommand::NetworkDropped => {
                    info!("FSM. Se recibió comando NetworkDropped.");
                    if mqtt_status == MqttStatus::Connected {
                        mqtt_status = MqttStatus::Disconnected;
                        if let Err(e) = tx_events.try_send(Event::EventNetworkDropped) {
                            error!(
                                "no se pudo enviar evento EventNetworkDropped desde handler. {e}"
                            );
                        }
                    }
                }
                FsmServiceCommand::MqttConnected => {
                    info!("FSM. Se recibió comando MqttConnected.");
                    if mqtt_status == MqttStatus::Disconnected {
                        mqtt_status = MqttStatus::Connected;
                        if state.new == StateGeneral::InitSystem {
                            if let Err(e) = tx_events.try_send(Event::EventStart) {
                                error!("no se pudo enviar evento EventStart desde handler. {e}");
                            }
                        } else {
                            if let Err(e) = tx_events.try_send(Event::EventNetworkRestored) {
                                error!(
                                    "no se pudo enviar evento EventNetworkRestored desde handler. {e}"
                                );
                            }
                        }
                    }
                }
            },
            Either4::Second(Err(e)) => {
                error!("el canal rx_command se ha cerrado. {e}");
                break;
            }

            Either4::Third(Ok(msg)) => match msg {
                InternalEvent::Timeout => {
                    info!("FSM. Se recibió evento de timeout de LinkageProtocol.");
                    if let Err(e) = tx_response.try_send(FsmServiceResponse::LinkageProtocol) {
                        error!("no se pudo enviar LinkageProtocol desde el handler. {e}");
                    }
                }
            },
            Either4::Third(Err(e)) => {
                error!("{e}");
            }

            Either4::Fourth(Ok(msg)) => match msg {
                InternalEvent::Timeout => {
                    if mqtt_status == MqttStatus::Connected {
                        let state_hub: String<20> = match state.new {
                            StateGeneral::Normal => {
                                heapless::String::<20>::try_from("normal").unwrap_or_default()
                            }
                            StateGeneral::InitBalance
                            | StateGeneral::InHandshake
                            | StateGeneral::Alert {
                                frequency: _,
                                jitter: _,
                            }
                            | StateGeneral::Data {
                                frequency: _,
                                jitter: _,
                            }
                            | StateGeneral::Monitor {
                                frequency: _,
                                jitter: _,
                            }
                            | StateGeneral::OutHandshake => {
                                heapless::String::<20>::try_from("balance").unwrap_or_default()
                            }
                            StateGeneral::Safe {
                                frequency: _,
                                jitter: _,
                            } => heapless::String::<20>::try_from("safe").unwrap_or_default(),
                            _ => heapless::String::<20>::try_from("none").unwrap_or_default(),
                        };
                        if let Err(e) =
                            tx_response.try_send(FsmServiceResponse::GenerateHubState(state_hub))
                        {
                            error!("no se pudo enviar GenerateHubState desde el handler. {e}");
                        }
                    }
                }
            },
            Either4::Fourth(Err(e)) => {
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
    rx_data: Receiver<usize>,
    free_idx_tx: Sender<usize>,
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

                    Either::Second(Ok(idx)) => {
                        let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                        match &slot.serialized {
                            Some(msg) => {
                                if let Err(e) = service.send_payload(msg.get_payload()) {
                                    error!("{e}");
                                }
                            }
                            _ => {}
                        }
                        slot.serialized = None;
                        free_idx_tx.try_send(idx).unwrap();
                    }
                    Either::Second(Err(e)) => {
                        error!("{e}");
                    }
                }
            }
        }
    }
}
