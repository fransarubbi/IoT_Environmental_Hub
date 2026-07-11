//! # Módulo Core: Orquestador Central del Sistema
//!
//! Este módulo implementa el componente `Core`, que actúa como el **Bus de Mensajes Central** y
//! el coordinador principal de la aplicación (patrón *Mediator*).
//!
//! ## Responsabilidad
//! Su única responsabilidad es recibir mensajes de los distintos servicios internos
//! y enrutarlos hacia su destino correcto.
//!
//! ## Arquitectura
//! El sistema sigue una arquitectura de estrella donde todos los servicios se comunican únicamente
//! con el `Core`, y el `Core` redistribuye los mensajes. Esto desacopla los servicios entre sí.

use anyhow::{Result, anyhow};
use async_channel::{Receiver, Sender};
use embassy_futures::select::{Either6, select6};
use log::error;
use std::sync::{Arc, RwLock};

use crate::app::fsm::logic::{FsmServiceCommand, FsmServiceResponse};
use crate::app::heartbeat::domain::{HeartbeatCommand, HeartbeatResponse};
use crate::app::message::domain::{MessageFromEdge, MessageServiceCommand, MessageServiceResponse};
use crate::app::system_settings::domain::{ConfigCommand, ConfigResponse, SystemSettings};
use crate::app::timer::logic::{TimerCommand, TimerResponse};
use crate::bsp::mqtt::MqttData;
use crate::bsp::ota::{OtaCommand, OtaResponse};
use crate::bsp::wifi::{WifiCommand, WifiResponse};

pub struct Core {
    core_from_fsm_service: Receiver<FsmServiceResponse>,
    core_to_fsm_service: Sender<FsmServiceCommand>,

    core_from_msg_service: Receiver<MessageServiceResponse>,
    core_to_msg_service: Sender<MessageServiceCommand>,

    core_to_config_service: Sender<ConfigCommand>,
    core_from_config_service: Receiver<ConfigResponse>,

    core_from_mqtt_service: Receiver<MqttData>,
    core_to_mqtt_service: Sender<MqttData>,

    core_from_wifi_service: Receiver<WifiResponse>,
    core_to_wifi_service: Sender<WifiCommand>,

    core_from_ota_service: Receiver<OtaResponse>,
    core_to_ota_service: Sender<OtaCommand>,

    core_from_timer_service: Receiver<TimerResponse>,
    core_to_timer_service: Sender<TimerCommand>,

    core_from_heartbeat_service: Receiver<HeartbeatResponse>,
    core_to_heartbeat_service: Sender<HeartbeatCommand>,
}

/// Builder para construir la estructura `Core`.
///
/// Dado que `Core` tiene múltiples dependencias estrictas (canales), este builder
/// asegura que todos los canales sean inyectados antes de crear la instancia,
/// evitando estados inválidos.
#[derive(Default)]
pub struct CoreBuilder {
    core_from_fsm_service: Option<Receiver<FsmServiceResponse>>,
    core_to_fsm_service: Option<Sender<FsmServiceCommand>>,

    core_from_msg_service: Option<Receiver<MessageServiceResponse>>,
    core_to_msg_service: Option<Sender<MessageServiceCommand>>,

    core_to_config_service: Option<Sender<ConfigCommand>>,
    core_from_config_service: Option<Receiver<ConfigResponse>>,

    core_from_mqtt_service: Option<Receiver<MqttData>>,
    core_to_mqtt_service: Option<Sender<MqttData>>,

    core_from_wifi_service: Option<Receiver<WifiResponse>>,
    core_to_wifi_service: Option<Sender<WifiCommand>>,

    core_from_ota_service: Option<Receiver<OtaResponse>>,
    core_to_ota_service: Option<Sender<OtaCommand>>,

    core_from_timer_service: Option<Receiver<TimerResponse>>,
    core_to_timer_service: Option<Sender<TimerCommand>>,

    core_from_heartbeat_service: Option<Receiver<HeartbeatResponse>>,
    core_to_heartbeat_service: Option<Sender<HeartbeatCommand>>,
}

