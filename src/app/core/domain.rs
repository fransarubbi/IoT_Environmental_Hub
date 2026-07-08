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

use log::error;
use async_channel::{Sender, Receiver};
use embassy_futures::select::{select, Either};
use anyhow::{anyhow, Result};

use crate::app::fsm::logic::{FsmServiceCommand, FsmServiceResponse};
use crate::app::message::domain::{MessageServiceCommand, MessageServiceResponse};
use crate::app::system_settings::domain::{ConfigCommand, ConfigResponse};


pub struct Core {
    core_from_fsm_service: Receiver<FsmServiceResponse>,
    core_to_fsm_service: Sender<FsmServiceCommand>,
    
    core_from_msg_service: Receiver<MessageServiceResponse>,
    core_to_msg_service: Sender<MessageServiceCommand>,
    
    core_to_config_service: Sender<ConfigCommand>,
    core_from_config_service: Receiver<ConfigResponse>,
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


    /// Construye la instancia de `Core`.
    ///
    /// # Errores
    /// Retorna un `Err(String)` si falta configurar alguno de los canales.
    pub fn build(self) -> Result<Core> {
        Ok(Core {
            core_from_fsm_service: self.core_from_fsm_service.ok_or(anyhow!("falta: core_from_fsm_service"))?,
            core_to_fsm_service: self.core_to_fsm_service.ok_or(anyhow!("falta: core_to_fsm_service"))?,
            
            core_from_msg_service: self.core_from_msg_service.ok_or(anyhow!("falta: core_from_msg_service"))?,
            core_to_msg_service: self.core_to_msg_service.ok_or(anyhow!("falta: core_to_msg_service"))?,

            core_to_config_service: self.core_to_config_service.ok_or(anyhow!("falta: core_to_config_service"))?,
            core_from_config_service: self.core_from_config_service.ok_or(anyhow!("falta: core_from_config_service"))?,
        })
    }
}



impl Core {

    /// Crea un nuevo `CoreBuilder` inicializado con valores por defecto (None).
    pub fn builder() -> CoreBuilder {
        CoreBuilder::default()
    }

    pub async fn run(self) {

        loop {
            match select(
                self.core_from_fsm_service.recv(), 
                self.core_from_msg_service.recv()
            ).await {
                Either::First(Ok(response)) => {
                    match response {
                        FsmServiceResponse::InitCli => {
                            // enviar a uart?
                        }
                        FsmServiceResponse::InitWifi => {
                            // enviar a wifi?
                        }
                        FsmServiceResponse::InitMqtt => {
                            // enviar a mqtt?
                        }
                        FsmServiceResponse::LinkageProtocol => {
                            // enviar mensaje
                        }
                        FsmServiceResponse::NotifyFirmware => {
                            // enviar mensaje
                        }
                    }
                }
                Either::First(Err(e)) => {
                    error!("el canal core_from_fsm_service se ha cerrado. {e}");
                }
            
                Either::Second(Ok(response)) => {
                    match response {
                        _ => {}
                    }
                }
                Either::Second(Err(e)) => {
                    error!("el canal core_from_msg_service se ha cerrado. {e}");
                }
            }
        }
    }
}