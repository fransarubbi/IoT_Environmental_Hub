//! Tareas principales que corren en cada núcleo del ESP32.
//!
//! - `core0_executor_task`: conectividad (WiFi + MQTT).
//! - `core1_executor_task`: lógica de negocio, sensores y servicios internos.

use edge_executor::LocalExecutor;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sntp::{EspSntp, SntpConf};
use std::sync::{Arc, RwLock};

use esp_idf_hal::{
    adc::{
        attenuation,
        oneshot::config::AdcChannelConfig,
        oneshot::{AdcChannelDriver, AdcDriver},
    },
    gpio::{PinDriver, Pull},
    modem::Modem,
};

use crate::bsp::http::EspIdfBypassManager;
use crate::hal::sensors::Sensor;
use crate::hal::sensors_drivers::{dht11::Dht11RmtDriver, ky037::Ky037, mq135::Mq135};

use crate::bsp::{mqtt::EspIdfMqttManager, ota::EspIdfOtaManager, wifi::EspIdfWifiManager};

use crate::app::{
    channels::domain::Channels, core::domain::*, data::logic::DataService, fsm::logic::FsmService,
    heartbeat::domain::HeartbeatService, message::domain::MessageService,
    system_settings::domain::SystemSettings, system_settings::logic::ConfigManager,
    timer::logic::TimerService,
};

/// Tarea del Core 0: levanta WiFi y MQTT y corre su executor local.
pub(crate) fn core0_executor_task(
    channels: Channels,
    settings: Arc<RwLock<SystemSettings>>,
    sys_loop: EspSystemEventLoop,
    nvs: EspDefaultNvsPartition,
    modem: Modem,
    hub_cert: &'static [u8],
    hub_key: &'static [u8],
    root_ca: &'static [u8],
) -> anyhow::Result<()> {
    let executor: LocalExecutor = Default::default();

    let wifi = EspIdfWifiManager::new(
        modem,
        sys_loop,
        nvs,
        &settings.read().unwrap().wifi_ssid(),
        &settings.read().unwrap().wifi_password(),
        channels.wifi_service_from_core,
    )
    .map_err(|e| anyhow::anyhow!("error al crear WiFi: {}", e))?;

    let _sntp = EspSntp::new(&SntpConf::default())?;

    let mqtt = EspIdfMqttManager::new(
        channels.mqtt_service_to_core,
        channels.mqtt_service_from_core,
        Arc::clone(&settings),
        hub_cert,
        hub_key,
        root_ca,
    )
    .map_err(|e| anyhow::anyhow!("error al crear MQTT: {}", e))?;

    executor.spawn(wifi.run()).detach();
    executor.spawn(mqtt.run()).detach();

    // Ejecutar el executor de forma indefinida
    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));

    Ok(())
}

/// Tarea del Core 1: inicializa los sensores y corre todos los servicios de lógica.
pub(crate) fn core1_executor_task(
    core: Core,
    channels: Channels,
    config_manager: ConfigManager,
    settings: Arc<RwLock<SystemSettings>>,
    gpio4: esp_idf_hal::gpio::Gpio4,
    gpio5: esp_idf_hal::gpio::Gpio5,
    gpio34: esp_idf_hal::gpio::Gpio34,
    adc1_periph: esp_idf_hal::adc::ADC1,
) {
    let http_client = {
        let guard = settings.read().unwrap();
        EspIdfBypassManager::new(guard.url_bypass())
    };

    let mut dht11 = Dht11RmtDriver::new(gpio4, "dht11").expect("fallo inicializando DHT11");
    dht11.init().unwrap();

    let ky_pin = PinDriver::input(gpio5, Pull::Floating).expect("fallo pin KY037");
    let mut ky037 = Ky037::new("ky037", ky_pin).expect("fallo inicializando KY037");
    ky037.init().unwrap();

    let adc1 = AdcDriver::new(adc1_periph).expect("fallo inicializando ADC1");
    let mq135_channel = AdcChannelDriver::new(
        &adc1,
        gpio34,
        &AdcChannelConfig {
            attenuation: attenuation::DB_12,
            calibration: esp_idf_hal::adc::oneshot::config::Calibration::Line,
            ..Default::default()
        },
    )
    .expect("fallo canal MQ135");

    let mut mq135 = Mq135::new("mq135", mq135_channel, 12.5, 0.2);
    mq135.init().unwrap();

    let executor: LocalExecutor = Default::default();

    executor.spawn(config_manager.run(&executor)).detach();
    executor.spawn(core.run(Arc::clone(&settings))).detach();

    executor
        .spawn(
            MessageService::new(
                channels.message_service_to_core,
                channels.message_service_from_core,
                Arc::clone(&settings),
            )
            .run(&executor),
        )
        .detach();
    executor
        .spawn(
            EspIdfOtaManager::new(channels.ota_service_to_core, channels.ota_service_from_core)
                .run(),
        )
        .detach();
    executor
        .spawn(
            TimerService::new(
                channels.timer_service_to_core,
                channels.timer_service_from_core,
            )
            .run(&executor, 10),
        )
        .detach();
    executor
        .spawn(
            HeartbeatService::new(
                channels.heartbeat_service_to_core,
                channels.heartbeat_service_from_core,
                Arc::clone(&settings),
            )
            .run(&executor),
        )
        .detach();

    executor
        .spawn(
            DataService::new(
                channels.data_service_to_core.clone(),
                channels.data_service_from_core.clone(),
                Arc::clone(&settings),
            )
            .run(&executor, dht11, mq135, ky037),
        )
        .detach();

    executor
        .spawn(
            FsmService::new(
                channels.fsm_service_to_core,
                channels.fsm_service_from_core,
                Arc::clone(&settings),
                http_client,
            )
            .run(&executor),
        )
        .detach();

    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));
}