impl CoreBuilder {
    // --- Métodos Setter ---
    // Cada método consume self y devuelve self para permitir encadenamiento.

    pub fn core_from_fsm_service(mut self, ch: Receiver<FsmServiceResponse>) -> Self {
        self.core_from_fsm_service = Some(ch);
        self
    }
    pub fn core_to_fsm_service(mut self, ch: Sender<FsmServiceCommand>) -> Self {
        self.core_to_fsm_service = Some(ch);
        self
    }

    pub fn core_from_msg_service(mut self, ch: Receiver<MessageServiceResponse>) -> Self {
        self.core_from_msg_service = Some(ch);
        self
    }
    pub fn core_to_msg_service(mut self, ch: Sender<MessageServiceCommand>) -> Self {
        self.core_to_msg_service = Some(ch);
        self
    }

    pub fn core_to_config_service(mut self, ch: Sender<ConfigCommand>) -> Self {
        self.core_to_config_service = Some(ch);
        self
    }

    pub fn core_from_config_service(mut self, ch: Receiver<ConfigResponse>) -> Self {
        self.core_from_config_service = Some(ch);
        self
    }

    pub fn core_to_mqtt_service(mut self, ch: Sender<MqttData>) -> Self {
        self.core_to_mqtt_service = Some(ch);
        self
    }

    pub fn core_from_mqtt_service(mut self, ch: Receiver<MqttData>) -> Self {
        self.core_from_mqtt_service = Some(ch);
        self
    }

    pub fn core_from_wifi_service(mut self, ch: Receiver<WifiResponse>) -> Self {
        self.core_from_wifi_service = Some(ch);
        self
    }

    pub fn core_to_wifi_service(mut self, ch: Sender<WifiCommand>) -> Self {
        self.core_to_wifi_service = Some(ch);
        self
    }

    pub fn core_from_ota_service(mut self, ch: Receiver<OtaResponse>) -> Self {
        self.core_from_ota_service = Some(ch);
        self
    }
    pub fn core_to_ota_service(mut self, ch: Sender<OtaCommand>) -> Self {
        self.core_to_ota_service = Some(ch);
        self
    }

    pub fn core_from_timer_service(mut self, ch: Receiver<TimerResponse>) -> Self {
        self.core_from_timer_service = Some(ch);
        self
    }
    pub fn core_to_timer_service(mut self, ch: Sender<TimerCommand>) -> Self {
        self.core_to_timer_service = Some(ch);
        self
    }

    pub fn core_from_heartbeat_service(mut self, ch: Receiver<HeartbeatResponse>) -> Self {
        self.core_from_heartbeat_service = Some(ch);
        self
    }
    pub fn core_to_heartbeat_service(mut self, ch: Sender<HeartbeatCommand>) -> Self {
        self.core_to_heartbeat_service = Some(ch);
        self
    }

