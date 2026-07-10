use crate::app::fsm::domain::{Action, Event, FsmState, Transition};
use async_channel::{Receiver, Sender, bounded};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use esp_idf_hal::reset::restart;
use log::{error, info};

pub enum FsmServiceResponse {
    CheckFirmware,
    NotifyFirmware(String),
    LinkageProtocol,
}

pub enum FsmServiceCommand {
    NotUpdateFirmware, // Incluye error tambien
    UpdateFirmware(String),
    LinkageOk,
    Handshake(String),
    Safe((String, u32, u32)),
    Normal,
}

pub struct FsmService {
    sender: Sender<FsmServiceResponse>,
    receiver: Receiver<FsmServiceCommand>,
}

impl FsmService {
    pub fn new(sender: Sender<FsmServiceResponse>, receiver: Receiver<FsmServiceCommand>) -> Self {
        Self { sender, receiver }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>) {
        let (tx_actions, rx_actions) = bounded::<Vec<Action>>(10);
        let (tx_event, rx_event) = bounded::<Event>(10);
        let (tx_response, rx_response) = bounded::<FsmServiceResponse>(10);
        let (tx_command, rx_command) = bounded::<FsmServiceCommand>(10);

        executor
            .spawn(handler_events_and_actions(
                tx_event,
                tx_response,
                rx_actions,
                rx_command,
            ))
            .detach();
        executor.spawn(run_fsm(tx_actions, rx_event)).detach();

        loop {
            match select(self.receiver.recv(), rx_response.recv()).await {
                // Caso 1: Recibimos un comando del exterior (receiver)
                Either::First(Ok(cmd)) => {
                    if let Err(e) = tx_command.try_send(cmd) {
                        error!("no se pudo enviar mensaje para generar, mensaje descartado. {e}");
                    }
                }
                Either::First(Err(_)) => {
                    error!("el canal receiver se ha cerrado.");
                    break;
                }

                // Caso 2: Recibimos una respuesta interna del handler_events_and_actions()
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

async fn handler_events_and_actions(
    tx_events: Sender<Event>,
    tx_response: Sender<FsmServiceResponse>,
    rx_action: Receiver<Vec<Action>>,
    rx_extern_events: Receiver<FsmServiceCommand>,
) {
    let mut firmware_version = String::new();
    loop {
        match select(rx_action.recv(), rx_extern_events.recv()).await {
            // Caso 1: acciones de la FSM
            Either::First(Ok(vec_action)) => {
                for action in vec_action {
                    match action {
                        Action::ActionCheckFirmware => {
                            if let Err(e) = tx_response.try_send(FsmServiceResponse::CheckFirmware)
                            {
                                error!("no se pudo enviar InitMqtt desde el handler. {e}");
                            }
                        }
                        Action::ActionNotifyFirmware => {
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
                        Action::ActionRestart => {
                            info!("reiniciando sistema...");
                            restart();
                        }

                        Action::ActionLinkageProtocol => {
                            if let Err(e) =
                                tx_response.try_send(FsmServiceResponse::LinkageProtocol)
                            {
                                error!("no se pudo enviar LinkageProtocol desde el handler. {e}");
                            }
                        }
                    }
                }
            }
            Either::First(Err(_)) => {
                error!("el canal rx_action se ha cerrado.");
                break;
            }

            // Caso 2: eventos externos
            Either::Second(Ok(event)) => match event {
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
                    if let Err(e) = tx_events.try_send(Event::EventLinkageOk) {
                        error!("no se pudo enviar evento EventLinkageOk desde handler. {e}");
                    }
                }
                FsmServiceCommand::Handshake(_) => {}
                FsmServiceCommand::Safe(_) => {}
                FsmServiceCommand::Normal => {}
            },
            Either::Second(Err(_)) => {
                error!("el canal rx_command se ha cerrado.");
                break;
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
async fn run_fsm(tx_actions: Sender<Vec<Action>>, rx_event: Receiver<Event>) {
    info!("iniciando tarea fsm");
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

/*
/// Tarea Worker asíncrona para el modo Bypass.
///
/// Utiliza canales (`Receiver`) en lugar de `xQueueReceive` y `xTaskNotifyWait`.

pub async fn run_bypass_worker<S: BypassService>(
    mut service: S,
    control_rx: Receiver<BypassCommand>,
    temp_alerts_rx: Receiver<Vec<u8>>,
    air_alerts_rx: Receiver<Vec<u8>>,
) {
    loop {
        // 1. Espera pasiva y asíncrona hasta recibir NOTIFY_CMD_START
        if let Ok(BypassCommand::Start) = control_rx.recv().await {
            info!("Modo Bypass INICIADO");

            loop {
                // 2. Preparamos los "Futures" (promesas) de lectura.
                // Llamar a .fuse() es necesario para cancelar de forma segura
                // los eventos que no ocurrieron dentro del select!.
                let mut temp_fut = temp_alerts_rx.recv().fuse();
                let mut air_fut = air_alerts_rx.recv().fuse();
                let mut ctrl_fut = control_rx.recv().fuse();

                // 3. El macro select! SUSPENDE esta tarea (0% consumo de CPU).
                // Se despertará inmediatamente solo cuando al menos UNO de los
                // canales reciba nueva información.
                select! {
                    temp_res = temp_fut => {
                        if let Ok(payload) = temp_res {
                            let _ = service.send_payload(&payload);
                        }
                    },
                    air_res = air_fut => {
                        if let Ok(payload) = air_res {
                            let _ = service.send_payload(&payload);
                        }
                    },
                    cmd_res = ctrl_fut => {
                        if let Ok(BypassCommand::Stop) = cmd_res {
                            info!("Modo Bypass DETENIDO");
                            break; // Rompe el bucle interno y vuelve a dormir esperando Start
                        }
                    }
                }
            }
        }
    }
}*/
