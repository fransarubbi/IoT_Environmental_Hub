use async_channel::{bounded, TryRecvError, TrySendError};
use rmp_serde::{from_slice, to_vec};
use serde::Serialize;
use log::{info, error, debug};
use crate::app::message::domain::*;
use crate::bsp::mqtt::IncomingMessage;


pub async fn parser(
    from_service_parser: Receiver<IncomingMessage>,
    tx: Sender<MessageServiceResponse>
) {
    while let Ok(msg) = from_service_parser.recv().await { 
        let topic = msg.topic;
        let payload = msg.payload;

        // Enrutar explícitamente según el sufijo del tópico
        let decoded: Option<MessageFromEdge> = if topic.ends_with("balance") {
            from_slice::<MessageStateBalanceMode>(&payload).ok().map(MessageFromEdge::StateBalanceMode)
        } else if topic.ends_with("normal") {
            from_slice::<MessageStateNormal>(&payload).ok().map(MessageFromEdge::StateNormal)
        } else if topic.ends_with("safe") {
            from_slice::<MessageStateSafeMode>(&payload).ok().map(MessageFromEdge::StateSafeMode)
        } else if topic.ends_with("phase") {
            from_slice::<PhaseNotification>(&payload).ok().map(MessageFromEdge::PhaseNotification)
        } else if topic.ends_with("handshake") {
            from_slice::<Handshake>(&payload).ok().map(MessageFromEdge::HandshakeToHub)
        } else if topic.ends_with("heartbeat") {
            from_slice::<Heartbeat>(&payload).ok().map(MessageFromEdge::Heartbeat)
        } else if topic.ends_with("new_firmware") {
            from_slice::<UpdateFirmware>(&payload).ok().map(MessageFromEdge::UpdateFirmware)
        } else if topic.ends_with("new_setting") {
            from_slice::<Settings>(&payload).ok().map(MessageFromEdge::FromServerSettings)
        } else if topic.ends_with("new_setting_ok") {
            from_slice::<SettingOk>(&payload).ok().map(MessageFromEdge::FromServerSettingsAck)
        } else if topic.ends_with("linkage_ack") {
            from_slice::<LinkageAck>(&payload).ok().map(MessageFromEdge::LinkageAck)
        } else {
            None
        };

        // Procesar el mensaje decodificado
        if let Some(decoded_msg) = decoded {
            if let Err(e) = tx.try_send(decoded_msg) {
                error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
            }
        } else {
            error!("no se pudo deserializar el mensaje del tópico: {}", topic);
        }
    }
}


async fn generator(
    from_service_generator: Receiver<MessageToEdge>,
    tx: Sender<MessageServiceResponse>,
    settings: Arc<SystemSettings>
) {
    while let Ok(msg) = from_service_generator.recv().await { 
        let topic = resolve_topic(settings, msg);
        match serialize(topic.0, topic.1, msg, topic.2) {
            Ok(msg) => {
                if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(msg)) {
                    error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                }
            }
            Err(e) => error!("no se pudo serializar mensaje. {e}")
        }
    }
}


async fn resolve_topic(
    settings: &SystemSettings,
    message: &MessageToEdge,
) -> (String, u8, bool) {
    match message {
        MessageToEdge::Report(_) => (
            settings.topic_data().topic.clone(),
            settings.topic_data().qos,
            settings.topic_data().retain
        ),
        MessageToEdge::Monitor(_) => (
            settings.topic_monitor().topic.clone(),
            settings.topic_monitor().qos,
            settings.topic_monitor().retain
        ),
        MessageToEdge::AlertAir(_) => (
            settings.topic_alert_air().topic.clone(),
            settings.topic_alert_air().qos,
            settings.topic_alert_air().retain
        ),
        MessageToEdge::AlertTem(_) => (
            settings.topic_alert_temp().topic.clone(),
            settings.topic_alert_temp().qos,
            settings.topic_alert_temp().retain
        ),
        MessageToEdge::HandshakeToEdge(_) => (
            settings.topic_handshake_to_edge().topic.clone(),
            settings.topic_handshake_to_edge().qos,
            settings.topic_handshake_to_edge().retain
        ),
        MessageToEdge::FirmwareOk(_) => (
            settings.topic_hub_firmware_ok().topic.clone(),
            settings.topic_hub_firmware_ok().qos,
            settings.topic_hub_firmware_ok().retain
        ),
        MessageToEdge::FromHubSettings(_) => (
            settings.topic_settings().topic.clone(),
            settings.topic_settings().qos,
            settings.topic_settings().retain
        ),  
        MessageToEdge::FromHubSettingsAck(_) => (
            settings.topic_settings_ok().topic.clone(),
            settings.topic_settings_ok().qos,
            settings.topic_settings_ok().retain
        ),
        MessageToEdge::EmptyQueue(_) => (
            settings.topic_empty_queue().topic.clone(),
            settings.topic_empty_queue().qos,
            settings.topic_empty_queue().retain
        ),
        MessageToEdge::EmptyQueueSafe(_) => (
            settings.topic_empty_queue().topic.clone(),
            settings.topic_empty_queue().qos,
            settings.topic_empty_queue().retain
        ),
        MessageToEdge::Ping(_) => (
            settings.topic_ping().topic.clone(),
            settings.topic_ping().qos,
            settings.topic_ping().retain
        ),
        MessageToEdge::LinkageRequest(_) => (
            settings.topic_linkage_request().topic.clone(),
            settings.topic_linkage_request().qos,
            settings.topic_linkage_request().retain
        ),
    }
}



/// Serializa cualquier estructura de dominio (T) a un arreglo de bytes utilizando **MessagePack**
/// y la empaqueta en un objeto [`SerializedMessage`] listo para ser procesado y publicado
/// por el cliente MQTT.
fn serialize<T>(
    topic: String,
    qos: u8,
    msg: T,
    retain: bool,
) -> anyhow::Result<SerializedMessage>
where
    T: Serialize,
{
    match to_vec(&msg) {
        Ok(payload) => {
            Ok((SerializedMessage::new(topic, payload, qos, retain)))
        }
        Err(e) => return Err(e),
    }
}