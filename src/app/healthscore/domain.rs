use async_channel::{Receiver, Sender};
use edge_executor::LocalExecutor;
use embassy_futures::select::{Either, select};
use embassy_time::{Duration, Timer};
use esp_idf_hal::sys::esp_timer_get_time;
use heapless::Vec;
use log::{error, info, warn};

const TIMEOUT_QOS_1: u8 = 15;
const SOCKET_ERROR: u8 = 10;
const HIGH_LATENCY_PENALTY: u8 = 5;
const MEDIUM_LATENCY_PENALTY: u8 = 2;
const LOW_LATENCY_BONUS: u8 = 5;
const MAX_PENDING_MSGS: usize = 5;
const MSG_TIMEOUT_US: u64 = 5_000_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealthState {
    Healthy,
    Degraded,
    Critical,
    Unavailable,
}

#[derive(Debug, Clone)]
pub enum HealthServiceCommand {
    MsgSent {
        msg_id: i32,
    },
    PubAck {
        msg_id: i32,
        return_code: u8,
        is_mqtt5: bool,
    },
    ErrorSend,
    Disconnect,
}

/// Comando de salida hacia la FSM o sistema central
#[derive(Debug, Clone)]
pub enum HealthServiceResponse {
    StateChanged(HealthState),
}

// --- Lógica Pura del Score (Desacoplada) ---

pub struct Score {
    value: u8,
}

impl Score {
    pub fn new() -> Self {
        Self { value: 100 }
    }

    pub fn get_state(&self) -> HealthState {
        match self.value {
            80..=100 => HealthState::Healthy,
            55..=79 => HealthState::Degraded,
            20..=54 => HealthState::Critical,
            _ => HealthState::Unavailable,
        }
    }

    /// Aplica una diferencia asegurando que el score siempre quede entre 1 y 100.
    /// Resuelve el bug de tu versión en C.
    fn apply_delta(&mut self, delta: i16) {
        let new_value = (self.value as i16 + delta).clamp(1, 100) as u8;
        self.value = new_value;
    }

    pub fn penalize_timeout(&mut self) {
        self.apply_delta(-(TIMEOUT_QOS_1 as i16));
    }

    pub fn penalize_socket_error(&mut self) {
        self.apply_delta(-(SOCKET_ERROR as i16));
    }

    pub fn set_disconnected(&mut self) {
        self.value = 1;
    }

    pub fn update_rtt(&mut self, rtt_ms: u64) {
        if rtt_ms >= 1000 {
            self.apply_delta(-(HIGH_LATENCY_PENALTY as i16));
        } else if rtt_ms >= 500 {
            self.apply_delta(-(MEDIUM_LATENCY_PENALTY as i16));
        } else if rtt_ms < 200 {
            self.apply_delta(LOW_LATENCY_BONUS as i16);
        }
    }
}

// --- Estructuras Internas del Actor ---

#[derive(Debug, Clone, Copy)]
struct PendingMsg {
    msg_id: i32,
    start_time_us: u64,
}

// --- El Actor (Servicio) ---

pub struct HealthScoreService {
    sender: Sender<HealthServiceResponse>,
    receiver: Receiver<HealthServiceCommand>,
}

impl HealthScoreService {
    pub fn new(
        sender: Sender<HealthServiceResponse>,
        receiver: Receiver<HealthServiceCommand>,
    ) -> Self {
        info!("creando HealthScoreService...");
        Self { sender, receiver }
    }

    pub async fn run<'a>(self, _executor: &'a LocalExecutor<'a>) {
        let mut score = Score::new();
        let mut last_reported_state = score.get_state();
        let mut pending_msgs: Vec<PendingMsg, MAX_PENDING_MSGS> = Vec::new();

        // Closure helper para notificar solo cuando el estado lógico cambie
        let mut check_and_notify = |score: &Score, sender: &Sender<HealthServiceResponse>| {
            let current_state = score.get_state();
            if current_state != last_reported_state {
                last_reported_state = current_state;
                let _ = sender.try_send(HealthServiceResponse::StateChanged(current_state));
                info!("Red cambió de estado a: {:?}", current_state);
            }
        };
        info!("iniciando HealthScoreService...");
        loop {
            // Esperamos un evento MQTT o que pase 1 segundo para hacer barrido de timeouts.
            let timer_fut = Timer::after(Duration::from_secs(1));
            let event_fut = self.receiver.recv();

            match select(event_fut, timer_fut).await {
                // Recibimos un Evento MQTT
                Either::First(Ok(event)) => {
                    let now_us = unsafe { esp_timer_get_time() } as u64;

                    match event {
                        HealthServiceCommand::MsgSent { msg_id } => {
                            let pm = PendingMsg {
                                msg_id,
                                start_time_us: now_us,
                            };
                            if pending_msgs.is_full() {
                                // Si está lleno, eliminamos el más viejo para hacer lugar
                                let _ = pending_msgs.remove(0);
                            }
                            let _ = pending_msgs.push(pm);
                        }

                        HealthServiceCommand::PubAck {
                            msg_id,
                            return_code,
                            is_mqtt5,
                        } => {
                            // Buscamos y extraemos el mensaje del vector
                            if let Some(idx) = pending_msgs.iter().position(|m| m.msg_id == msg_id)
                            {
                                let pm = pending_msgs.remove(idx);

                                if is_mqtt5 && return_code != 0x00 {
                                    error!("PUBACK error: 0x{:02X}", return_code);
                                    score.set_disconnected();
                                } else {
                                    let rtt_us = now_us.saturating_sub(pm.start_time_us);
                                    let rtt_ms = rtt_us / 1000;
                                    score.update_rtt(rtt_ms);
                                }
                                check_and_notify(&score, &self.sender);
                            }
                        }

                        HealthServiceCommand::ErrorSend => {
                            score.penalize_socket_error();
                            check_and_notify(&score, &self.sender);
                        }

                        HealthServiceCommand::Disconnect => {
                            score.set_disconnected();
                            pending_msgs.clear();
                            check_and_notify(&score, &self.sender);
                        }
                    }
                }
                Either::First(Err(e)) => {
                    error!("fallo en canal de HealthScore: {e}");
                    break;
                }

                // Pasó 1 Segundo (Garbage Collection de Timeouts)
                Either::Second(_) => {
                    let now_us = unsafe { esp_timer_get_time() } as u64;
                    let mut has_timeouts = false;

                    // Usamos retain() que elimina eficientemente los elementos que no cumplan la condición
                    pending_msgs.retain(|pm| {
                        let is_timeout = now_us.saturating_sub(pm.start_time_us) > MSG_TIMEOUT_US;
                        if is_timeout {
                            score.penalize_timeout();
                            has_timeouts = true;
                            false // false significa "eliminar del vector"
                        } else {
                            true // true significa "conservar en el vector"
                        }
                    });

                    if has_timeouts {
                        warn!("se detectaron timeouts en mensajes QoS 1. Score penalizado.");
                        check_and_notify(&score, &self.sender);
                    }
                }
            }
        }
    }
}
