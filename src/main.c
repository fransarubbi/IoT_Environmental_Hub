#include "System/system.h"
#include "esp_system.h"

app_queues_t queues;
app_event_group_t event_group;
app_static_mem_t mem;
app_task_handle_t task_handle;


void app_main(void) {
    if (!init_queues()) esp_restart();   // Inicializar recursos de memoria (colas, eventos)
    if (!init_event_group()) esp_restart();;   // Inicializar event group
    if (!init_base_drivers()) esp_restart();;   // Inicializar drivers base (WiFi, UART, MQTT)
    wait_for_sensors();  // Esperar a que los sensores esten listos (Bloqueante)
    start_application_tasks();   // Arrancar las tareas del sistema
}