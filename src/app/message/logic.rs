use crate::app::message::domain::*;
use crate::app::system_settings::domain::{EnergyMode, SystemSettings};
use crate::bsp::mqtt::IncomingMessage;
use crate::bsp::wifi::get_unix_epoch;
use anyhow::anyhow;
use async_channel::{Receiver, Sender};
use heapless::{String, Vec};
use log::{error, info};
use rmp_serde::{from_slice, to_vec_named as to_vec};
use serde::Serialize;
use std::sync::{Arc, RwLock};

/// Construye un heapless String<N> desde un &str, truncando si excede la capacidad.
macro_rules! hl_str {
    ($src:expr, $N:expr) => {{
        let src: &str = $src;
        let len = src.len().min($N);
        let mut s = String::<$N>::new();
        let _ = s.push_str(&src[..len]);
        s
    }};
}

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
        let topic = msg.topic.as_str();
        let payload = &msg.payload[..];

        if payload.is_empty() {
            log::debug!("Message. Payload vacío en {}, ignorando...", topic);
            continue;
        }

        // Enrutar explícitamente según el sufijo del tópico
        let decoded: Option<MessageFromEdge> = if topic.ends_with("balance") {
            info!("Message. Parseando mensaje de Balance.");
            from_slice::<MessageStateBalanceMode>(&payload)
                .ok()
                .map(MessageFromEdge::StateBalanceMode)
        } else if topic.ends_with("normal") {
            info!("Message. Parseando mensaje de Normal.");
            from_slice::<MessageStateNormal>(&payload)
                .ok()
                .map(MessageFromEdge::StateNormal)
        } else if topic.ends_with("safe") {
            info!("Message. Parseando mensaje de Safe.");
            from_slice::<MessageStateSafeMode>(&payload)
                .ok()
                .map(MessageFromEdge::StateSafeMode)
        } else if topic.ends_with("phase") {
            info!("Message. Parseando mensaje de Phase.");
            from_slice::<PhaseNotification>(&payload)
                .ok()
                .map(MessageFromEdge::PhaseNotification)
        } else if topic.ends_with("handshake") {
            info!("Message. Parseando mensaje de Handshake.");
            from_slice::<Handshake>(&payload)
                .ok()
                .map(MessageFromEdge::HandshakeToHub)
        } else if topic.ends_with("heartbeat") {
            info!("Message. Parseando mensaje de Heartbeat.");
            from_slice::<Heartbeat>(&payload)
                .ok()
                .map(MessageFromEdge::Heartbeat)
        } else if topic.ends_with("new_firmware") {
            info!("Message. Parseando mensaje de NewFirmware.");
            from_slice::<UpdateFirmware>(&payload)
                .ok()
                .map(MessageFromEdge::UpdateFirmware)
        } else if topic.ends_with("new_setting") {
            info!("Message. Parseando mensaje de NewSetting.");
            from_slice::<Settings>(&payload)
                .ok()
                .map(MessageFromEdge::FromServerSettings)
        } else if topic.ends_with("new_setting_ok") {
            info!("Message. Parseando mensaje de SettingOk.");
            from_slice::<SettingOk>(&payload)
                .ok()
                .map(MessageFromEdge::FromServerSettingsAck)
        } else if topic.ends_with("linkage_ack") {
            info!("Message. Parseando mensaje de LinkageAck.");
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
            MessageServiceCommand::Report {
                pulse_counter,
                pulse_max_duration,
                mq135_aqi,
                dht11_temp,
                dht11_hum,
            } => {
                info!("Message. Serializando mensaje de Measurement.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let sample = settings.read().unwrap().sample_rate();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = Measurement {
                    metadata,
                    network: hl_str!(&network, 20),
                    pulse_counter,
                    pulse_max_duration,
                    temperature: dht11_temp,
                    humidity: dht11_hum,
                    air_quality: mq135_aqi,
                    sample,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::Report(msg.clone()));
            }
            MessageServiceCommand::Monitor {
                timestamp,
                uptime_sec,
                heap_free,
                heap_min_free,
                heap_largest_block,
            } => {
                info!("Message. Serializando mensaje de Monitor.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp,
                };
                let msg = Monitor {
                    metadata,
                    network: hl_str!(&network, 20),
                    heap_free,
                    heap_min_free,
                    heap_largest_block,
                    uptime_sec,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::Monitor(msg.clone()));
            }
            MessageServiceCommand::AlertAir {
                initial_air_quality,
                actual_air_quality,
            } => {
                info!("Message. Serializando mensaje de AlertAir.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = AlertAir {
                    metadata,
                    network: hl_str!(&network, 20),
                    initial_air_quality,
                    actual_air_quality,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::AlertAir(msg.clone()));
            }
            MessageServiceCommand::AlertTemp {
                initial_temp,
                actual_temp,
            } => {
                info!("Message. Serializando mensaje de AlertTemp.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = AlertTh {
                    metadata,
                    network: hl_str!(&network, 20),
                    initial_temp,
                    actual_temp,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::AlertTem(msg.clone()));
            }
            MessageServiceCommand::GenerateFirmwareOk(str) => {
                info!("Message. Serializando mensaje de FirmwareOk.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
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
                info!("Message. Serializando mensaje de Settings.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let ssid = settings.read().unwrap().wifi_ssid().to_string();
                let password = settings.read().unwrap().wifi_password().to_string();
                let mqtt_uri = settings.read().unwrap().mqtt_uri().to_string();
                let dev_name = settings.read().unwrap().device_name().to_string();
                let sample = settings.read().unwrap().sample_rate();
                let energy_mode = match settings.read().unwrap().energy_mode() {
                    EnergyMode::LOW => 0,
                    EnergyMode::NORMAL => 1,
                    EnergyMode::PERFORMANCE => 2,
                };
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = Settings {
                    metadata,
                    message_id: id,
                    network: hl_str!(&network, NETWORK_STRING_LEN),
                    wifi_ssid: hl_str!(&ssid, WIFI_SSID_STRING_LEN),
                    wifi_password: hl_str!(&password, WIFI_PASSWORD_STRING_LEN),
                    mqtt_uri: hl_str!(&mqtt_uri, MQTT_URI_STRING_LEN),
                    device_name: hl_str!(&dev_name, DEVICE_NAME_STRING_LEN),
                    sample: sample,
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
                info!("Message. Serializando mensaje de SettingAck.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = SettingOk {
                    metadata,
                    message_id,
                    network: hl_str!(&network, NETWORK_STRING_LEN),
                    handshake: true,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::FromHubSettingsAck(msg.clone())
                );
            }
            MessageServiceCommand::EmptyQueuePhase { state, phase } => {
                info!("Message. Serializando mensaje de cola vacía de fase.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let edge = settings.read().unwrap().id_edge().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!(&edge, 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = EmptyQueue {
                    metadata,
                    state,
                    phase,
                    queue_empty: true,
                };
                send_serialized!(tx, &settings, msg, MessageToEdge::EmptyQueue(msg.clone()));
            }
            MessageServiceCommand::GenerateEmptyQueueSafe => {
                info!("Message. Serializando mensaje de cola vacía en safe.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let edge = settings.read().unwrap().id_edge().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!(&edge, 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = EmptyQueueSafeMode {
                    metadata,
                    state: hl_str!("safe_mode", 15),
                    queue_empty: true,
                };
                send_serialized!(
                    tx,
                    &settings,
                    msg,
                    MessageToEdge::EmptyQueueSafe(msg.clone())
                );
            }
            MessageServiceCommand::GenerateLinkageRequest => {
                info!("Message. Serializando mensaje de LinkageRequest.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let edge = settings.read().unwrap().id_edge().to_string();
                let dev_name = settings.read().unwrap().device_name().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!(&edge, 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = LinkageRequest {
                    metadata,
                    device_name: hl_str!(&dev_name, DEVICE_NAME_STRING_LEN),
                    network: hl_str!(&network, NETWORK_STRING_LEN),
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
                info!("Message. Serializando mensaje de Handshake.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let edge = settings.read().unwrap().id_edge().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!(&edge, 18),
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
            MessageServiceCommand::GenerateBypassAlertAir {
                initial_air_quality,
                actual_air_quality,
            } => {
                info!("Message. Serializando mensaje de alerta de aire bypass.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = AlertAir {
                    metadata,
                    network: hl_str!(&network, 20),
                    initial_air_quality,
                    actual_air_quality,
                };
                let topic = resolve_topic(&settings, &MessageToEdge::AlertAir(msg.clone()));
                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        if let Err(e) = tx.try_send(MessageServiceResponse::SerializedBypass(msg)) {
                            error!(
                                "no se pudo enviar mensaje SerializedBypass, mensaje descartado. {e}"
                            );
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
            }
            MessageServiceCommand::GenerateBypassAlertTemp {
                initial_temp,
                actual_temp,
            } => {
                info!("Message. Serializando mensaje de alerta de aire bypass.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = AlertTh {
                    metadata,
                    network: hl_str!(&network, 20),
                    initial_temp,
                    actual_temp,
                };
                let topic = resolve_topic(&settings, &MessageToEdge::AlertTem(msg.clone()));
                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        if let Err(e) = tx.try_send(MessageServiceResponse::SerializedBypass(msg)) {
                            error!(
                                "no se pudo enviar mensaje SerializedBypass, mensaje descartado. {e}"
                            );
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
            }
            _ => {}
        }
    }
}

fn resolve_topic(
    settings: &Arc<RwLock<SystemSettings>>,
    message: &MessageToEdge,
) -> (String<75>, u8, bool) {
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
fn serialize<T>(
    topic: String<75>,
    qos: u8,
    msg: T,
    retain: bool,
) -> anyhow::Result<SerializedMessage>
where
    T: Serialize,
{
    match to_vec(&msg) {
        Ok(std_payload) => {
            let mut payload = Vec::<u8, MAX_MSGPACK_BUFFER_SIZE>::new();
            payload
                .extend_from_slice(&std_payload)
                .map_err(|_| anyhow!("payload demasiado grande para el buffer heapless"))?;
            Ok(SerializedMessage::new(topic, payload, qos, retain))
        }
        Err(e) => Err(anyhow!("error serializando: {}", e)),
    }
}
