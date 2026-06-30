/**
* @file heartbeat.c
 * @brief Implementación del sistema de vidas del Monitor de Vitalidad.
 */


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Heartbeat/heartbeat.h"
#include <esp_log.h>
#include "Fsm/fsm.h"
#include "System/system.h"


typedef struct {
    uint8_t count_beat;
    State old_state;
    State new_state;
} state_heartbeat;


static const char *TAG = "Heartbeat";


/**
 * @brief Lógica central de gestión de vidas.
 *
 * Esta función aplica las reglas del watchdog:
 * 1. Si la FSM cambió de estado, reinicia el contador a 2.
 * 2. Si es un TIMEOUT, resta una vida (sin bajar de 0).
 * 3. Si es un latido entrante (INCOMING), suma una vida (sin superar MAX_LIVES).
 * 4. Si las vidas llegan a 0, dispara el evento de fallo.
 *
 * @param heartbeat Puntero a la estructura de estado.
 * @param heart Tipo de evento recibido (TIMEOUT_HEARTBEAT o HEARTBEAT_INCOMING).
 */
static void check_beat(state_heartbeat *heartbeat, const uint32_t heart) {
    /*if (heartbeat->old_state != heartbeat->new_state) {
        heartbeat->old_state = heartbeat->new_state;
        heartbeat->count_beat = 2;
    }*/

    if (heart == TIMEOUT_HEARTBEAT) {
        if (heartbeat->count_beat > 0) {
            heartbeat->count_beat -= 1;
        }
        ESP_LOGW(TAG, "Warning: timeout detectado. Vidas restantes: %d", heartbeat->count_beat);
    }
    else if (heart == HEARTBEAT_INCOMING) {
        heartbeat->count_beat = 2;
        ESP_LOGI(TAG, "Info: latido recibido. Vidas restauradas");
    }

    if (heartbeat->count_beat == 0) {
        ESP_LOGE(TAG, "Error: enlace perdido (0 vidas). Notificando a FSM");
        const uint32_t flag = TIMEOUT_HEARTBEAT;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));

        heartbeat->count_beat = 2;
    }
}


/**
 * @brief Tarea principal (Loop infinito).
 *
 * Escucha la cola de eventos de heartbeat. Al recibir un evento:
 * 1. Consulta el estado actual de la FSM de forma atómica.
 * 2. Filtra la ejecución: Solo procesa latidos en estados específicos
 * (NORMAL, SAFE, BALANCE, etc.) ignorando estados transitorios.
 * 3. Invoca a check_beat() para actualizar el contador.
 *
 * @param pvParameter Parámetro de FreeRTOS.
 */
void heartbeat_task(void *pvParameter) {
    static uint32_t heart;
    static state_heartbeat heartbeat;
    uint32_t notification = 0;

    heartbeat.old_state = CHECK_FIRMWARE;
    heartbeat.count_beat = 2;

    while (1) {
        xTaskNotifyWait(0, NOTIFY_CMD_START, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            xQueueReset(queues.heartbeat);
            bool running = true;
            
            heartbeat.count_beat = 2; 

            while (running) {
                if (xQueueReceive(queues.heartbeat, &heart, pdMS_TO_TICKS(100)) == pdTRUE) {
                    heartbeat.new_state = atomic_load(&shared_state);
                    switch (heartbeat.new_state) {
                        case NORMAL:
                        case SAFE_MODE:
                        case ALERT:
                        case DATA:
                        case MONITOR:
                            check_beat(&heartbeat, heart);
                            break;
                        default: break;
                    }
                }
                
                uint32_t stop_signal = 0;
                const BaseType_t result = xTaskNotifyWait(0, NOTIFY_CMD_STOP, &stop_signal, 0);

                if (result == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        running = false;
                    }
                }
            }
        }
    }
}