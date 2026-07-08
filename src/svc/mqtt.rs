use esp_idf_svc::mqtt::client::QoS;


/// Trait que abstrae implementacion de MQTT
pub trait Mqtt {
    fn publish(
        &mut self,
        topic: &str,
        payload: &[u8],
        qos: QoS,
        retain: bool,
    ) -> Result<u16, String>;
    fn subscribe(&mut self, topic: &str, qos: QoS) -> Result<u16, String>;
    fn enable_subscriptions(&mut self);
}
