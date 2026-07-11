use esp_idf_svc::mqtt::client::QoS;
use heapless::String;

/// Trait que abstrae implementacion de MQTT
pub trait Mqtt {
    fn publish(
        &mut self,
        topic: &str,
        payload: &[u8],
        qos: QoS,
        retain: bool,
    ) -> Result<u16, String<100>>;
    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<u16, String<50>>;
    fn enable_subscriptions(&mut self);
}
