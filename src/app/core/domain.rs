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
use futures::{FutureExt, select};
use log::error;
use std::sync::{Arc, RwLock};

use crate::app::{
    data::domain::{DataServiceCommand, DataServiceResponse},
    fsm::{
        domain::StateGeneral,
        logic::{FsmServiceCommand, FsmServiceResponse},
    },
    healthscore::domain::{HealthServiceCommand, HealthServiceResponse, HealthState},
    heartbeat::domain::{HeartbeatCommand, HeartbeatResponse, StateForHeartbeat},
    message::domain::{MessageFromEdge, MessageServiceCommand, MessageServiceResponse},
    system_settings::domain::{ConfigCommand, ConfigField, ConfigResponse, SystemSettings},
    timer::logic::{TimerCommand, TimerResponse},
};

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

    core_from_timer_service: Receiver<TimerResponse>,
    core_to_timer_service: Sender<TimerCommand>,

    core_from_heartbeat_service: Receiver<HeartbeatResponse>,
    core_to_heartbeat_service: Sender<HeartbeatCommand>,

    core_from_data_service: Receiver<DataServiceResponse>,
    core_to_data_service: Sender<DataServiceCommand>,

    core_from_health_service: Receiver<HealthServiceResponse>,
    core_to_health_service: Sender<HealthServiceCommand>,
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

    core_from_timer_service: Option<Receiver<TimerResponse>>,
    core_to_timer_service: Option<Sender<TimerCommand>>,

    core_from_heartbeat_service: Option<Receiver<HeartbeatResponse>>,
    core_to_heartbeat_service: Option<Sender<HeartbeatCommand>>,

    core_from_data_service: Option<Receiver<DataServiceResponse>>,
    core_to_data_service: Option<Sender<DataServiceCommand>>,

    core_from_health_service: Option<Receiver<HealthServiceResponse>>,
    core_to_health_service: Option<Sender<HealthServiceCommand>>,
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

    pub fn core_from_data_service(mut self, ch: Receiver<DataServiceResponse>) -> Self {
        self.core_from_data_service = Some(ch);
        self
    }
    pub fn core_to_data_service(mut self, ch: Sender<DataServiceCommand>) -> Self {
        self.core_to_data_service = Some(ch);
        self
    }

    pub fn core_from_health_service(mut self, ch: Receiver<HealthServiceResponse>) -> Self {
        self.core_from_health_service = Some(ch);
        self
    }
    pub fn core_to_health_service(mut self, ch: Sender<HealthServiceCommand>) -> Self {
        self.core_to_health_service = Some(ch);
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

            core_from_data_service: self
                .core_from_data_service
                .ok_or(anyhow!("falta: core_from_data_service"))?,
            core_to_data_service: self
                .core_to_data_service
                .ok_or(anyhow!("falta: core_to_data_service"))?,

            core_from_health_service: self
                .core_from_health_service
                .ok_or(anyhow!("falta: core_from_health_service"))?,
            core_to_health_service: self
                .core_to_health_service
                .ok_or(anyhow!("falta: core_to_health_service"))?,
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
            select! {
                res = self.core_from_fsm_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            FsmServiceResponse::LinkageProtocol => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::GenerateLinkageRequest) {
                                    error!("no se pudo enviar GenerateLinkageRequest desde core. {e}");
                                }
                            }
                            FsmServiceResponse::SubscribeInitialTopic => {
                                if let Err(e) = self.core_to_mqtt_service.try_send(MqttData::SubscribeInitial) {
                                    error!("no se pudo enviar SubscribeInitial desde core. {e}");
                                }
                            }
                            FsmServiceResponse::NotifyFirmware(version) => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::GenerateFirmwareOk(version)) {
                                    error!("no se pudo enviar GenerateFirmwareOk desde core. {e}");
                                }
                            }
                            FsmServiceResponse::CheckFirmware => {
                                if let Err(e) = self.core_to_ota_service.try_send(OtaCommand::CheckFirmware) {
                                        error!("no se pudo enviar CheckFirmware desde core. {e}");
                                }
                            }
                            FsmServiceResponse::InitSystem => {
                                if let Err(e) = self.core_to_mqtt_service.try_send(MqttData::SubscribeOperational) {
                                    error!("no se pudo enviar SubscribeOperational desde core. {e}");
                                }
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitSystemStart) {
                                        error!("no se pudo enviar InitSystemStart desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryStore(state) => {
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::None)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Store)) {
                                    error!("no se pudo enviar Store desde core. {e}");
                                }
                                match state {
                                    StateGeneral::InitSystem => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitSystemStop) {
                                                error!("no se pudo enviar InitSystemStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::InHandshake | StateGeneral::OutHandshake => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStop) {
                                                error!("no se pudo enviar HandshakeStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::InitBalance => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitBalanceStop) {
                                                error!("no se pudo enviar InitBalanceStop desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                            }
                            FsmServiceResponse::EntryCooling => {
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::CoolingStart) {
                                        error!("no se pudo enviar CoolingStart desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryUpdate => {
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::CoolingStop) {
                                        error!("no se pudo enviar CoolingStop desde core. {e}");
                                }
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::GeneratePing) {
                                        error!("no se pudo enviar GeneratePing desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryNormal(state) => {
                                match state {
                                    StateGeneral::InitSystem => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitSystemStop) {
                                            error!("no se pudo enviar InitSystemStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::Safe { frequency, jitter } => {
                                        if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::None)) {
                                            error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                        }
                                    }
                                    StateGeneral::OutHandshake => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStop) {
                                            error!("no se pudo enviar HandshakeStop desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::Normal)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Normal)) {
                                    error!("no se pudo enviar Normal desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntrySafe((state, frequency, jitter)) => {
                                match state {
                                    StateGeneral::InitSystem => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitSystemStop) {
                                            error!("no se pudo enviar InitSystemStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::InHandshake | StateGeneral::OutHandshake => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStop) {
                                            error!("no se pudo enviar HandshakeStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::InitBalance => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitBalanceStop) {
                                            error!("no se pudo enviar InitBalanceStop desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::Safe)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Safe { frequency, jitter })) {
                                        error!("no se pudo enviar Normal desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryBypass(state) => {
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::None)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                                match state {
                                    StateGeneral::Cooling => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::CoolingStop) {
                                            error!("no se pudo enviar CoolingStop desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::BypassStart) {
                                    error!("no se pudo enviar BypassStart desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryInitBalance(state, duration) => {
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::None)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                                match state {
                                    StateGeneral::InitSystem => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitSystemStop) {
                                            error!("no se pudo enviar InitSystemStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::Cooling => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::CoolingStop) {
                                            error!("no se pudo enviar CoolingStop desde core. {e}");
                                        }
                                    }
                                    StateGeneral::InHandshake | StateGeneral::OutHandshake => {
                                        if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStop) {
                                            error!("no se pudo enviar HandshakeStop desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitBalanceStart) {
                                        error!("no se pudo enviar InitSystemStop desde core. {e}");
                                }
                                if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::State(StateForHeartbeat::Balance)) {
                                    error!("no se pudo enviar StateForHeartbeat desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryAlert((frequency, jitter)) => {
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitBalanceStop) {
                                    error!("no se pudo enviar InitBalanceStop desde core. {e}");
                                }
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Alert { frequency, jitter } )) {
                                    error!("no se pudo enviar Alert desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryData((frequency, jitter)) => {
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Data { frequency, jitter } )) {
                                    error!("no se pudo enviar Data desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryMonitor((frequency, jitter)) => {
                                if let Err(e) = self.core_to_data_service.try_send(DataServiceCommand::State(StateGeneral::Monitor { frequency, jitter } )) {
                                    error!("no se pudo enviar Monitor desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryInHandshake => {
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::InitBalanceStop) {
                                    error!("no se pudo enviar InitBalanceStop desde core. {e}");
                                }
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStart) {
                                    error!("no se pudo enviar HandshakeStart desde core. {e}");
                                }
                            }
                            FsmServiceResponse::EntryOutHandshake => {
                                if let Err(e) = self.core_to_timer_service.try_send(TimerCommand::HandshakeStart) {
                                    error!("no se pudo enviar HandshakeStart desde core. {e}");
                                }
                            }
                            FsmServiceResponse::UpdateBalanceEpoch(epoch) => {
                                if let Err(e) = self.core_to_config_service.try_send(ConfigCommand::UpdateField(ConfigField::BalanceEpoch(epoch))) {
                                    error!("no se pudo enviar UpdateBalanceEpoch desde core. {e}");
                                }
                            }
                            FsmServiceResponse::UpdateLinkageFlag => {
                                if let Err(e) = self.core_to_config_service.try_send(ConfigCommand::UpdateField(ConfigField::LinkageFlag(true))) {
                                    error!("no se pudo enviar UpdateLinkageFlag desde core. {e}");
                                }
                            }
                        },
                        Err(e) => error!("el canal core_from_fsm_service se ha cerrado. {e}"),
                    }
                },

                res = self.core_from_msg_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            MessageServiceResponse::Serialized(msg) => {
                                if let Err(e) = self
                                    .core_to_mqtt_service
                                    .try_send(MqttData::OutMessage(msg))
                                {
                                    error!("no se pudo enviar OutMessage en core. {e}");
                                }
                            }
                            MessageServiceResponse::Message(msg) => match msg {
                                MessageFromEdge::FromServerSettings(msg) => {
                                    if let Err(e) = self.core_to_config_service.try_send(ConfigCommand::UpdateConfig(msg)) {
                                        error!("no se pudo enviar UpdateConfig desde core. {e}");
                                    }
                                }
                                MessageFromEdge::FromServerSettingsAck(msg) => {
                                    if let Err(e) = self.core_to_config_service.try_send(ConfigCommand::SettingsAck(msg)) {
                                        error!("no se pudo enviar SettingsAck desde core. {e}");
                                    }
                                }
                                MessageFromEdge::HandshakeToHub(msg) => {
                                    let epoch = settings.read().unwrap().balance_epoch();
                                    let edge = settings.read().unwrap().id_edge().to_string();
                                    if msg.balance_epoch >= epoch
                                        && msg.metadata.sender_user_id == edge.as_str()
                                    {
                                        if let Err(e) = self
                                            .core_to_fsm_service
                                            .try_send(FsmServiceCommand::Handshake(msg.flag))
                                        {
                                            error!("no se pudo enviar UpdateFirmware en core. {e}");
                                        }
                                    }
                                }
                                MessageFromEdge::Heartbeat(_) => {
                                    if let Err(e) = self.core_to_heartbeat_service.try_send(HeartbeatCommand::HeartbeatIncoming) {
                                        error!("no se pudo enviar HeartbeatIncoming desde core. {e}");
                                    }
                                }
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
                                MessageFromEdge::PhaseNotification(msg) => {
                                    if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::Phase((msg.epoch, msg.phase, msg.frequency, msg.jitter))) {
                                        error!("no se pudo enviar Phase desde core. {e}");
                                    }
                                }
                                MessageFromEdge::StateBalanceMode(msg) => {
                                     if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::Balance((msg.balance_epoch, msg.duration))) {
                                         error!("no se pudo enviar Balance desde core. {e}");
                                     }
                                }
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
                                            FsmServiceCommand::Safe((msg.frequency, msg.jitter)),
                                        ) {
                                            error!("no se pudo enviar Safe en core. {e}");
                                        }
                                    }
                                }
                                MessageFromEdge::UpdateFirmware(msg) => {
                                    let edge = settings.read().unwrap().id_edge().to_string();
                                    let network = settings.read().unwrap().id_network().to_string();
                                    let mac = settings.read().unwrap().mac_addr().to_string();
                                    if msg.metadata.sender_user_id == edge.as_str()
                                        && msg.network == network.as_str()
                                    {
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
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_config_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                             ConfigResponse::GenerateSettings(id) => {
                                 if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::GenerateSettings(id)) {
                                     error!("no se pudo enviar GenerateSettings desde core. {e}");
                                 }
                             }
                             ConfigResponse::GenerateSettingsAck(id) => {
                                 if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::GenerateSettingsAck(id)) {
                                     error!("no se pudo enviar GenerateSettingsAck desde core. {e}");
                                 }
                             }
                        },
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_mqtt_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            MqttData::Connected => {
                                // se podria implementar logica de enviar a healthservice y que repunte algunos points
                            }
                            MqttData::Disconnected => {
                                if let Err(e) = self.core_to_health_service.try_send(HealthServiceCommand::Disconnect) {
                                    error!("no se pudo enviar Disconnect desde core. {e}");
                                }
                            }
                            MqttData::InMessage(msg) => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::ParseMessage(msg)) {
                                    error!("no se pudo enviar ParseMessage desde core. {e}");
                                }
                            }
                            MqttData::PubAck { msg_id: u16, return_code: u8 } => {

                            }
                            MqttData::Health(health) => {
                                if let Err(e) = self.core_to_health_service.try_send(health) {
                                    error!("no se pudo enviar Health desde core. {e}");
                                }
                            }
                            _ => {}
                        },
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_ota_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
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
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_timer_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            TimerResponse::InitSystemReady => {

                            }
                            TimerResponse::CoolingReady => {

                            }
                            TimerResponse::BypassReady => {

                            }
                            TimerResponse::InitBalanceReady => {

                            }
                            TimerResponse::HandshakeReady => {

                            }
                        },
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_heartbeat_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            HeartbeatResponse::Disconnected => {
                                if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::EdgeDisconnected) {
                                    error!("no se pudo enviar EdgeDisconnected desde core. {e}");
                                }
                            }
                            _ => {}
                        },
                        Err(e) => error!("{e}"),
                    }
                },

                res = self.core_from_data_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            DataServiceResponse::Report { pulse_counter, pulse_max_duration, mq135_aqi, dht11_temp, dht11_hum } => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::Report { pulse_counter, pulse_max_duration, mq135_aqi, dht11_temp, dht11_hum }) {
                                    error!("no se pudo enviar Report desde core. {e}");
                                }
                            }
                            DataServiceResponse::AlertAir { initial_air_quality, actual_air_quality } => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::AlertAir { initial_air_quality, actual_air_quality }) {
                                    error!("no se pudo enviar AlertAir desde core. {e}");
                                }
                            }
                            DataServiceResponse::AlertTemp { initial_temp, actual_temp } => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::AlertTemp { initial_temp, actual_temp }) {
                                    error!("no se pudo enviar AlertTemp desde core. {e}");
                                }
                            }
                            DataServiceResponse::Monitor { timestamp, uptime_sec, heap_free, heap_min_free, heap_largest_block } => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::Monitor { timestamp, uptime_sec, heap_free, heap_min_free, heap_largest_block }) {
                                    error!("no se pudo enviar Monitor desde core. {e}");
                                }
                            }
                            DataServiceResponse::EmptyQueueSafe => {

                            }
                            DataServiceResponse::AnAlertWasGenerated => {
                                if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::AnAlertWasGenerated) {
                                    error!("no se pudo enviar AnAlertWasGenerated desde core. {e}");
                                }
                            }
                            DataServiceResponse::EmptyQueuePhase { state, phase } => {
                                if let Err(e) = self.core_to_msg_service.try_send(MessageServiceCommand::EmptyQueuePhase { state, phase }) {
                                    error!("no se pudo enviar EmptyQueueSafe desde core. {e}");
                                }
                            }
                            DataServiceResponse::BypassAlertAir { initial_air_quality, actual_air_quality } => {

                            }
                            DataServiceResponse::BypassAlertTemp { initial_temp, actual_temp } => {

                            }
                        },
                        Err(e) => error!("{e}"),
                    }
                },


                res = self.core_from_health_service.recv().fuse() => {
                    match res {
                        Ok(response) => match response {
                            HealthServiceResponse::StateChanged(health) => {
                                match health {
                                    HealthState::Healthy => {
                                        if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::HealthyConnection) {
                                            error!("no se pudo enviar HealthyConnection desde core. {e}");
                                        }
                                    }
                                    HealthState::Critical => {
                                        if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::CriticalConnection) {
                                            error!("no se pudo enviar CriticalConnection desde core. {e}");
                                        }
                                    }
                                    HealthState::Unavailable => {
                                        if let Err(e) = self.core_to_fsm_service.try_send(FsmServiceCommand::UnavailableConnection) {
                                            error!("no se pudo enviar UnavailableConnection desde core. {e}");
                                        }
                                    }
                                    _ => {}
                                }
                            }
                        },
                        Err(e) => error!("{e}"),
                    }
                },
            }
        }
    }
}
