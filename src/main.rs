// Declaración de los módulos principales de la aplicación
pub mod hal;
pub mod svc;
pub mod bsp;
pub mod app;

use std::sync::Arc;
use edge_executor::LocalExecutor;
use esp_idf_hal::prelude::Peripherals;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::log::EspLogger;
use async_channel::bounded;

// Importaciones de tus componentes internos
use crate::app::system_settings::domain::SystemSettings;
use crate::app::message::domain::{MessageService, MessageServiceCommand, MessageServiceResponse};



fn main() -> anyhow::Result<()> {

    /*
    Crear un solo LocalExecutor por núcleo de CPU. 
     */



    // 1. Inicialización obligatoria del entorno ESP-IDF y parches de enlazado
    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();
    log::info!("Iniciando IoT Environmental Hub...");

    // 2. Tomar el control de los periféricos de la capa de abstracción de hardware
    let peripherals = Peripherals::take()
        .map_err(|e| anyhow::anyhow!("No se pudieron tomar los periféricos: {:?}", e))?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = EspDefaultNvsPartition::take()?;


    // 3. Creación de la configuración global compartida (Singleton de lectura)
    // Aquí cargarías los datos reales usando tu ConfigManager o un valor inicializado por NVS
    let mut settings = SystemSettings::default();
    settings.set_wifi_ssid("Mi_Red_WiFi".to_string());
    settings.set_wifi_password("Contraseña123".to_string());
    settings.set_mqtt_uri("mqtts://broker.hivemq.com:8883".to_string());
    
    let shared_settings = Arc::new(settings);

    // 4. Creación de los Canales Globales de Comunicación (Bounded para evitar desbordamientos de memoria)
    let (msg_cmd_tx, msg_cmd_rx) = bounded::<MessageServiceCommand>(15);
    let (msg_res_tx, msg_res_rx) = bounded::<MessageServiceResponse>(15);

    // 5. Instanciación de Servicios y Managers de la capa BSP
    // Clonamos el Arc antes de pasarlo a los contextos donde se necesite
    let settings_for_mqtt = Arc::clone(&shared_settings);
    
    // NOTA: Para instanciar tu EspIdfMqttManager necesitarás pasarle los certificados y los canales creados.
    // Ejemplo ilustrativo de cómo se acoplarían tus tareas:
    //let msg_service = MessageService::new(msg_res_tx, msg_cmd_rx, shared_settings.clone());

    // 6. Configuración del Executor Asíncrono de un solo hilo (ideal para núcleos específicos)
    let executor = LocalExecutor::default();

    // 7. Spawning (Planificación) de las tareas en el Executor en segundo plano
    // Cada función asíncrona se ejecuta de forma cooperativa
    executor.spawn(msg_service.run(&executor))
        .map_err(|e| anyhow::anyhow!("Fallo al planificar Message Service Task: {:?}", e))?;

    // 8. Bucle infinito asíncrono en la tarea principal
    // esp_idf_hal::task::block_on mantiene vivo el hilo de FreeRTOS ejecutando el planificador.
    esp_idf_hal::task::block_on(executor.run(async {
        log::info!("Executor en marcha de manera asíncrona. Entrando en bucle de control.");
        loop {
            // USAR SIEMPRE un temporizador asíncrono para no congelar el executor.
            // Si usas delay síncronos de FreeRTOS o std::thread::sleep, romperás el paralelismo cooperativo.
            embassy_time::Timer::after(embassy_time::Duration::from_secs(60)).await;
            println!("[Main Thread] Heartbeat del sistema operativo cooperativo");
        }
    }));

    Ok(())
}