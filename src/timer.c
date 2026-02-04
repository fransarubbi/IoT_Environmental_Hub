/**
* @file timer.c
 * @brief Implementación del gestor de temporizadores.
 */

#include "freertos/FreeRTOS.h"
#include "Timer/timer.h"
#include <esp_log.h>
#include "esp_timer.h"
#include <stdint.h>
#include "Fsm/fsm.h"
#include "System/system.h"


static timers_t timers;


/**
 * @brief Busca una ranura libre en el array de timers.
 * @return Índice (0 a MAX-1) si encuentra espacio, o -1 si está lleno.
 */
static int find_empty_slot() {
    for (uint8_t i = 0; i < MAX_SIMULTANEOUS_TIMERS; i++) {
        if (timers.timers[i].type == NONE) return i;
    }
    return -1; // Lleno
}


/**
 * @brief Busca la ubicación de un timer existente por su tipo.
 * @param type El tipo de timer a buscar.
 * @return Índice del timer si existe, o -1 si no se encuentra.
 */
static int find_slot_by_type(const timer_types_t type) {
    for (uint8_t i = 0; i < MAX_SIMULTANEOUS_TIMERS; i++) {
        if (timers.timers[i].type == type) return i;
    }
    return -1;
}


/**
 * @brief Guarda el handle de un timer recién creado en una ranura libre.
 *
 * @param type Tipo de timer.
 * @param handle Handle devuelto por esp_timer_create.
 * @return true Si se guardó correctamente.
 * @return false Si no había espacio (el array estaba lleno).
 */
static bool save_handle_timer(const timer_types_t type, const esp_timer_handle_t handle) {
    const int slot = find_empty_slot();
    if (slot == -1) return false;

    timers.timers[slot].type = type;
    timers.timers[slot].handle = handle;

    return true;
}


/**
 * @brief Callback genérico ejecutado por esp_timer cuando expira un tiempo.
 *
 * Esta función se ejecuta en el contexto de alta prioridad del "Timer Task".
 * Convierte el argumento void* de vuelta a timer_types_t y envía el flag
 * correspondiente a la cola de flags del sistema.
 *
 * @param arg Argumento pasado al crear el timer (contiene el `timer_types_t` casteado).
 */
