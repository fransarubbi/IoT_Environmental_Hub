//! # Módulo de Canales de Comunicación (Wiring)
//!
//! Este módulo centraliza la creación y gestión de todos los canales
//! utilizados para la comunicación interna del sistema.
//!
//! Esta estructura actúa como una "placa base" (motherboard) virtual que crea
//! todos los pares `(Sender, Receiver)` necesarios antes de inyectarlos en sus tareas.
//!
//! ## Convención de Nomenclatura
//! Para evitar confusiones sobre la dirección del flujo de datos, los canales siguen un
//! estricto patrón de nombres:
//! * `[origen]_to_[destino]`: Representa el extremo emisor (`Sender`).
//! * `[destino]_from_[origen]`: Representa el extremo receptor (`Receiver`).
//!
//! Los canales se agrupan en pares bidireccionales por cada subsistema (excepto monitor,
//! que es unidireccional por naturaleza).

use async_channel::{Receiver, Sender, bounded};
use log::info;

use crate::app::{
    data::domain::{DataServiceCommand, DataServiceResponse},
    fsm::logic::{FsmServiceCommand, FsmServiceResponse},
    heartbeat::domain::{HeartbeatCommand, HeartbeatResponse},
    message::domain::{MessageServiceCommand, MessageServiceResponse},
    pool::pool::CORE_POOL_SIZE,
    system_settings::domain::{ConfigCommand, ConfigResponse},
    timer::logic::{TimerCommand, TimerResponse},
};

use crate::bsp::{
    mqtt::MqttData,
    ota::{OtaCommand, OtaResponse},
    wifi::WifiCommand,
};

/// Contenedor maestro de todos los canales MPSC del sistema.
///
/// Esta estructura almacena temporalmente los extremos de transmisión y recepción
/// de cada servicio. Durante la fase de inicialización de la aplicación (el "wiring"),
/// esta estructura se consume, y cada campo es movido (moved) a su respectiva
/// tarea (ej. el `Core` toma todos los campos `core_*`, mientras que cada servicio
/// toma sus respectivos `*_to_core` y `*_from_core`).
#[derive(Clone)]
pub struct Channels {
    pub fsm_service_to_core: Sender<FsmServiceResponse>,
    pub core_from_fsm_service: Receiver<FsmServiceResponse>,
    pub core_to_fsm_service: Sender<FsmServiceCommand>,
    pub fsm_service_from_core: Receiver<FsmServiceCommand>,

    pub message_service_to_core: Sender<MessageServiceResponse>,
    pub core_from_message_service: Receiver<MessageServiceResponse>,
    pub core_to_message_service: Sender<MessageServiceCommand>,
    pub message_service_from_core: Receiver<MessageServiceCommand>,

    pub core_to_config_manager: Sender<ConfigCommand>,
    pub config_manager_from_core: Receiver<ConfigCommand>,
    pub config_manager_to_core: Sender<ConfigResponse>,
    pub core_from_config_manager: Receiver<ConfigResponse>,

    pub core_to_wifi_service: Sender<WifiCommand>,
    pub wifi_service_from_core: Receiver<WifiCommand>,

    pub mqtt_service_to_core: Sender<MqttData>,
    pub core_from_mqtt_service: Receiver<MqttData>,
    pub core_to_mqtt_service: Sender<MqttData>,
    pub mqtt_service_from_core: Receiver<MqttData>,

    pub ota_service_to_core: Sender<OtaResponse>,
    pub core_from_ota_service: Receiver<OtaResponse>,
    pub core_to_ota_service: Sender<OtaCommand>,
    pub ota_service_from_core: Receiver<OtaCommand>,

    pub timer_service_to_core: Sender<TimerResponse>,
    pub core_from_timer_service: Receiver<TimerResponse>,
    pub core_to_timer_service: Sender<TimerCommand>,
    pub timer_service_from_core: Receiver<TimerCommand>,

    pub heartbeat_service_to_core: Sender<HeartbeatResponse>,
    pub core_from_heartbeat_service: Receiver<HeartbeatResponse>,
    pub core_to_heartbeat_service: Sender<HeartbeatCommand>,
    pub heartbeat_service_from_core: Receiver<HeartbeatCommand>,

    pub data_service_to_core: Sender<DataServiceResponse>,
    pub core_from_data_service: Receiver<DataServiceResponse>,
    pub core_to_data_service: Sender<DataServiceCommand>,
    pub data_service_from_core: Receiver<DataServiceCommand>,

    pub free_pool_index_tx: Sender<usize>,
    pub free_pool_index_rx: Receiver<usize>,
}

