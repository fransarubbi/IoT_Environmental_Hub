/**
* @file healthscore.c
 * @brief Implementación de la lógica de puntuación de salud de red.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Healthscore/healthscore.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include "Fsm/fsm.h"
#include "System/system.h"


/**
 * @brief Convierte el puntaje numérico (0-100) a un estado lógico.
 *
 * @param current_score Puntero al puntaje actual.
 * @return Estado de salud correspondiente (HEALTHY, DEGRADED, CRITICAL, UNAVAILABLE).
 */
static health_state_t get_state_of_score(const uint8_t *current_score) {
    if (80 <= *current_score && *current_score <= 100) {
        return HEALTHY;
    }
    if (55 <= *current_score && *current_score <= 79) {
        return DEGRADED;
    }
    if (20 <= *current_score && *current_score <= 54) {
        return CRITICAL;
    }
    if (0 < *current_score && *current_score <= 19) {
        return UNAVAILABLE;
    }
    return UNAVAILABLE;
}


/**
 * @brief Notifica a la FSM central sobre el estado actual de salud.
 *
 * Envía un flag a la cola de eventos del sistema solo si corresponde al estado actual.
 * Esta función se llama únicamente cuando se detecta un cambio de categoría en el score.
 *
 * @param current_score Puntero al puntaje actual.
 */
