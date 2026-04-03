/**
* @file data.c
 * @brief Implementación de la lógica de recolección y publicación de datos.
 *
 * Este módulo contiene las dos tareas críticas para el flujo de datos:
 * 1. Collector: Sincroniza y agrega lecturas de múltiples sensores.
 * 2. Publisher: Implementa el "Traffic Shaping", gestionando colas, prioridades
 * de alertas y retardos variables (Jitter) según el estado de la FSM (Normal, Safe, Alert, etc.).
 */


#include "Data/data.h"
#include <esp_random.h>
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "MQTT/mqtt.h"
#include "Time/time.h"
#include "esp_log.h"
#include "System/system.h"
#include "mpack.h"
#include "Fsm/fsm.h"
#include "Healthscore/healthscore.h"
#include "Message/message.h"


/**
 * @brief Estructura local para almacenar en caché los tópicos MQTT.
 * Evita reconstruir strings en cada envío, ahorrando CPU.
 */
typedef struct {
    char topic_data[MAX_TOPIC];
    char topic_monitor[MAX_TOPIC];
    char topic_settings[MAX_TOPIC];
    char topic_alert_air[MAX_TOPIC];
    char topic_alert_temp[MAX_TOPIC];
    char topic_firmware[MAX_TOPIC];
    char topic_handshake[MAX_TOPIC];
    char topic_ping[MAX_TOPIC];
    char topic_queue[MAX_TOPIC];
    char topic_linkage_request[MAX_TOPIC];
} topic;


/**
 * @brief Carga todos los tópicos MQTT desde la configuración global.
 * @param topics Puntero a la estructura local de tópicos.
 */
static void init_topics(topic *topics) {
    settings_get_mqtt_topic_data(topics->topic_data, sizeof(topics->topic_data));
    settings_get_mqtt_topic_monitor(topics->topic_monitor, sizeof(topics->topic_monitor));
    settings_get_mqtt_topic_settings(topics->topic_settings, sizeof(topics->topic_settings));
    settings_get_mqtt_topic_alert_air(topics->topic_alert_air, sizeof(topics->topic_alert_air));
    settings_get_mqtt_topic_alert_temp(topics->topic_alert_temp, sizeof(topics->topic_alert_temp));
    settings_get_mqtt_topic_hub_firmware_ok(topics->topic_firmware, sizeof(topics->topic_firmware));
    settings_get_mqtt_topic_handshake_to_edge(topics->topic_handshake, sizeof(topics->topic_handshake));
    settings_get_mqtt_topic_ping(topics->topic_ping, sizeof(topics->topic_ping));
    settings_get_mqtt_topic_empty_queue(topics->topic_queue, sizeof(topics->topic_queue));
    settings_get_mqtt_topic_linkage_request(topics->topic_linkage_request, sizeof(topics->topic_linkage_request));
}


/**
 * @brief Actualiza el puntaje de salud del sistema tras un intento de envío MQTT.
 *
 * @param ret Resultado del envío (ID del mensaje o -1 si hubo error).
 * @param qos Calidad de servicio del mensaje enviado.
 */
static void update_health_score(const int ret, const int qos) {
    if (ret == -1) {
        const health_event_t event = {
            .event = HEALTH_EVT_ERROR_SEND,
            .msg_id = 0,
            .timestamp = 0
        };
        xQueueSend(queues.health, &event, pdMS_TO_TICKS(0));
    } else if (qos == 1) {
        const health_event_t event = {
            .event = HEALTH_EVT_MSG_SENT,
            .msg_id = ret,
            .timestamp = esp_timer_get_time()
        };
        xQueueSend(queues.health, &event, pdMS_TO_TICKS(0));
    }
}


/**
 * @brief Genera número aleatorio entre 0..N
 * @param N Valor máximo inclusive
 * @return Número aleatorio 0..N
 *
 * Para N=5: devuelve 0,1,2,3,4,5 con distribución uniforme
 */
static uint32_t random_jitter(const uint32_t N) {
    const uint64_t product = (uint64_t)esp_random() * N;
    return (uint32_t)(product >> 32);
}


/**
 * @brief  Recolecta la informacion en el parametro data
 *
 * @param dht11
 * @param ky037
 * @param mq135
 * @param data Puntero de tipo data_sensors_t que recibe la informacion. Para no generar una
 * copia de este campo, se usa un puntero y de esa forma buscar mas eficiencia.
 */