impl Channels {
    /// Inicializa y enlaza todos los canales requeridos por el sistema.
    ///
    /// Esta función agrupa la creación repetitiva de canales, asegurando que todos
    /// se instancien con la misma política de encolamiento. Utiliza canales "bounded" (limitados)
    /// para prevenir el agotamiento de memoria en caso de cuellos de botella.
    ///
    /// # Argumentos
    ///
    /// * `buffer_size` - Capacidad máxima de mensajes en espera para CADA canal.
    ///   Si la cola se llena, la tarea que intente hacer `try_send().await` se bloqueará
    ///   (aplicando backpressure) hasta que el receptor consuma mensajes.
    ///
    /// # Retorno
    /// Retorna una instancia completa de `Channels` con todos los extremos conectados.
    pub fn new(buffer_size: usize) -> Self {
        info!("creando canales del sistema...");

        // FSM
        let (fsm_s2c_tx, fsm_s2c_rx) = bounded(buffer_size);
        let (fsm_c2s_tx, fsm_c2s_rx) = bounded(buffer_size);

        // Message
        let (msg_s2c_tx, msg_s2c_rx) = bounded(buffer_size);
        let (msg_c2s_tx, msg_c2s_rx) = bounded(buffer_size);

        // Settings
        let (config_c2s_tx, config_c2s_rx) = bounded(buffer_size);
        let (config_s2c_tx, config_s2c_rx) = bounded(buffer_size);

        // Wifi
        let (wifi_c2s_tx, wifi_c2s_rx) = bounded(buffer_size);

        // MQTT
        let (mqtt_s2c_tx, mqtt_s2c_rx) = bounded(buffer_size);
        let (mqtt_c2s_tx, mqtt_c2s_rx) = bounded(buffer_size);

        // OTA
        let (ota_s2c_tx, ota_s2c_rx) = bounded(buffer_size);
        let (ota_c2s_tx, ota_c2s_rx) = bounded(buffer_size);

        // Timer
        let (timer_s2c_tx, timer_s2c_rx) = bounded(buffer_size);
        let (timer_c2s_tx, timer_c2s_rx) = bounded(buffer_size);

        // Heartbeat
        let (heartbeat_s2c_tx, heartbeat_s2c_rx) = bounded(buffer_size);
        let (heartbeat_c2s_tx, heartbeat_c2s_rx) = bounded(buffer_size);

        // Data
        let (data_s2c_tx, data_s2c_rx) = bounded(buffer_size);
        let (data_c2s_tx, data_c2s_rx) = bounded(buffer_size);

        // Pool
        let (free_tx, free_rx) = bounded(CORE_POOL_SIZE + 1);

        for i in 0..CORE_POOL_SIZE {
            free_tx.try_send(i).unwrap();
        }

        Self {
            fsm_service_to_core: fsm_s2c_tx,
            core_from_fsm_service: fsm_s2c_rx,
            core_to_fsm_service: fsm_c2s_tx,
            fsm_service_from_core: fsm_c2s_rx,

            message_service_to_core: msg_s2c_tx,
            core_from_message_service: msg_s2c_rx,
            core_to_message_service: msg_c2s_tx,
            message_service_from_core: msg_c2s_rx,

            core_to_config_manager: config_c2s_tx,
            config_manager_from_core: config_c2s_rx,
            config_manager_to_core: config_s2c_tx,
            core_from_config_manager: config_s2c_rx,

            core_to_wifi_service: wifi_c2s_tx,
            wifi_service_from_core: wifi_c2s_rx,

            mqtt_service_to_core: mqtt_s2c_tx,
            core_from_mqtt_service: mqtt_s2c_rx,
            core_to_mqtt_service: mqtt_c2s_tx,
            mqtt_service_from_core: mqtt_c2s_rx,

            ota_service_to_core: ota_s2c_tx,
            core_from_ota_service: ota_s2c_rx,
            core_to_ota_service: ota_c2s_tx,
            ota_service_from_core: ota_c2s_rx,

            timer_service_to_core: timer_s2c_tx,
            core_from_timer_service: timer_s2c_rx,
            core_to_timer_service: timer_c2s_tx,
            timer_service_from_core: timer_c2s_rx,

            heartbeat_service_to_core: heartbeat_s2c_tx,
            core_from_heartbeat_service: heartbeat_s2c_rx,
            core_to_heartbeat_service: heartbeat_c2s_tx,
            heartbeat_service_from_core: heartbeat_c2s_rx,

            data_service_to_core: data_s2c_tx,
            core_from_data_service: data_s2c_rx,
            core_to_data_service: data_c2s_tx,
            data_service_from_core: data_c2s_rx,

            free_pool_index_tx: free_tx,
            free_pool_index_rx: free_rx,
        }
    }
}