static void send_flag_when_score_changes(const uint8_t *current_score) {
    if (get_state_of_score(current_score) == HEALTHY) {
        uint32_t flag = HEALTH_SCORE_HEALTHY;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
    if (55 <= *current_score && *current_score <= 79) {
        uint32_t flag = HEALTH_SCORE_DEGRADED;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
    if (20 <= *current_score && *current_score <= 54) {
        uint32_t flag = HEALTH_SCORE_CRITICAL;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
    if (0 < *current_score && *current_score <= 19) {
        uint32_t flag = HEALTH_SCORE_UNAVAILABLE;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
}


/**
 * @brief Registra un nuevo mensaje QoS 1 para seguimiento de RTT.
 *
 * Busca un slot libre en el array de pendientes. Si no hay espacio, descarta el mensaje.
 *
 * @param msg_pending Puntero a la estructura de gestión de pendientes.
 * @param id ID del mensaje MQTT.
 * @param timestamp Momento del envío (us).
 */
static void add_pending_msg(pending_t *msg_pending, int32_t id, int64_t timestamp) {
    for (uint8_t i = 0; i < MAX_PENDING_MSGS; i++) {
        if (msg_pending->msg[i].active == false) {
            msg_pending->msg[i].msg_id = id;
            msg_pending->msg[i].start_time = timestamp;
            msg_pending->msg[i].active = true;
            return;
        }
    }
}


/**
 * @brief Busca un mensaje en el array de pendientes por su ID.
 *
 * @param msg_pending Puntero a la estructura de gestión.
 * @param id ID del mensaje a buscar (proveniente del PUBACK).
 * @return Índice en el array (0 a MAX-1) o -1 si no se encuentra.
 */
static int find_msg_index(pending_t *msg_pending, int32_t id) {
    for (uint8_t i = 0; i < MAX_PENDING_MSGS; i++) {
        if (msg_pending->msg[i].msg_id == id && msg_pending->msg[i].active == true) {
            return i;
        }
    }
    return -1;
}


/**
 * @brief Actualiza el puntaje basado en la latencia medida (RTT).
 *
 * Aplica bonificaciones por respuestas rápidas o penalizaciones por lentitud.
 * Detecta cambios de estado lógico y notifica a la FSM.
 *
 * @param current_score Puntero al score actual (entrada/salida).
 * @param rtt Latencia medida en milisegundos.
 */
static void update_score_rtt(uint8_t *current_score, const int64_t rtt) {
    const uint8_t old_score = *current_score;
    if (rtt >= 1000) {
        if (*current_score >= HIGH_LATENCY) {
            *current_score -= HIGH_LATENCY;
        }
    }
    if (500 <= rtt && rtt < 1000) {
        if (*current_score >= MEDIUM_LATENCY) {
            *current_score -= MEDIUM_LATENCY;
        }
    }
    if (rtt < 200) {
        if (*current_score <= 95) {
            *current_score += LOW_LATENCY;
        }
    }

    const health_state_t state_of_score = get_state_of_score(current_score);
    const health_state_t state_of_old_score = get_state_of_score(&old_score);
    if (state_of_score != state_of_old_score) {
        send_flag_when_score_changes(current_score);
    }
}


/**
 * @brief Penaliza el score por errores de socket o buffer lleno.
 */
static void update_score_socket_error(uint8_t *current_score) {
    const uint8_t old_score = *current_score;
    if (*current_score >= SOCKET_ERROR) {
        *current_score -= SOCKET_ERROR;
    }
    const health_state_t state_of_score = get_state_of_score(current_score);
    const health_state_t state_of_old_score = get_state_of_score(&old_score);
    if (state_of_score != state_of_old_score) {
        send_flag_when_score_changes(current_score);
    }
}


/**
 * @brief Resetea el score a 0 debido a una desconexión crítica.
 */
static void update_score_disconnected(uint8_t *current_score) {
    const uint8_t old_score = *current_score;
    *current_score = 0;
    const health_state_t state_of_score = get_state_of_score(current_score);
    const health_state_t state_of_old_score = get_state_of_score(&old_score);
    if (state_of_score != state_of_old_score) {
        send_flag_when_score_changes(current_score);
    }
}


/**
 * @brief Penaliza el score cuando un mensaje expira sin recibir ACK.
 */
static void update_score_timeout(uint8_t *current_score) {
    const uint8_t old_score = *current_score;
    if (*current_score >= TIMEOUT_QOS_1) {
        *current_score -= TIMEOUT_QOS_1;
    }
    const health_state_t state_of_score = get_state_of_score(current_score);
    const health_state_t state_of_old_score = get_state_of_score(&old_score);
    if (state_of_score != state_of_old_score) {
        send_flag_when_score_changes(current_score);
    }
}


/**
 * @brief Tarea principal del Health Score.
 * * Ciclo infinito que:
 * 1. Espera eventos en la cola (con timeout corto).
 * 2. Procesa eventos de MQTT (Envíos, ACKs, Errores).
 * 3. Realiza "Garbage Collection" de mensajes que excedieron el tiempo de espera.
 */
void health_score_task(void *pvParam) {

    static pending_t msg_pending;
    static uint8_t current_score = 100;
    static health_event_t evt;
    memset(&msg_pending, 0, sizeof(pending_t));

    while (1) {
        // Esperamos máximo 100ms. Si no llega nada, revisamos timeouts internos.
        if (xQueueReceive(queues.health, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (evt.event) {
                case HEALTH_EVT_MSG_SENT:   // Buscar slot libre y guardar
                    add_pending_msg(&msg_pending, evt.msg_id, evt.timestamp);
                    break;

                case HEALTH_EVT_PUBACK: {
                    // Buscar msg_id en array
                    const int idx = find_msg_index(&msg_pending, evt.msg_id);
                    if (idx != -1) {   // Calcular RTT
                        const int64_t rtt_us = evt.timestamp - msg_pending.msg[idx].start_time;
                        const int64_t rtt_ms = rtt_us / 1000;
                        update_score_rtt(&current_score, rtt_ms); // Pasamos ms
                        msg_pending.msg[idx].active = false; // Liberamos slot
                    }
                    break;
                }

                case HEALTH_EVT_ERROR_SEND:
                    update_score_socket_error(&current_score);
                    break;

                case HEALTH_EVT_DISCONNECT:  // Limpiar todos los mensajes pendientes porque ya no llegarán ACKs
                    update_score_disconnected(&current_score);
                    memset(&msg_pending, 0, sizeof(pending_t));
                    break;
            }
        }

        // Revisar si algún mensaje en pendiente lleva mucho tiempo sin ACK
        const int64_t now = esp_timer_get_time();
        for (uint8_t i = 0; i < MAX_PENDING_MSGS; i++) {
            // Solo chequeamos los activos
            if (msg_pending.msg[i].active) {
                if ((now - msg_pending.msg[i].start_time) > MSG_TIMEOUT_US) {
                    update_score_timeout(&current_score);
                    msg_pending.msg[i].active = false; // Liberamos slot
                }
            }
        }
    }
}