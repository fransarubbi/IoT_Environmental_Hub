use crate::bsp::wifi::get_unix_epoch;
use anyhow::anyhow;
use async_channel::{Receiver, Sender};
use heapless::{String, Vec};
use log::{error, info};
use rmp_serde::{from_slice, to_vec_named as to_vec};
use serde::Serialize;
use std::sync::{Arc, RwLock};

use crate::app::{
    message::domain::*,
    pool::pool::CORE_DATA_POOL,
    system_settings::domain::{EnergyMode, SystemSettings},
};

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

pub async fn parser(from_service_parser: Receiver<usize>, tx: Sender<MessageServiceResponse>) {
    while let Ok(idx) = from_service_parser.recv().await {
        {
            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
            match &slot.incoming {
                Some(msg) => {
                    let topic = msg.topic.as_str();
                    let payload = &msg.payload[..];

                    if payload.is_empty() {
                        info!("Message. Payload vacío en {}, ignorando...", topic);
                        continue;
                    }

                    // Enrutar explícitamente según el sufijo del tópico
                    let decoded: Option<MessageFromEdge> = if topic.ends_with("edge_state") {
                        info!("Message. Parseando mensaje de EdgeState.");
                        from_slice::<EdgeState>(&payload)
                            .ok()
                            .map(MessageFromEdge::State)
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
                        slot.from_edge = Some(decoded_msg);
                        if let Err(e) = tx.try_send(MessageServiceResponse::Message(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    } else {
                        error!("no se pudo deserializar el mensaje del tópico: {}", topic);
                    }
                }
                _ => info!("se recibió un mensaje Incoming en el parser que es incorrecto"),
            }
            slot.incoming = None;
        }
    }
}

pub async fn generator(
    from_service_generator: Receiver<MessageServiceCommand>,
    tx: Sender<MessageServiceResponse>,
    settings: Arc<RwLock<SystemSettings>>,
    free_idx_rx: Receiver<usize>,
) {
    while let Ok(cmd) = from_service_generator.recv().await {
        match cmd {
            MessageServiceCommand::Report {
                pulse_counter,
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
                    temperature: dht11_temp,
                    humidity: dht11_hum,
                    air_quality: mq135_aqi,
                    sample,
                };
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::SerializedBypass(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
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
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::SerializedBypass(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
            }
            MessageServiceCommand::GenerateHubState(state) => {
                info!("Message. Serializando mensaje de HubState.");
                let mac = settings.read().unwrap().mac_addr().to_string();
                let network = settings.read().unwrap().id_network().to_string();
                let metadata = Metadata {
                    sender_user_id: hl_str!(&mac, 18),
                    destination_id: hl_str!("server0", 18),
                    timestamp: get_unix_epoch(),
                };
                let msg = HubState {
                    metadata,
                    network: hl_str!(&network, 20),
                    state,
                };
                let topic = msg.resolve_topic(&settings);

                match serialize(topic.0, topic.1, msg, topic.2) {
                    Ok(msg) => {
                        let idx = free_idx_rx.recv().await.unwrap();
                        {
                            let mut slot = CORE_DATA_POOL[idx].lock().unwrap();
                            slot.serialized = Some(msg);
                        }
                        if let Err(e) = tx.try_send(MessageServiceResponse::Serialized(idx)) {
                            error!("no se pudo enviar mensaje Serialized, mensaje descartado. {e}");
                        }
                    }
                    Err(e) => error!("no se pudo serializar mensaje. {e}"),
                }
            }
            _ => {}
        }
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