static void get_formated_data(dht11_data_t *dht11, ky037_t *ky037, mq135_data_t *mq135, data_sensors_t *data) {
    memset(data, 0, sizeof(*data));
    data->time = get_time();

    if (xQueueReceive(queues.dht11_buffer, dht11, 0)) {
        data->dht11_temperature = dht11_get_temperature(dht11);
        data->dht11_humidity = dht11_get_humidity(dht11);
    }

    if (xQueueReceive(queues.ky037_buffer, ky037, 0)) {
        data->ky037_counter = ky037_get_counter(ky037);
        data->ky037_max_duration = ky037_get_duration(ky037);
    }

    if (xQueueReceive(queues.mq135_buffer, mq135, 0)) {
        data->co2ppm = mq135->co2ppm;
    }
}


/**
 * @brief Tarea de Recolección de Datos.
 *
 * Se bloquea esperando el bit `ALL_DATA_READY` en el event group.
 * Cuando DHT11, MQ135 y KY037 han reportado datos:
 * 1. Extrae los datos de sus respectivas colas.
 * 2. Agrega la información en una estructura `data_sensors_t`.
 * 3. Genera un paquete MPack (serialización).
 * 4. Envía el paquete a la cola `data_buffer` para ser publicado.
 */
void data_collection_task(void *pvParameter) {
    static data_sensors_t data;
    static dht11_data_t dht11;
    static ky037_t ky037;
    static mq135_data_t mq135;
    mqtt_packet_t packet;

    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;

            while (running) {
                const EventBits_t bits = xEventGroupWaitBits(event_group.collector_events,
                    ALL_DATA_READY,
                    pdTRUE,  // Limpiar bits despues de leer
                    pdTRUE,  // Esperar todos los bits
                    pdMS_TO_TICKS(1000)); // Timeout corto

                uint32_t stop_signal = 0;
                if (xTaskNotifyWait(0, ULONG_MAX, &stop_signal, 0) == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        ESP_LOGW("Data", "WARNING: Señal STOP recibida. Suspendiendo...");
                        running = false;
                        continue; // Sale del bucle interno y espera un nuevo START limpio
                    }
                }

                if ((bits & ALL_DATA_READY) == ALL_DATA_READY) {
                    get_formated_data(&dht11, &ky037, &mq135, &data);
                    if (generate_message_data(data, &packet)) {
                        if (xQueueSend(queues.data_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                            ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                            free(packet.payload);
                        }
                    } else {
                        ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                    }
                }
            }
        }
    }
}


/**
 * @brief Procesa la cola de datos de sensores.
 */
static void get_data_from_queue_data_buffer(mqtt_packet_t *packet_data, const topic *topics) {
    if (xQueueReceive(queues.data_buffer, packet_data, 0)) {
        const esp_err_t ret = mqtt_publish(topics->topic_data, packet_data->payload, (int)packet_data->len, QOS_DATA, NOT_RETAIN);
        free(packet_data->payload);
        update_health_score(ret, QOS_DATA);
    }
}


/**
 * @brief Procesa la cola de monitor (métricas del sistema).
 */
static void get_data_from_queue_monitor_buffer(mqtt_packet_t *packet_monitor, const topic *topics) {
    if (xQueueReceive(queues.monitor_buffer, packet_monitor, 0)) {
        const esp_err_t ret = mqtt_publish(topics->topic_monitor, packet_monitor->payload, (int)packet_monitor->len, QOS_MONITOR, NOT_RETAIN);
        free(packet_monitor->payload);
        update_health_score(ret, QOS_MONITOR);
    }
}


/**
 * @brief Procesa la cola de settings.
 */
static void get_data_from_queue_settings_buffer(mqtt_packet_t *packet_settings, const topic *topics) {
    if (xQueueReceive(queues.settings_buffer, packet_settings, 0)) {
        const esp_err_t ret = mqtt_publish(topics->topic_settings, packet_settings->payload, (int)packet_settings->len, QOS_SETTING, NOT_RETAIN);
        free(packet_settings->payload);
        update_health_score(ret, QOS_SETTING);
    }
}


/**
 * @brief Procesa la cola de alertas de aire (Gas/CO2).
 */
static void get_data_from_queue_alert_air_buffer(mqtt_packet_t *packet_alert, const topic *topics) {
    if (xQueueReceive(queues.alert_air_buffer, packet_alert, 0)) {
        const State state = atomic_load(&shared_state);
        if (state != BYPASS) {
            const esp_err_t ret = mqtt_publish(topics->topic_alert_air, packet_alert->payload, (int)packet_alert->len, QOS_ALERT, NOT_RETAIN);
            free(packet_alert->payload);
            update_health_score(ret, QOS_ALERT);
        }
    }
}


