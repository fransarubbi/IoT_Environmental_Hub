/**
* @file converter.c
 * @brief Implementación de la lógica de despachador de eventos.
 */


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Converter/converter.h"
#include "System/system.h"
#include "Fsm/fsm.h"
#include "Message/message.h"
#include "MQTT/mqtt.h"
#include "Timer/timer.h"


/**
 * @brief Tarea principal de conversión.
 *
 * Ciclo infinito que:
 * 1. Bloquea esperando un flag en `queues.flag`.
 * 2. Lee atómicamente el estado global (`shared_state`).
 * 3. Verifica si el flag es relevante para ese estado en particular.
 * 4. Envía el evento resultante a `queues.event` para que la FSM lo procese.
 *
 * @note Esta arquitectura evita que la FSM tenga que conocer los detalles de
 * bajo nivel de quién generó la señal (Timer, MQTT, etc.), recibiendo solo
 * eventos semánticos (ej. 'eFromInitToNormal').
 *
 * @param pvParameters Parámetros de tarea (ignorado).
 */
void flag_converter_task(void *pvParameters) {
    uint32_t flag;

    while (1) {
        if (xQueueReceive(queues.flag, &flag, portMAX_DELAY) == pdTRUE) {
            const State state = atomic_load(&shared_state);

            switch (state) {
                case CHECK_FIRMWARE:
                    if (flag == 0) {
                        Event event = eNotUpdate;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == UPDATE_FLAG) {
                        Event event = eUpdate;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case INIT_SYSTEM:
                    if (flag == TIMEOUT_INIT) {
                        Event event = eFromInitToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_NORMAL) {
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case UPDATE:
                    if (flag == UPDATE_OK) {
                        Event event = eUpdateOk;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == 0) {
                        Event event = eUpdateError;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case INIT_BALANCE_MODE:
                    if (flag == TIMEOUT_HEARTBEAT || flag == TIMEOUT_BALANCE) {
                        Event event = eFromInitBalanceToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        Event event = eFromInitBalanceToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_ALERT) {
                        Event event = eFromInitBalanceToAlert;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_DATA) {
                        Event event = eFromInitBalanceToData;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_MONITOR) {
                        Event event = eFromInitBalanceToMonitor;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        Event event = eFromInitBalanceToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case IN_HANDSHAKE:
                    if (flag == TIMEOUT_HEARTBEAT || flag == TIMEOUT_BALANCE) {
                        Event event = eFromInHandshakeToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_ALERT) {
                        Event event = eFromInHandshakeToAlert;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        Event event = eFromInHandshakeToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case ALERT:
                    if (flag == TIMEOUT_HEARTBEAT) {
                        Event event = eFromAlertToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_DATA) {
                        Event event = eFromAlertToData;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case DATA:
                    if (flag == TIMEOUT_HEARTBEAT) {
                        Event event = eFromDataToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_MONITOR) {
                        Event event = eFromDataToMonitor;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == DATA_EMPTY_QUEUE) {
                        xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
                        xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
                        xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, DATA);
                        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
                    }
                    break;

                case MONITOR:
                    if (flag == TIMEOUT_HEARTBEAT) {
                        Event event = eFromMonitorToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MONITOR_EMPTY_QUEUE) {
                        xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
                        xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
                        xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
                        xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, MONITOR);
                        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
                    }
                    break;

                case OUT_HANDSHAKE:
                    if (flag == TIMEOUT_HEARTBEAT || flag == TIMEOUT_BALANCE) {
                        Event event = eFromOutHandshakeToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_NORMAL) {
                        Event event = eFromOutHandshakeToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        Event event = eFromOutHandshakeToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case NORMAL:
                    if (flag == STATE_BALANCE_MODE) {
                        Event event = eFromNormalToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT || flag == HEALTH_SCORE_CRITICAL
                        || flag == HEALTH_SCORE_UNAVAILABLE) {
                        Event event = eFromNormalToCooling;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case STORE:
                    if (flag == MESSAGE_ALERT) {
                        Event event = eFromStoreToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        Event event = eFromStoreToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case COOLING_TIME:
                    if (flag == TIMEOUT_COOLING) {
                        Event event = eFromCoolingToUpdateScore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MESSAGE_ALERT) {
                        Event event = eToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case UPDATE_SCORE:
                    if (flag == HEALTH_SCORE_DEGRADED) {
                        Event event = eFromUpdateScoreToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HEALTH_SCORE_UNAVAILABLE || flag == HEALTH_SCORE_CRITICAL) {
                        Event event = eFromUpdateScoreToCooling;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MESSAGE_ALERT) {
                        Event event = eToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case BYPASS:
                    if (flag == STATE_BALANCE_MODE) {
                        Event event = eFromBypassToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_BYPASS) {
                        Event event = eFromBypassToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case SAFE_MODE:
                    if (flag == STATE_NORMAL) {
                        Event event = eFromSafeToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == SAFE_MODE_EMPTY_QUEUE) {
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, SAFE_MODE);
                        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT) {
                        Event event = eFromSafeToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                default: {}
                    break;
            }
        }
    }
}
