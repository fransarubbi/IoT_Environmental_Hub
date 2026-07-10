use crate::app::message::domain::*;
use crate::app::system_settings::domain::{EnergyMode, SystemSettings};
use crate::bsp::mqtt::IncomingMessage;
use crate::bsp::wifi::get_unix_epoch;
use anyhow::anyhow;
use async_channel::{Receiver, Sender};
use log::error;
use rmp_serde::{from_slice, to_vec};
use serde::Serialize;
use std::sync::{Arc, RwLock};

macro_rules! send_serialized {
    ($tx:expr, $settings:expr, $msg:expr, $variant:expr) => {{
        let topic = resolve_topic($settings, &$variant);
        match serialize(topic.0, topic.1, $msg, topic.2) {
            Ok(msg) => {
                if let Err(e) = $tx.try_send(MessageServiceResponse::Serialized(msg)) {
                    error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                }
            }
            Err(e) => error!("no se pudo serializar mensaje. {e}"),
        }
    }};
}

pub async fn parser(
    from_service_parser: Receiver<IncomingMessage>,
    tx: Sender<MessageServiceResponse>,
) {
    while let Ok(msg) = from_service_parser.recv().await {
        let topic = msg.topic;
        let payload = msg.payload;

        // Enrutar explícitamente según el sufijo del tópico
        let decoded: Option<MessageFromEdge> = if topic.ends_with("balance") {
            from_slice::<MessageStateBalanceMode>(&payload)
                .ok()
                .map(MessageFromEdge::StateBalanceMode)
        } else if topic.ends_with("normal") {
            from_slice::<MessageStateNormal>(&payload)
                .ok()
                .map(MessageFromEdge::StateNormal)
        } else if topic.ends_with("safe") {
            from_slice::<MessageStateSafeMode>(&payload)
                .ok()
                .map(MessageFromEdge::StateSafeMode)
        } else if topic.ends_with("phase") {
            from_slice::<PhaseNotification>(&payload)
                .ok()
                .map(MessageFromEdge::PhaseNotification)
        } else if topic.ends_with("handshake") {
            from_slice::<Handshake>(&payload)
                .ok()
                .map(MessageFromEdge::HandshakeToHub)
        } else if topic.ends_with("heartbeat") {
            from_slice::<Heartbeat>(&payload)
                .ok()
                .map(MessageFromEdge::Heartbeat)
        } else if topic.ends_with("new_firmware") {
            from_slice::<UpdateFirmware>(&payload)
                .ok()
                .map(MessageFromEdge::UpdateFirmware)
        } else if topic.ends_with("new_setting") {
            from_slice::<Settings>(&payload)
                .ok()
                .map(MessageFromEdge::FromServerSettings)
        } else if topic.ends_with("new_setting_ok") {
            from_slice::<SettingOk>(&payload)
                .ok()
                .map(MessageFromEdge::FromServerSettingsAck)
        } else if topic.ends_with("linkage_ack") {
            from_slice::<LinkageAck>(&payload)
                .ok()
                .map(MessageFromEdge::LinkageAck)
        } else {
            None
        };

        // Procesar el mensaje decodificado
        if let Some(decoded_msg) = decoded {
            if let Err(e) = tx.try_send(MessageServiceResponse::Message(decoded_msg)) {
                error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
            }
        } else {
            error!("no se pudo deserializar el mensaje del tópico: {}", topic);
        }
    }
}

pub async fn generator(
    from_service_generator: Receiver<MessageServiceCommand>,
    tx: Sender<MessageServiceResponse>,
    settings: Arc<RwLock<SystemSettings>>,
) {
    while let Ok(cmd) = from_service_generator.recv().await {
        match cmd {
            MessageServiceCommand::GenerateReport(msg) => {
                send_serialized!(tx, &settings, msg, MessageToEdge::Report(msg.clone()));
            }
            MessageServiceCommand::GenerateMonitor(msg) => {
                send_serialized!(tx, &settings, msg, MessageToEdge::Monitor(msg.clone()));
            }
            MessageServiceCommand::GenerateAlertAir(msg) => {
                send_serialized!(tx, &settings, msg, MessageToEdge::AlertAir(msg.clone()));
            }
            MessageServiceCommand::GenerateAlertTem(msg) => {
                send_serialized!(tx, &settings, msg, MessageToEdge::AlertTem(msg.clone()));
            }
            MessageServiceCommand::GenerateFirmwareOk(str) => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: "server0".to_string(),
                    timestamp: get_unix_epoch(),
                };
                let msg = FirmwareOk {
                    metadata,
                    version: str,
                    is_ok: true,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::FirmwareOk(msg.clone()));
            }
            MessageServiceCommand::GenerateSettings(id) => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: "server0".to_string(),
                    timestamp: get_unix_epoch(),
                };
                let energy_mode = match settings.read().unwrap().energy_mode() {
                    EnergyMode::LOW => 0,
                    EnergyMode::NORMAL => 1,
                    EnergyMode::PERFORMANCE => 2,
                };
                let msg = Settings {
                    metadata,
                    message_id: id,
                    network: settings.read().unwrap().id_network().to_string(),
                    wifi_ssid: settings.read().unwrap().wifi_ssid().to_string(),
                    wifi_password: settings.read().unwrap().wifi_password().to_string(),
                    mqtt_uri: settings.read().unwrap().mqtt_uri().to_string(),
                    device_name: settings.read().unwrap().device_name().to_string(),
                    sample: settings.read().unwrap().sample_rate() as u32,
                    energy_mode,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::FromHubSettings(msg.clone())
                );
            }
            MessageServiceCommand::GenerateSettingsAck(message_id) => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: "server0".to_string(),
                    timestamp: get_unix_epoch(),
                };
                let msg = SettingOk {
                    metadata,
                    message_id,
                    network: settings.read().unwrap().id_network().to_string(),
                    handshake: true,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::FromHubSettingsAck(msg.clone())
                );
            }
            MessageServiceCommand::GenerateEmptyQueue(msg) => {
                send_serialized!(tx, &settings, msg, MessageToEdge::EmptyQueue(msg.clone()));
            }
            MessageServiceCommand::GenerateEmptyQueueSafe(msg) => {
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::EmptyQueueSafe(msg.clone())
                );
            }
            MessageServiceCommand::GeneratePing => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: settings.read().unwrap().id_edge().to_string(),
                    timestamp: get_unix_epoch(),
                };
                let msg = Ping {
                    metadata,
                    network: settings.read().unwrap().id_network().to_string(),
                    ping: true,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::Ping(msg.clone()));
            }
            MessageServiceCommand::GenerateLinkageRequest => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: settings.read().unwrap().id_edge().to_string(),
                    timestamp: get_unix_epoch(),
                };
                let msg = LinkageRequest {
                    metadata,
                    device_name: settings.read().unwrap().device_name().to_string(),
                    network: settings.read().unwrap().id_network().to_string(),
                    linkage_request: true,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::LinkageRequest(msg.clone())
                );
            }
            MessageServiceCommand::GenerateHandshake((balance_epoch, flag)) => {
                let metadata = Metadata {
                    sender_user_id: settings.read().unwrap().mac_addr().to_string(),
                    destination_id: settings.read().unwrap().id_edge().to_string(),
                    timestamp: get_unix_epoch(),
                };
                let msg = Handshake {
                    metadata,
                    flag,
                    balance_epoch,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::HandshakeToEdge(msg.clone())
                );
            }
            _ => {}
        }
    }
}