    /// Construye la instancia de `Core`.
    ///
    /// # Errores
    /// Retorna un `Err(String)` si falta configurar alguno de los canales.
    pub fn build(self) -> Result<Core> {
        Ok(Core {
            core_from_fsm_service: self
                .core_from_fsm_service
                .ok_or(anyhow!("falta: core_from_fsm_service"))?,
            core_to_fsm_service: self
                .core_to_fsm_service
                .ok_or(anyhow!("falta: core_to_fsm_service"))?,

            core_from_msg_service: self
                .core_from_msg_service
                .ok_or(anyhow!("falta: core_from_msg_service"))?,
            core_to_msg_service: self
                .core_to_msg_service
                .ok_or(anyhow!("falta: core_to_msg_service"))?,

            core_to_config_service: self
                .core_to_config_service
                .ok_or(anyhow!("falta: core_to_config_service"))?,
            core_from_config_service: self
                .core_from_config_service
                .ok_or(anyhow!("falta: core_from_config_service"))?,

            core_to_mqtt_service: self
                .core_to_mqtt_service
                .ok_or(anyhow!("falta: core_to_config_service"))?,
            core_from_mqtt_service: self
                .core_from_mqtt_service
                .ok_or(anyhow!("falta: core_from_config_service"))?,

            core_from_wifi_service: self
                .core_from_wifi_service
                .ok_or(anyhow!("falta: core_from_wifi_service"))?,
            core_to_wifi_service: self
                .core_to_wifi_service
                .ok_or(anyhow!("falta: core_to_wifi_service"))?,

            core_from_ota_service: self
                .core_from_ota_service
                .ok_or(anyhow!("falta: core_from_ota_service"))?,
            core_to_ota_service: self
                .core_to_ota_service
                .ok_or(anyhow!("falta: core_to_ota_service"))?,

            core_from_timer_service: self
                .core_from_timer_service
                .ok_or(anyhow!("falta: core_from_timer_service"))?,
            core_to_timer_service: self
                .core_to_timer_service
                .ok_or(anyhow!("falta: core_to_timer_service"))?,

            core_from_heartbeat_service: self
                .core_from_heartbeat_service
                .ok_or(anyhow!("falta: core_from_heartbeat_service"))?,
            core_to_heartbeat_service: self
                .core_to_heartbeat_service
                .ok_or(anyhow!("falta: core_to_heartbeat_service"))?,
        })
    }
}

impl Core {
    /// Crea un nuevo `CoreBuilder` inicializado con valores por defecto (None).
    pub fn builder() -> CoreBuilder {
        CoreBuilder::default()
    }

