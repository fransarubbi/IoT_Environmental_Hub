use log::{info, error};
use async_channel::{Sender, Receiver};
use crate::app::fsm::domain::{Action, Event, Transition, FsmState};


/// Tarea asíncrona que ejecuta la lógica pura de la Máquina de Estados.
///
/// Mantiene el estado persistente (`FsmState`) y avanza tras recibir eventos.
///
/// * `tx_actions`: Canal para emitir los efectos secundarios que deben ejecutarse.
/// * `rx_event`: Canal de entrada de eventos (triggers).
pub async fn run_fsm(
    tx_actions: Sender<Vec<Action>>,
    rx_event: Receiver<Event>
) {
    info!("iniciando tarea fsm");
    let mut state = FsmState::new();

    match state.step(Event::EventStart) {
        Transition::Valid(t) => {
            state = t.change_state();
            let _ = tx_actions.send(t.actions()).await;
        }
        Transition::Invalid(t) => error!("fsm transición inválida {}", t.get_invalid()),
    }

    loop {
        if let Ok(event) = rx_event.recv().await {
            match state.step(event) {
                Transition::Valid(t) => {
                    state = t.change_state();
                    let _ = tx_actions.send(t.actions()).await;
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