/**
 * @brief Procesa la cola de alertas de temperatura.
 */
static void get_data_from_queue_alert_temp_buffer(mqtt_packet_t *packet_alert, const topic *topics) {
    if (xQueueReceive(queues.alert_temp_buffer, packet_alert, 0)) {
        const State state = atomic_load(&shared_state);
        if (state != BYPASS) {
            const esp_err_t ret = mqtt_publish(topics->topic_alert_temp, packet_alert->payload, (int)packet_alert->len, QOS_ALERT, NOT_RETAIN);
            free(packet_alert->payload);
            update_health_score(ret, QOS_ALERT);
        }
    }
}


/**
 * @brief Procesa la cola de alertas de temperatura.
 */
static void get_data_from_queue_general_buffer(mqtt_msg_general_t *packet_general, const topic *topics) {
    if (xQueueReceive(queues.general, packet_general, 0)) {
        switch (packet_general->topic) {
            case FIRMWARE_OK: {
                const esp_err_t ret = mqtt_publish(topics->topic_firmware, packet_general->payload, (int)packet_general->len, QOS_FIRMWARE, NOT_RETAIN);
                free(packet_general->payload);
                update_health_score(ret, QOS_FIRMWARE);
                break;
            }
            case HANDSHAKE: {
                const esp_err_t ret = mqtt_publish(topics->topic_handshake, packet_general->payload, (int)packet_general->len, QOS_HANDSHAKE, NOT_RETAIN);
                free(packet_general->payload);
                update_health_score(ret, QOS_HANDSHAKE);
                break;
            }
            case PING: {
                const esp_err_t ret = mqtt_publish(topics->topic_ping, packet_general->payload, (int)packet_general->len, QOS_PING, NOT_RETAIN);
                free(packet_general->payload);
                update_health_score(ret, QOS_PING);
                break;
            }
            case QUEUE_EMPTY: {
                const esp_err_t ret = mqtt_publish(topics->topic_queue, packet_general->payload, (int)packet_general->len, QOS_EMPTY, NOT_RETAIN);
                free(packet_general->payload);
                update_health_score(ret, QOS_EMPTY);
                break;
            }
            case LINKAGE_REQUEST: {
                const esp_err_t ret = mqtt_publish(topics->topic_linkage_request, packet_general->payload, (int)packet_general->len, QOS_LINKAGE, NOT_RETAIN);
                free(packet_general->payload);
                update_health_score(ret, QOS_LINKAGE);
                break;
            }
        }
    }
}


/**
 * @brief Tarea de Publicación (Traffic Shaper).
 *
 * Implementa la lógica de priorización y control de flujo del protocolo.
 * Funciona como una máquina de estados implícita que decide qué enviar y cuándo.
 *
 * Reglas de Prioridad:
 * 1. Mensajes Generales (Handshakes/Pings) siempre se procesan.
 * 2. Estado NORMAL: Envío inmediato de todo.
 * 3. Estado SAFE_MODE: Envío ralentizado con Jitter + notificación única de "Cola Vacía".
 * 4. Estados Fases (ALERT/DATA/MONITOR):
 * - Prioridad estricta: Alertas > Datos > Monitor.
 * - Jitter obligatorio.
 * - Notificación "One-Shot" cuando la cola correspondiente se vacía.
 */
