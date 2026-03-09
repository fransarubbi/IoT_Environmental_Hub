#include "System/system.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

app_queues_t queues;
app_event_group_t event_group;
app_static_mem_t mem;
app_task_handle_t task_handle;


void app_main(void) {

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 20000,      // 20 segundos
        .idle_core_mask = (1 << 0) | (1 << 1),  // Vigilar ambos núcleos
        .trigger_panic = true,    // Reiniciar si se bloquea
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));

    if (!init_queues()) esp_restart();          // Inicializar recursos de memoria (colas, eventos)
    if (!init_event_group()) esp_restart();;    // Inicializar event group
    if (!init_base_drivers()) esp_restart();;   // Inicializar drivers base (WiFi, UART, MQTT)
    wait_for_sensors();                         // Esperar a que los sensores esten listos (Bloqueante)
    start_application_tasks();                  // Arrancar las tareas del sistema
}