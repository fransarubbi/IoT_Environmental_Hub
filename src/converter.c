/**
* @file converter.c
 * @brief Implementación de la lógica de despachador de eventos.
 */


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Converter/converter.h"
#include <esp_log.h>
#include "System/system.h"
#include "Fsm/fsm.h"
#include "Message/message.h"
#include "MQTT/mqtt.h"
#include "Timer/timer.h"


static const char *TAG = "Converter";


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
                        ESP_LOGD(TAG, "Estado: CHECK_FIRMWARE, Flag: 0");
                        Event event = eNotUpdate;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == UPDATE_FLAG) {
                        ESP_LOGD(TAG, "Estado: CHECK_FIRMWARE, Flag: UPDATE_FLAG");
                        Event event = eUpdate;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case LINKAGE:
                    if (flag == LINKAGE_OK) {
                        ESP_LOGD(TAG, "Estado: LINKAGE, Flag: LINKAGE_OK");
                        Event event = eLinkageOk;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case INIT_SYSTEM:
                    if (flag == TIMEOUT_INIT) {
                        ESP_LOGD(TAG, "Estado: INIT_SYSTEM, Flag: TIMEOUT_INIT");
                        Event event = eFromInitToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        ESP_LOGD(TAG, "Estado: INIT_SYSTEM, Flag: STATE_SAFE_MODE");
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: INIT_SYSTEM, Flag: STATE_BALANCE_MODE");
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_NORMAL) {
                        ESP_LOGD(TAG, "Estado: INIT_SYSTEM, Flag: STATE_NORMAL");
                        delete_timer(INIT_SYSTEM_TIMER);
                        Event event = eFromInitToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case UPDATE:
                    if (flag == UPDATE_OK) {
                        ESP_LOGD(TAG, "Estado: UPDATE, Flag: UPDATE_OK");
                        Event event = eUpdateOk;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == 0) {
                        ESP_LOGD(TAG, "Estado: UPDATE, Flag: 0");
                        Event event = eUpdateError;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case INIT_BALANCE_MODE:
                    if (flag == TIMEOUT_BALANCE) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: TIMEOUT_BALANCE");
                        Event event = eFromInitBalanceToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromInitBalanceToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_ALERT) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: PHASE_ALERT");
                        Event event = eFromInitBalanceToAlert;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_DATA) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: PHASE_DATA");
                        Event event = eFromInitBalanceToData;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_MONITOR) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: PHASE_MONITOR");
                        Event event = eFromInitBalanceToMonitor;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        ESP_LOGD(TAG, "Estado: INIT_BALANCE_MODE, Flag: STATE_SAFE_MODE");
                        Event event = eFromInitBalanceToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case IN_HANDSHAKE:
                    if (flag == HANDSHAKE_REQUEST) {
                        Event event = eRepeatHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_BALANCE) {
                        ESP_LOGD(TAG, "Estado: IN_HANDSHAKE, Flag: TIMEOUT_BALANCE");
                        Event event = eFromInHandshakeToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_ALERT) {
                        ESP_LOGD(TAG, "Estado: IN_HANDSHAKE, Flag: PHASE_ALERT");
                        Event event = eFromInHandshakeToAlert;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        ESP_LOGD(TAG, "Estado: IN_HANDSHAKE, Flag: NEWER_EPOCH");
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        ESP_LOGD(TAG, "Estado: IN_HANDSHAKE, Flag: STATE_SAFE_MODE");
                        Event event = eFromInHandshakeToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case ALERT:
                    if (flag == ALERT_EMPTY_QUEUE) {
                        ESP_LOGD(TAG, "Estado: ALERT, Flag: ALERT_EMPTY_QUEUE");
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, ALERT);
                        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT) {
                        ESP_LOGD(TAG, "Estado: ALERT, Flag: TIMEOUT_HEARTBEAT");
                        Event event = eFromAlertToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_DATA) {
                        ESP_LOGD(TAG, "Estado: ALERT, Flag: PHASE_DATA");
                        Event event = eFromAlertToData;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        ESP_LOGD(TAG, "Estado: ALERT, Flag: NEWER_EPOCH");
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case DATA:
                    if (flag == TIMEOUT_HEARTBEAT) {
                        ESP_LOGD(TAG, "Estado: DATA, Flag: TIMEOUT_HEARTBEAT");
                        Event event = eFromDataToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == PHASE_MONITOR) {
                        ESP_LOGD(TAG, "Estado: DATA, Flag: PHASE_MONITOR");
                        Event event = eFromDataToMonitor;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        ESP_LOGD(TAG, "Estado: DATA, Flag: NEWER_EPOCH");
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == DATA_EMPTY_QUEUE) {
                        ESP_LOGD(TAG, "Estado: DATA, Flag: DATA_EMPTY_QUEUE");
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, DATA);
                        if (xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            free(packet.payload);
                        }
                    }
                    break;

                case MONITOR:
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: MONITOR, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromMonitorToOutHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT) {
                        ESP_LOGD(TAG, "Estado: MONITOR, Flag: TIMEOUT_HEARTBEAT");
                        Event event = eFromMonitorToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        ESP_LOGD(TAG, "Estado: MONITOR, Flag: NEWER_EPOCH");
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MONITOR_EMPTY_QUEUE) {
                        ESP_LOGD(TAG, "Estado: MONITOR, Flag: MONITOR_EMPTY_QUEUE");
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, MONITOR);
                        if (xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            free(packet.payload);
                        }
                    }
                    break;

                case OUT_HANDSHAKE:
                    if (flag == HANDSHAKE_REQUEST) {
                        Event event = eRepeatHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_BALANCE) {
                        ESP_LOGD(TAG, "Estado: OUT_HANDSHAKE, Flag: TIMEOUT_BALANCE");
                        Event event = eFromOutHandshakeToStore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_NORMAL) {
                        ESP_LOGD(TAG, "Estado: OUT_HANDSHAKE, Flag: NORMAL");
                        Event event = eFromOutHandshakeToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == NEWER_EPOCH) {
                        ESP_LOGD(TAG, "Estado: OUT_HANDSHAKE, Flag: NEWER_EPOCH");
                        Event event = eNewerEpoch;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        ESP_LOGD(TAG, "Estado: OUT_HANDSHAKE, Flag: STATE_SAFE_MODE");
                        Event event = eFromOutHandshakeToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case NORMAL:
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: NORMAL, Flag: STATE_BALANCE_MODE");
                        Event event = eFromNormalToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: NORMAL, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromNormalToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT || flag == HEALTH_SCORE_CRITICAL
                        || flag == HEALTH_SCORE_UNAVAILABLE) {
                        ESP_LOGD(TAG, "Estado: NORMAL, Flag: TIMEOUT_HEARTBEAT o HEALTH_SCORE_CRITICAL o HEALTH_SCORE_UNAVAILABLE");
                        Event event = eFromNormalToCooling;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case STORE:
                    if (flag == STATE_NORMAL) {
                        ESP_LOGD(TAG, "Estado: STORE, Flag: STATE_NORMAL");
                        Event event = eFromStoreToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_SAFE_MODE) {
                        ESP_LOGD(TAG, "Estado: STORE, Flag: STATE_SAFE_MODE");
                        Event event = eFromStoreToSafe;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MESSAGE_ALERT) {
                        ESP_LOGD(TAG, "Estado: STORE, Flag: MESSAGE_ALERT");
                        Event event = eFromStoreToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: STORE, Flag: STATE_BALANCE_MODE");
                        Event event = eFromStoreToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: STORE, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromStoreToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case COOLING_TIME:
                    if (flag == TIMEOUT_COOLING) {
                        ESP_LOGD(TAG, "Estado: COOLING_TIME, Flag: TIMEOUT_COOLING");
                        Event event = eFromCoolingToUpdateScore;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MESSAGE_ALERT) {
                        ESP_LOGD(TAG, "Estado: COOLING_TIME, Flag: MESSAGE_ALERT");
                        Event event = eToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: COOLING_TIME, Flag: STATE_BALANCE_MODE ");
                        Event event = eFromCoolingToInitBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: COOLING_TIME, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromCoolingToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case UPDATE_SCORE:
                    if (flag == HEALTH_SCORE_DEGRADED) {
                        ESP_LOGD(TAG, "Estado: UPDATE_SCORE, Flag: HEALTH_SCORE_DEGRADED");
                        Event event = eFromUpdateScoreToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HEALTH_SCORE_UNAVAILABLE || flag == HEALTH_SCORE_CRITICAL) {
                        ESP_LOGD(TAG, "Estado: UPDATE_SCORE, Flag: HEALTH_SCORE_UNAVAILABLE o HEALTH_SCORE_CRITICAL");
                        Event event = eFromUpdateScoreToCooling;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == MESSAGE_ALERT) {
                        ESP_LOGD(TAG, "Estado: UPDATE_SCORE, Flag: MESSAGE_ALERT");
                        Event event = eToBypass;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: UPDATE_SCORE, Flag: STATE_BALANCE_MODE");
                        Event event = eFromUpdateScoreToInitBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == HANDSHAKE_REQUEST) {
                        ESP_LOGD(TAG, "Estado: UPDATE_SCORE, Flag: HANDSHAKE_REQUEST");
                        Event event = eFromUpdateScoreToInHandshake;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case BYPASS:
                    if (flag == STATE_BALANCE_MODE) {
                        ESP_LOGD(TAG, "Estado: BYPASS, Flag: STATE_BALANCE_MODE");
                        Event event = eFromBypassToBalance;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_BYPASS) {
                        ESP_LOGD(TAG, "Estado: BYPASS, Flag: TIMEOUT_BYPASS");
                        Event event = eFromBypassToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    break;

                case SAFE_MODE:
                    if (flag == SAFE_MODE_EMPTY_QUEUE) {
                        ESP_LOGD(TAG, "Estado: SAFE_MODE, Flag: SAFE_MODE_EMPTY_QUEUE");
                        mqtt_msg_general_t packet;
                        generate_message_empty_queue(&packet, SAFE_MODE);
                        if (xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            free(packet.payload);
                        }
                        Event event = eFromSafeToNormal;
                        xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                    }
                    if (flag == TIMEOUT_HEARTBEAT) {
                        ESP_LOGD(TAG, "Estado: SAFE_MODE, Flag: TIMEOUT_HEARTBEAT");
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
