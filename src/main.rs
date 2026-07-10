// Declaración de los módulos principales de la aplicación
pub mod app;
pub mod bsp;
pub mod hal;
pub mod svc;

use edge_executor::LocalExecutor;
use esp_idf_hal::modem::Modem;
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::task::thread::ThreadSpawnConfiguration;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::gpio::AnyIOPin;
use esp_idf_svc::hal::uart::{UartConfig, UartDriver};
use esp_idf_svc::hal::units::Hertz;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::sntp::{EspSntp, SntpConf};
use log::{error, info};
use std::sync::{Arc, RwLock};
use std::thread::Builder;

use crate::app::channels::domain::Channels;
use crate::app::core::domain::*;
use crate::app::fsm::logic::FsmService;
use crate::app::heartbeat::domain::HeartbeatService;
use crate::app::message::domain::MessageService;
use crate::app::system_settings::domain::SystemSettings;
use crate::app::system_settings::logic::ConfigManager;
use crate::app::timer::logic::TimerService;
use crate::bsp::cli::Cli;
use crate::bsp::mqtt::EspIdfMqttManager;
use crate::bsp::ota::EspIdfOtaManager;
use crate::bsp::wifi::EspIdfWifiManager;
use crate::hal::uart::EspIdfUartManager;

fn core0_executor_task(
    channels: Channels,
    settings: Arc<RwLock<SystemSettings>>,
    sys_loop: EspSystemEventLoop,
    nvs: EspDefaultNvsPartition,
    modem: Modem,
    hub_cert: &[u8],
    hub_key: &[u8],
    root_ca: &[u8],
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

    executor.spawn(wifi.run()).detach();

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

    executor.spawn(mqtt.run()).detach();

    // Ejecutar el executor de forma indefinida
    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));

    Ok(())
}

fn core1_executor_task(
    core: Core,
    channels: Channels,
    config_manager: ConfigManager,
    settings: Arc<RwLock<SystemSettings>>,
) {
    let executor: LocalExecutor = Default::default();

    // Lanzamos el ConfigManager que construimos en el main
    executor.spawn(config_manager.run()).detach();

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

    executor.spawn(core.run(Arc::clone(&settings))).detach();

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
            FsmService::new(channels.fsm_service_to_core, channels.fsm_service_from_core)
                .run(&executor),
        )
        .detach();

    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));
}

fn main() -> anyhow::Result<()> {
    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();
    info!("iniciando IoT Environmental Hub...");

    // Tomar el control de los periféricos
    let peripherals = Peripherals::take()
        .map_err(|e| anyhow::anyhow!("no se pudieron tomar los periféricos: {:?}", e))?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = EspDefaultNvsPartition::take()?;
    let modem = peripherals.modem;

    let root_ca: &[u8] = include_bytes!("../certs/root.crt");
    let hub_cert: &[u8] = include_bytes!("../certs/hub.crt");
    let hub_key: &[u8] = include_bytes!("../certs/hub.key");

    let tx_pin = peripherals.pins.gpio17; // TX UART
    let rx_pin = peripherals.pins.gpio16; // RX UART

    let uart_config = UartConfig::new().baudrate(Hertz(115_200));

    let uart_driver = UartDriver::new(
        peripherals.uart1,
        tx_pin,
        rx_pin,
        Option::<AnyIOPin>::None,
        Option::<AnyIOPin>::None,
        &uart_config,
    )?;

    let uart = EspIdfUartManager::new(uart_driver);

    let channels = Channels::new(10);

    let core = Core::builder()
        .core_from_fsm_service(channels.core_from_fsm_service.clone())
        .core_to_fsm_service(channels.core_to_fsm_service.clone())
        .core_from_msg_service(channels.core_from_message_service.clone())
        .core_to_msg_service(channels.core_to_message_service.clone())
        .core_to_config_service(channels.core_to_config_manager.clone())
        .core_from_config_service(channels.core_from_config_manager.clone())
        .build()?;

    let (mut config_manager, settings) = ConfigManager::new(
        channels.config_manager_to_core.clone(),
        channels.config_manager_from_core.clone(),
        nvs.clone(),
    )?;

    let mut cli = Cli::new(uart, Arc::clone(&settings));
    let save = cli.run(config_manager.has_data());

    if save {
        match config_manager.save_to_nvs() {
            Ok(_) => {}
            Err(_) => error!("no se pudo guardar datos en NVS"),
        }
    }

    ThreadSpawnConfiguration {
        name: Some(c"wifi_and_mqtt_core"),
        pin_to_core: Some(esp_idf_hal::cpu::Core::Core0),
        priority: 10,
        ..Default::default()
    }
    .set()?;

    let channels0 = channels.clone();
    let settings0 = Arc::clone(&settings);

    let _core0_thread = Builder::new().stack_size(8192).spawn(move || {
        // Manejamos el error en caso de que core0_executor_task falle
        if let Err(e) = core0_executor_task(
            channels0, settings0, sys_loop, nvs, modem, hub_cert, hub_key, root_ca,
        ) {
            error!("core 0 finalizó con error crítico: {:?}", e);
        }
    })?;

    ThreadSpawnConfiguration {
        name: Some(c"logic_core"),
        pin_to_core: Some(esp_idf_hal::cpu::Core::Core1),
        priority: 5,
        ..Default::default()
    }
    .set()?;

    let _core1_thread = Builder::new()
        .stack_size(8192)
        .spawn(move || core1_executor_task(core, channels, config_manager, settings))?;

    let _ = _core0_thread.join();
    let _ = _core1_thread.join();

    Ok(())
}
