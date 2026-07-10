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
use embassy_futures::select::{Either5, select5};
use log::error;
use std::sync::{Arc, RwLock};

use crate::app::fsm::logic::{FsmServiceCommand, FsmServiceResponse};
use crate::app::message::domain::{MessageFromEdge, MessageServiceCommand, MessageServiceResponse};
use crate::app::system_settings::domain::{ConfigCommand, ConfigResponse, SystemSettings};
use crate::bsp::mqtt::MqttData;
use crate::bsp::ota::{OtaCommand, OtaResponse};
use crate::bsp::wifi::WifiCommand;

pub struct Core {
    core_from_fsm_service: Receiver<FsmServiceResponse>,
    core_to_fsm_service: Sender<FsmServiceCommand>,

    core_from_msg_service: Receiver<MessageServiceResponse>,
    core_to_msg_service: Sender<MessageServiceCommand>,

    core_to_config_service: Sender<ConfigCommand>,
    core_from_config_service: Receiver<ConfigResponse>,

    core_from_mqtt_service: Receiver<MqttData>,
    core_to_mqtt_service: Sender<MqttData>,

    core_to_wifi_service: Sender<WifiCommand>,

    core_from_ota_service: Receiver<OtaResponse>,
    core_to_ota_service: Sender<OtaCommand>,
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

    core_to_wifi_service: Option<Sender<WifiCommand>>,

    core_from_ota_service: Option<Receiver<OtaResponse>>,
    core_to_ota_service: Option<Sender<OtaCommand>>,
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

            core_to_wifi_service: self
                .core_to_wifi_service
                .ok_or(anyhow!("falta: core_to_config_service"))?,

            core_from_ota_service: self
                .core_from_ota_service
                .ok_or(anyhow!("falta: core_from_ota_service"))?,
            core_to_ota_service: self
                .core_to_ota_service
                .ok_or(anyhow!("falta: core_to_ota_service"))?,
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
            match select5(
                self.core_from_fsm_service.recv(),
                self.core_from_msg_service.recv(),
                self.core_from_config_service.recv(),
                self.core_from_mqtt_service.recv(),
                self.core_from_ota_service.recv(),
            )
            .await
            {
                Either5::First(Ok(response)) => {
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
                Either5::First(Err(e)) => {
                    error!("el canal core_from_fsm_service se ha cerrado. {e}");
                }

                Either5::Second(Ok(response)) => match response {
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
                            if msg.balance_epoch >= epoch && msg.metadata.sender_user_id == edge {
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
                            if msg.metadata.sender_user_id == edge && msg.linkage_ack {
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
                            if msg.metadata.sender_user_id == edge {
                                if let Err(e) =
                                    self.core_to_fsm_service.try_send(FsmServiceCommand::Normal)
                                {
                                    error!("no se pudo enviar Normal en core. {e}");
                                }
                            }
                        }
                        MessageFromEdge::StateSafeMode(msg) => {
                            let edge = settings.read().unwrap().id_edge().to_string();
                            if msg.metadata.sender_user_id == edge {
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
                            if msg.metadata.sender_user_id == edge && msg.network == network {
                                if msg.metadata.destination_id == mac
                                    || msg.metadata.destination_id == "all".to_string()
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
                Either5::Second(Err(e)) => {
                    error!("el canal core_from_msg_service se ha cerrado. {e}");
                }

                Either5::Third(Ok(response)) => {}
                Either5::Third(Err(e)) => {}

                Either5::Fourth(Ok(response)) => {}
                Either5::Fourth(Err(e)) => {}

                Either5::Fifth(Ok(response)) => match response {
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
                Either5::Fifth(Err(e)) => {}
            }
        }
    }
}