fn resolve_topic(
    settings: &Arc<RwLock<SystemSettings>>,
    message: &MessageToEdge,
) -> (String, u8, bool) {
    match message {
        MessageToEdge::Report(_) => (
            settings.read().unwrap().topic_data().topic.clone(),
            settings.read().unwrap().topic_data().qos,
            settings.read().unwrap().topic_data().retain,
        ),
        MessageToEdge::Monitor(_) => (
            settings.read().unwrap().topic_monitor().topic.clone(),
            settings.read().unwrap().topic_monitor().qos,
            settings.read().unwrap().topic_monitor().retain,
        ),
        MessageToEdge::AlertAir(_) => (
            settings.read().unwrap().topic_alert_air().topic.clone(),
            settings.read().unwrap().topic_alert_air().qos,
            settings.read().unwrap().topic_alert_air().retain,
        ),
        MessageToEdge::AlertTem(_) => (
            settings.read().unwrap().topic_alert_temp().topic.clone(),
            settings.read().unwrap().topic_alert_temp().qos,
            settings.read().unwrap().topic_alert_temp().retain,
        ),
        MessageToEdge::HandshakeToEdge(_) => (
            settings
                .read()
                .unwrap()
                .topic_handshake_to_edge()
                .topic
                .clone(),
            settings.read().unwrap().topic_handshake_to_edge().qos,
            settings.read().unwrap().topic_handshake_to_edge().retain,
        ),
        MessageToEdge::FirmwareOk(_) => (
            settings
                .read()
                .unwrap()
                .topic_hub_firmware_ok()
                .topic
                .clone(),
            settings.read().unwrap().topic_hub_firmware_ok().qos,
            settings.read().unwrap().topic_hub_firmware_ok().retain,
        ),
        MessageToEdge::FromHubSettings(_) => (
            settings.read().unwrap().topic_settings().topic.clone(),
            settings.read().unwrap().topic_settings().qos,
            settings.read().unwrap().topic_settings().retain,
        ),
        MessageToEdge::FromHubSettingsAck(_) => (
            settings.read().unwrap().topic_settings_ok().topic.clone(),
            settings.read().unwrap().topic_settings_ok().qos,
            settings.read().unwrap().topic_settings_ok().retain,
        ),
        MessageToEdge::EmptyQueue(_) => (
            settings.read().unwrap().topic_empty_queue().topic.clone(),
            settings.read().unwrap().topic_empty_queue().qos,
            settings.read().unwrap().topic_empty_queue().retain,
        ),
        MessageToEdge::EmptyQueueSafe(_) => (
            settings.read().unwrap().topic_empty_queue().topic.clone(),
            settings.read().unwrap().topic_empty_queue().qos,
            settings.read().unwrap().topic_empty_queue().retain,
        ),
        MessageToEdge::Ping(_) => (
            settings.read().unwrap().topic_ping().topic.clone(),
            settings.read().unwrap().topic_ping().qos,
            settings.read().unwrap().topic_ping().retain,
        ),
        MessageToEdge::LinkageRequest(_) => (
            settings
                .read()
                .unwrap()
                .topic_linkage_request()
                .topic
                .clone(),
            settings.read().unwrap().topic_linkage_request().qos,
            settings.read().unwrap().topic_linkage_request().retain,
        ),
    }
}

/// Serializa cualquier estructura de dominio (T) a un arreglo de bytes utilizando **MessagePack**
/// y la empaqueta en un objeto [`SerializedMessage`] listo para ser procesado y publicado
/// por el cliente MQTT.
fn serialize<T>(topic: String, qos: u8, msg: T, retain: bool) -> anyhow::Result<SerializedMessage>
where
    T: Serialize,
{
    match to_vec(&msg) {
        Ok(payload) => Ok(SerializedMessage::new(topic, payload, qos, retain)),
        Err(e) => Err(anyhow!("error serializando: {}", e)),
    }
}