void timer_generic_callback(void *arg) {

    const timer_types_t timer = (timer_types_t)(uint64_t)arg;

    switch (timer) {
        case HEARTBEAT_NORMAL_TIMER: {
            const uint32_t flag = TIMEOUT_HEARTBEAT;
            xQueueSend(queues.heartbeat, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case HEARTBEAT_BALANCE_MODE_TIMER: {
            const uint32_t flag = TIMEOUT_HEARTBEAT;
            xQueueSend(queues.heartbeat, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case HEARTBEAT_SAFE_MODE_TIMER: {
            const uint32_t flag = TIMEOUT_HEARTBEAT;
            xQueueSend(queues.heartbeat, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case INIT_SYSTEM_TIMER: {
            const uint32_t flag = TIMEOUT_INIT;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case COOLING_TIMER: {
            const uint32_t flag = TIMEOUT_COOLING;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case BYPASS_TIMER: {
            const uint32_t flag = TIMEOUT_BYPASS;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case INIT_BALANCE_TIMER: {
            const uint32_t flag = TIMEOUT_BALANCE;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(0));
            break;
        }
        case HANDSHAKE_TIMER: {
            const uint32_t flag = TIMEOUT_BALANCE;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(0));
            break;
        }
        default: {}
    }
}


/**
 * @brief Inicializa y arranca un timer específico.
 *
 * Configura el timer según su tipo (periódico u one-shot y duración), lo crea
 * usando la API de esp_timer y lo almacena en una ranura libre del array interno.
 * Si no hay espacio, el timer se elimina inmediatamente para evitar fugas.
 *
 * @param type El tipo de timer a iniciar (de `timer_types_t`).
 */
void init_timer(const timer_types_t type) {

    esp_timer_handle_t timer_handle = NULL;
    uint32_t timeout = 0;
    bool periodic = false;

    switch (type) {
        case HEARTBEAT_NORMAL_TIMER: {
            timeout = TIMEOUT_HEARTBEAT_NORMAL;
            periodic = true;
            const esp_timer_create_args_t timer_normal = {
                .callback = &timer_generic_callback,
                .arg = (void*) HEARTBEAT_NORMAL_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "heartbeat_normal",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_normal, &timer_handle));
            break;
        }
        case HEARTBEAT_BALANCE_MODE_TIMER: {
            timeout = TIMEOUT_HEARTBEAT_BALANCE_MODE;
            periodic = true;
            const esp_timer_create_args_t timer_balance_mode = {
                .callback = &timer_generic_callback,
                .arg = (void*) HEARTBEAT_BALANCE_MODE_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "heartbeat_bm",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_balance_mode, &timer_handle));
            break;
        }
        case HEARTBEAT_SAFE_MODE_TIMER: {
            timeout = TIMEOUT_HEARTBEAT_SAFE_MODE;
            periodic = true;
            const esp_timer_create_args_t timer_safe_mode = {
                .callback = &timer_generic_callback,
                .arg = (void*) HEARTBEAT_SAFE_MODE_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "heartbeat_sm",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_safe_mode, &timer_handle));
            break;
        }
        case INIT_SYSTEM_TIMER: {
            timeout = TIMEOUT_INIT_SYSTEM;
            periodic = false;
            const esp_timer_create_args_t timer_init_system = {
                .callback = &timer_generic_callback,
                .arg = (void*) INIT_SYSTEM_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "init_sys",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_init_system, &timer_handle));
            break;
        }
        case COOLING_TIMER: {
            timeout = TIMEOUT_COOLING_TIMER;
            periodic = false;
            const esp_timer_create_args_t timer_cooling = {
                .callback = &timer_generic_callback,
                .arg = (void*) COOLING_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "cooling",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_cooling, &timer_handle));
            break;
        }
        case BYPASS_TIMER: {
            timeout = TIMEOUT_BYPASS_TIMER;
            periodic = false;
            const esp_timer_create_args_t timer_bypass = {
                .callback = &timer_generic_callback,
                .arg = (void*) BYPASS_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "bypass",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_bypass, &timer_handle));
            break;
        }
        case INIT_BALANCE_TIMER: {
            timeout = TIMEOUT_INIT_BALANCE_TIMER;
            periodic = false;
            const esp_timer_create_args_t timer_init_balance = {
                .callback = &timer_generic_callback,
                .arg = (void*) INIT_BALANCE_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "init_bm",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_init_balance, &timer_handle));
            break;
        }
        case HANDSHAKE_TIMER: {
            timeout = TIMEOUT_HANDSHAKE;
            periodic = false;
            const esp_timer_create_args_t timer_handshake = {
                .callback = &timer_generic_callback,
                .arg = (void*) HANDSHAKE_TIMER,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "handshake",
                .skip_unhandled_events = false
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_handshake, &timer_handle));
            break;
        }
        default: {}
    }

    if (periodic) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle, timeout));
    } else {
        ESP_ERROR_CHECK(esp_timer_start_once(timer_handle, timeout));
    }

    if (!save_handle_timer(type, timer_handle)) {
        esp_timer_delete(timer_handle);
    }
}


/**
 * @brief Detiene y elimina un timer de la memoria.
 *
 * Detiene el timer si está corriendo, libera los recursos de esp_timer
 * y marca la ranura interna como disponible (NONE).
 *
 * @param type El tipo de timer a eliminar.
 */
void delete_timer(const timer_types_t type) {
    const int slot = find_slot_by_type(type);
    if (slot != -1) {
        esp_timer_stop(timers.timers[slot].handle);
        esp_timer_delete(timers.timers[slot].handle);
        timers.timers[slot].type = NONE;
        timers.timers[slot].handle = NULL;
    }
}