    pub async fn run(self, settings: Arc<RwLock<SystemSettings>>) {
        loop {
            match select6(
                self.core_from_fsm_service.recv(),
                self.core_from_msg_service.recv(),
                self.core_from_config_service.recv(),
                self.core_from_mqtt_service.recv(),
                self.core_from_ota_service.recv(),
                self.core_from_timer_service.recv(),
            )
            .await
            {
                Either6::First(Ok(response)) => {
                    match response {
                        FsmServiceResponse::LinkageProtocol => {
                            // enviar mensaje
                        }
                        FsmServiceResponse::NotifyFirmware(version) => {
                            if let Err(e) = self
                                .core_to_msg_service
                                .try_send(MessageServiceCommand::GenerateFirmwareOk(version))
                            {
                                error!("no se pudo enviar GenerateFirmwareOk en core. {e}");
                            }
                        }
                        FsmServiceResponse::CheckFirmware => {
                            if let Err(e) =
                                self.core_to_ota_service.try_send(OtaCommand::CheckFirmware)
                            {
                                error!("no se pudo enviar CheckFirmware en core. {e}");
                            }
                        }
                    }
                }
                Either6::First(Err(e)) => {
                    error!("el canal core_from_fsm_service se ha cerrado. {e}");
                }

                Either6::Second(Ok(response)) => match response {
                    MessageServiceResponse::Serialized(msg) => {
                        if let Err(e) = self
                            .core_to_mqtt_service
                            .try_send(MqttData::OutMessage(msg))
                        {
                            error!("no se pudo enviar OutMessage en core. {e}");
                        }
                    }
                    MessageServiceResponse::Message(msg) => match msg {
                        MessageFromEdge::FromServerSettings(msg) => {}
                        MessageFromEdge::FromServerSettingsAck(msg) => {}
                        MessageFromEdge::HandshakeToHub(msg) => {
                            let epoch = settings.read().unwrap().balance_epoch();
                            let edge = settings.read().unwrap().id_edge().to_string();
                            if msg.balance_epoch >= epoch && msg.metadata.sender_user_id == edge.as_str() {
                                if let Err(e) = self
                                    .core_to_fsm_service
                                    .try_send(FsmServiceCommand::Handshake(msg.flag))
                                {
                                    error!("no se pudo enviar UpdateFirmware en core. {e}");
                                }
                            }
                        }
                        MessageFromEdge::Heartbeat(msg) => {}
                        MessageFromEdge::LinkageAck(msg) => {
                            let edge = settings.read().unwrap().id_edge().to_string();
                            if msg.metadata.sender_user_id == edge.as_str() && msg.linkage_ack {
                                if let Err(e) = self
                                    .core_to_fsm_service
                                    .try_send(FsmServiceCommand::LinkageOk)
                                {
                                    error!("no se pudo enviar LinkageOk en core. {e}");
                                }
                            }
                        }
                        MessageFromEdge::PhaseNotification(msg) => {}
                        MessageFromEdge::StateBalanceMode(msg) => {}
                        MessageFromEdge::StateNormal(msg) => {
                            let edge = settings.read().unwrap().id_edge().to_string();
                            if msg.metadata.sender_user_id == edge.as_str() {
                                if let Err(e) =
                                    self.core_to_fsm_service.try_send(FsmServiceCommand::Normal)
                                {
                                    error!("no se pudo enviar Normal en core. {e}");
                                }
                            }
                        }
                        MessageFromEdge::StateSafeMode(msg) => {
                            let edge = settings.read().unwrap().id_edge().to_string();
                            if msg.metadata.sender_user_id == edge.as_str() {
                                if let Err(e) = self.core_to_fsm_service.try_send(
                                    FsmServiceCommand::Safe((msg.state, msg.frequency, msg.jitter)),
                                ) {
                                    error!("no se pudo enviar Safe en core. {e}");
                                }
                            }
                        }
                        MessageFromEdge::UpdateFirmware(msg) => {
                            let edge = settings.read().unwrap().id_edge().to_string();
                            let network = settings.read().unwrap().id_network().to_string();
                            let mac = settings.read().unwrap().mac_addr().to_string();
                            if msg.metadata.sender_user_id == edge.as_str() && msg.network == network.as_str() {
                                if msg.metadata.destination_id == mac.as_str()
                                    || msg.metadata.destination_id == "all"
                                {
                                    if let Err(e) =
                                        self.core_to_ota_service.try_send(OtaCommand::CheckFirmware)
                                    {
                                        error!("no se pudo enviar Safe en core. {e}");
                                    }
                                }
                            }
                        }
                    },
                },
                Either6::Second(Err(e)) => {
                    error!("el canal core_from_msg_service se ha cerrado. {e}");
                }

                Either6::Third(Ok(response)) => {}
                Either6::Third(Err(e)) => {
                    error!("el canal core_from_config_service se ha cerrado. {e}");
                }

                Either6::Fourth(Ok(response)) => {}
                Either6::Fourth(Err(e)) => {
                    error!("el canal core_from_mqtt_service se ha cerrado. {e}");
                }

                Either6::Fifth(Ok(response)) => match response {
                    OtaResponse::NoUpdateAvailable => {
                        if let Err(e) = self
                            .core_to_fsm_service
                            .try_send(FsmServiceCommand::NotUpdateFirmware)
                        {
                            error!("no se pudo enviar NotUpdateFirmware en core. {e}");
                        }
                    }
                    OtaResponse::UpdatedSuccesful(version) => {
                        if let Err(e) = self
                            .core_to_fsm_service
                            .try_send(FsmServiceCommand::UpdateFirmware(version))
                        {
                            error!("no se pudo enviar UpdateFirmware en core. {e}");
                        }
                    }
                },
                Either6::Fifth(Err(e)) => {
                    error!("el canal core_from_ota_service se ha cerrado. {e}");
                }

                Either6::Sixth(Ok(response)) => match response {
                    TimerResponse::InitSystemReady => {}
                    TimerResponse::InitBalanceReady => {}
                    TimerResponse::HandshakeReady => {}
                    TimerResponse::HeartbeatBalanceReady => {}
                    TimerResponse::HeartbeatNormalReady => {}
                    TimerResponse::HeartbeatSafeReady => {}
                    TimerResponse::CoolingReady => {}
                    TimerResponse::BypassReady => {}
                },
                Either6::Sixth(Err(e)) => {
                    error!("el canal core_from_timer_service se ha cerrado. {e}");
                }
            }
        }
    }
}
