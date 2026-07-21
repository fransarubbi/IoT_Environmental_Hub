pub mod app;
pub mod bsp;
pub mod hal;
pub mod svc;

use core::fmt::Write;
use esp_idf_hal::sys::esp_efuse_mac_get_default;
use esp_idf_hal::{
    cpu::Core::{Core0, Core1},
    peripherals::Peripherals,
    task::thread::ThreadSpawnConfiguration,
};
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::gpio::AnyIOPin;
use esp_idf_svc::hal::uart::{UartConfig, UartDriver};
use esp_idf_svc::hal::units::Hertz;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use heapless::String;
use log::{error, info};
use std::sync::Arc;
use std::thread::Builder;

use crate::hal::uart::EspIdfUartManager;

use crate::bsp::cli::Cli;

use crate::app::{
    channels::domain::Channels,
    core::domain::*,
    system_settings::logic::ConfigManager,
    tasks::{core0_executor_task, core1_executor_task},
};

fn main() -> anyhow::Result<()> {
    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();

    info!("Iniciando IoT Environmental Hub...");

    let peripherals =
        Peripherals::take().map_err(|e| anyhow::anyhow!("fallo periféricos: {:?}", e))?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = EspDefaultNvsPartition::take()?;
    let modem = peripherals.modem;

    let root_ca: &'static [u8] = concat!(include_str!("../certs/root.crt"), "\0").as_bytes();
    let hub_cert: &'static [u8] = concat!(include_str!("../certs/hub.crt"), "\0").as_bytes();
    let hub_key: &'static [u8] = concat!(include_str!("../certs/hub.key"), "\0").as_bytes();

    let tx_pin = peripherals.pins.gpio17;
    let rx_pin = peripherals.pins.gpio16;

    let gpio4 = peripherals.pins.gpio4;
    let gpio5 = peripherals.pins.gpio5;
    let gpio34 = peripherals.pins.gpio34;
    let adc1 = peripherals.adc1;

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

    let channels = Channels::new(3);

    let core = Core::builder()
        .core_from_fsm_service(channels.core_from_fsm_service.clone())
        .core_to_fsm_service(channels.core_to_fsm_service.clone())
        .core_from_msg_service(channels.core_from_message_service.clone())
        .core_to_msg_service(channels.core_to_message_service.clone())
        .core_from_config_service(channels.core_from_config_manager.clone())
        .core_to_config_service(channels.core_to_config_manager.clone())
        .core_from_mqtt_service(channels.core_from_mqtt_service.clone())
        .core_to_mqtt_service(channels.core_to_mqtt_service.clone())
        .core_to_wifi_service(channels.core_to_wifi_service.clone())
        .core_from_ota_service(channels.core_from_ota_service.clone())
        .core_to_ota_service(channels.core_to_ota_service.clone())
        .core_from_timer_service(channels.core_from_timer_service.clone())
        .core_to_timer_service(channels.core_to_timer_service.clone())
        .core_from_heartbeat_service(channels.core_from_heartbeat_service.clone())
        .core_to_heartbeat_service(channels.core_to_heartbeat_service.clone())
        .core_from_data_service(channels.core_from_data_service.clone())
        .core_to_data_service(channels.core_to_data_service.clone())
        .build()?;

    let (mut config_manager, settings) = ConfigManager::new(
        channels.config_manager_to_core.clone(),
        channels.config_manager_from_core.clone(),
        nvs.clone(),
    )?;

    let mut cli = Cli::new(uart, Arc::clone(&settings));
    if cli.run(config_manager.has_data()) {
        let _ = config_manager
            .save_to_nvs()
            .map_err(|_| error!("fallo guardando en NVS"));
    }

    {
        let mut mac_bytes = [0u8; 6];
        unsafe {
            esp_efuse_mac_get_default(mac_bytes.as_mut_ptr());
        }
        let mut mac_address = String::<18>::new();

        write!(
            &mut mac_address,
            "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
            mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]
        )
        .unwrap();

        let mut cfg = settings.write().unwrap();
        cfg.set_mac_addr(mac_address);
        cfg.update_topics();
        info!("tópicos MQTT generados exitosamente.");
    }

    ThreadSpawnConfiguration {
        name: Some(c"wifi_and_mqtt_core"),
        pin_to_core: Some(Core0),
        priority: 10,
        ..Default::default()
    }
    .set()?;

    let channels0 = channels.clone();
    let settings0 = Arc::clone(&settings);
    let _core0_thread = Builder::new().stack_size(16384).spawn(move || {
        if let Err(e) = core0_executor_task(
            channels0, settings0, sys_loop, nvs, modem, hub_cert, hub_key, root_ca,
        ) {
            error!("Core 0 abortado: {:?}", e);
        }
    })?;

    ThreadSpawnConfiguration {
        name: Some(c"logic_core"),
        pin_to_core: Some(Core1),
        priority: 5,
        ..Default::default()
    }
    .set()?;

    let _core1_thread = Builder::new().stack_size(32768).spawn(move || {
        core1_executor_task(
            core,
            channels,
            config_manager,
            settings,
            gpio4,
            gpio5,
            gpio34,
            adc1,
        )
    })?;

    let _ = _core0_thread.join();
    let _ = _core1_thread.join();

    Ok(())
}
