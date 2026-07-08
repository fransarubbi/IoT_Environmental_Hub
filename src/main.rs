// Declaración de los módulos principales de la aplicación
pub mod hal;
pub mod svc;
pub mod bsp;
pub mod app;

use log::info;
use std::sync::Arc;
use std::thread::Builder;
use edge_executor::LocalExecutor;
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::task::thread::ThreadSpawnConfiguration;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::log::EspLogger;

use crate::app::channels::domain::Channels;
use crate::app::core::domain::*;
use crate::app::fsm::logic::FsmService;
use crate::app::message::domain::MessageService;
use crate::app::system_settings::logic::ConfigManager;



fn core0_executor_task() {
    let executor: LocalExecutor = Default::default();

    // Lanzar tareas asíncronas de WiFi y MQTT en este executor
    // executor.spawn(wifi_manager.run()).detach(); 

    // Ejecutar el executor de forma indefinida
    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));
}


fn core1_executor_task(
    core: Core,
    channels: Channels, 
    nvs_partition: EspDefaultNvsPartition
) {
    let executor: LocalExecutor = Default::default();

    // Como somos dueños de channels, podemos usar sus atributos directamente
    let res = ConfigManager::new(
        channels.config_manager_to_core, 
        channels.config_manager_from_core, 
        nvs_partition
    );

    if let Ok((config_manager, settings)) = res {
        executor.spawn(config_manager.run()).detach();

        executor.spawn(MessageService::new(
            channels.message_service_to_core, 
            channels.message_service_from_core, 
            Arc::clone(&settings)
        ).run(&executor)).detach();

        executor.spawn(core.run()).detach();

        executor.spawn(FsmService::new(
            channels.fsm_service_to_core, 
            channels.fsm_service_from_core
        ).run(&executor)).detach();
    }

    esp_idf_hal::task::block_on(executor.run(std::future::pending::<()>()));
}



/*
1. uart_init() 
2. wifi_init() 
3. time_init() 
4. mqtt_init() 
*/


fn main() -> anyhow::Result<()> {
    // Inicialización obligatoria del entorno ESP-IDF y parches de enlazado
    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();
    info!("Iniciando IoT Environmental Hub...");

    // Tomar el control de los periféricos
    let _peripherals = Peripherals::take()
        .map_err(|e| anyhow::anyhow!("no se pudieron tomar los periféricos: {:?}", e))?;
    let _sys_loop = EspSystemEventLoop::take()?;
    let nvs = EspDefaultNvsPartition::take()?;

    let channels = Channels::new(10);

    let core = Core::builder()
        .core_from_fsm_service(channels.core_from_fsm_service.clone())
        .core_to_fsm_service(channels.core_to_fsm_service.clone())
        .core_from_msg_service(channels.core_from_message_service.clone())
        .core_to_msg_service(channels.core_to_message_service.clone())
        .core_to_config_service(channels.core_to_config_manager.clone())
        .core_from_config_service(channels.core_from_config_manager.clone())
        .build()?;

    ThreadSpawnConfiguration {
        name: Some(c"wifi_and_mqtt_core"), 
        pin_to_core: Some(esp_idf_hal::cpu::Core::Core0), 
        priority: 10,                   
        ..Default::default()
    }.set()?;

    // El spawn sin argumentos está bien porque `core0_executor_task` se evalúa como puntero de función
    let _core0_thread = Builder::new()
        .stack_size(8192) 
        .spawn(core0_executor_task)?;

    ThreadSpawnConfiguration {
        name: Some(c"logic_core"),
        pin_to_core: Some(esp_idf_hal::cpu::Core::Core1), 
        priority: 5,      
        ..Default::default()
    }.set()?;


    let _core1_thread = Builder::new()
        .stack_size(8192) 
        .spawn(move || core1_executor_task(core, channels, nvs))?;

    // El hilo principal se bloquea esperando a hilos que nunca terminan.
    let _ = _core0_thread.join();
    let _ = _core1_thread.join();

    Ok(())
}