void data_publish_task(void *pvParameter) {
    static topic topics;
    mqtt_packet_t packet_data;
    mqtt_packet_t packet_monitor;
    mqtt_packet_t packet_settings;
    mqtt_packet_t packet_alert;
    mqtt_msg_general_t packet_general;

    init_topics(&topics);

    bool alert_empty = true;
    bool data_empty = true;
    bool monitor_empty = true;
    bool safe_empty = true;

    while (1) {
        const State state = atomic_load(&shared_state);
        static State prev_state = CHECK_FIRMWARE;

        if (state != prev_state) {
            alert_empty = true;
            data_empty = true;
            monitor_empty = true;
            safe_empty = true;
            prev_state = state;
        }

        const EventBits_t bits = xEventGroupGetBits(event_group.mqtt_event_group);
        if ((bits & MQTT_CONNECTED_BIT) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        get_data_from_queue_general_buffer(&packet_general, &topics);

        if (state == NORMAL) {
            get_data_from_queue_data_buffer(&packet_data, &topics);
            get_data_from_queue_monitor_buffer(&packet_monitor, &topics);
            get_data_from_queue_settings_buffer(&packet_settings, &topics);
            get_data_from_queue_alert_air_buffer(&packet_alert, &topics);
            get_data_from_queue_alert_temp_buffer(&packet_alert, &topics);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        else if (state == SAFE_MODE) {
            const UBaseType_t msg_pending = uxQueueMessagesWaiting(queues.data_buffer) +
                                            uxQueueMessagesWaiting(queues.monitor_buffer) +
                                            uxQueueMessagesWaiting(queues.alert_temp_buffer) +
                                            uxQueueMessagesWaiting(queues.alert_air_buffer);
            if (msg_pending > 0) {
                const uint32_t safe_jitter = atomic_load(&safe_mode.jitter);
                const uint32_t jitter = random_jitter(safe_jitter);
                get_data_from_queue_data_buffer(&packet_data, &topics);
                get_data_from_queue_monitor_buffer(&packet_monitor, &topics);
                get_data_from_queue_alert_air_buffer(&packet_alert, &topics);
                get_data_from_queue_alert_temp_buffer(&packet_alert, &topics);
                vTaskDelay(pdMS_TO_TICKS((safe_mode.frequency + jitter) * 1000));
            }
            else {
                if (safe_empty) {
                    safe_empty = false;
                    const uint32_t flag = SAFE_MODE_EMPTY_QUEUE;
                    xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else if (state == ALERT) {
            const UBaseType_t msg_pending = uxQueueMessagesWaiting(queues.alert_air_buffer) +
                                            uxQueueMessagesWaiting(queues.alert_temp_buffer);
            if (msg_pending > 0) {
                const uint32_t frequency = atomic_load(&phase.frequency);
                const uint32_t phase_jitter = atomic_load(&phase.jitter);
                const uint32_t jitter = random_jitter(phase_jitter);
                get_data_from_queue_alert_air_buffer(&packet_alert, &topics);
                get_data_from_queue_alert_temp_buffer(&packet_alert, &topics);
                vTaskDelay(pdMS_TO_TICKS((frequency + jitter) * 1000));
            }
            else {
                if (alert_empty) {
                    alert_empty = false;
                    const uint32_t flag = ALERT_EMPTY_QUEUE;
                    xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else if (state == DATA) {
            const UBaseType_t msg_pending_alert = uxQueueMessagesWaiting(queues.alert_air_buffer) +
                                                  uxQueueMessagesWaiting(queues.alert_temp_buffer);
            const UBaseType_t msg_pending_data = uxQueueMessagesWaiting(queues.data_buffer);
            if (msg_pending_data == 0) {
                if (data_empty) {
                    data_empty = false;
                    const uint32_t flag = DATA_EMPTY_QUEUE;
                    xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
                }
            }
            if (msg_pending_data > 0 || msg_pending_alert > 0) {
                const uint32_t frequency = atomic_load(&phase.frequency);
                const uint32_t phase_jitter = atomic_load(&phase.jitter);
                const uint32_t jitter = random_jitter(phase_jitter);
                get_data_from_queue_alert_air_buffer(&packet_alert, &topics);
                get_data_from_queue_alert_temp_buffer(&packet_alert, &topics);
                get_data_from_queue_data_buffer(&packet_data, &topics);
                vTaskDelay(pdMS_TO_TICKS((frequency + jitter) * 1000));
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else if (state == MONITOR) {
            const UBaseType_t msg_pending = uxQueueMessagesWaiting(queues.alert_air_buffer) +
                                                  uxQueueMessagesWaiting(queues.alert_temp_buffer) +
                                                  uxQueueMessagesWaiting(queues.data_buffer);
            const UBaseType_t msg_pending_monitor = uxQueueMessagesWaiting(queues.monitor_buffer);
            if (msg_pending_monitor == 0) {
                if (monitor_empty) {
                    monitor_empty = false;
                    const uint32_t flag = MONITOR_EMPTY_QUEUE;
                    xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
                }
            }
            if (msg_pending_monitor > 0 || msg_pending > 0) {
                const uint32_t frequency = atomic_load(&phase.frequency);
                const uint32_t phase_jitter = atomic_load(&phase.jitter);
                const uint32_t jitter = random_jitter(phase_jitter);
                get_data_from_queue_alert_air_buffer(&packet_alert, &topics);
                get_data_from_queue_alert_temp_buffer(&packet_alert, &topics);
                get_data_from_queue_data_buffer(&packet_data, &topics);
                get_data_from_queue_monitor_buffer(&packet_monitor, &topics);
                vTaskDelay(pdMS_TO_TICKS((frequency + jitter) * 1000));
            }
            else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}