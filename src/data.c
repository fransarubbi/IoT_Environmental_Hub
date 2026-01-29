#include "Data/data.h"
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




static void update_health_score(int ret, int qos) {
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
 * @brief  Tarea que lee los sensores, formatea los datos y los encola mientras haya espacio en la misma.
 *
 * @param pvParameter
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

                xEventGroupWaitBits(event_group.collector_events,
                    ALL_DATA_READY,
                    pdTRUE,  // Limpiar bits despues de leer
                    pdTRUE,  // Esperar todos los bits
                    pdMS_TO_TICKS(portMAX_DELAY)
                    );
                get_formated_data(&dht11, &ky037, &mq135, &data);
                if (generate_message_data(data, &packet)) {
                    if (xQueueSend(queues.data_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                        free(packet.payload);
                    }
                } else {
                    ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                }

                uint32_t stop_signal = 0;
                if (xTaskNotifyWait(0, ULONG_MAX, &stop_signal, 0) == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        ESP_LOGW("Data", "WARNING: Señal STOP recibida. Suspendiendo...");
                        running = false;
                    }
                }
            }
        }
    }
}


/**
 * @brief  Tarea que lee los datos de la cola y los publica al broker mientras la conexion
 * este activa.
 *
 * @param pvParameter
 */
void data_publish_task(void *pvParameter) {
    mqtt_packet_t packet_data;
    mqtt_packet_t packet_monitor;
    mqtt_packet_t packet_settings;
    mqtt_packet_t packet_alert;

    char topic_data[MAX_TOPIC];
    char topic_monitor[MAX_TOPIC];
    char topic_settings[MAX_TOPIC];
    char topic_alert_air[MAX_TOPIC];
    char topic_alert_temp[MAX_TOPIC];

    settings_get_mqtt_topic_data(topic_data, sizeof(topic_data));
    settings_get_mqtt_topic_monitor(topic_monitor, sizeof(topic_monitor));
    settings_get_mqtt_topic_settings(topic_settings, sizeof(topic_settings));
    settings_get_mqtt_topic_alert_air(topic_alert_air, sizeof(topic_alert_air));
    settings_get_mqtt_topic_alert_temp(topic_alert_temp, sizeof(topic_alert_temp));

    while (1) {
        bool did_work = false;
        EventBits_t bits = xEventGroupGetBits(event_group.mqtt_event_group);
        if ((bits & MQTT_CONNECTED_BIT) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (xQueueReceive(queues.data_buffer, &packet_data, 0)) {
            const esp_err_t ret = mqtt_publish(topic_data, packet_data.payload, (int)packet_data.len, QOS_DATA, NOT_RETAIN);
            free(packet_data.payload);
            did_work = true;
            update_health_score(ret, QOS_DATA);
        }
        if (xQueueReceive(queues.monitor_buffer, &packet_monitor, 0)) {
            const esp_err_t ret = mqtt_publish(topic_monitor, packet_monitor.payload, (int)packet_monitor.len, QOS_MONITOR, NOT_RETAIN);
            free(packet_monitor.payload);
            did_work = true;
            update_health_score(ret, QOS_MONITOR);
        }
        if (xQueueReceive(queues.settings_buffer, &packet_settings, 0)) {
            const esp_err_t ret = mqtt_publish(topic_settings, packet_settings.payload, (int)packet_settings.len, QOS_SETTING, NOT_RETAIN);
            free(packet_settings.payload);
            did_work = true;
            update_health_score(ret, QOS_SETTING);
        }
        if (xQueueReceive(queues.alert_air_buffer, &packet_alert, 0)) {
            const esp_err_t ret = mqtt_publish(topic_alert_air, packet_alert.payload, (int)packet_alert.len, QOS_ALERT, NOT_RETAIN);
            free(packet_alert.payload);
            did_work = true;
            update_health_score(ret, QOS_ALERT);
        }
        if (xQueueReceive(queues.alert_temp_buffer, &packet_alert, 0)) {
            const esp_err_t ret = mqtt_publish(topic_alert_temp, packet_alert.payload, (int)packet_alert.len, QOS_ALERT, NOT_RETAIN);
            free(packet_alert.payload);
            did_work = true;
            update_health_score(ret, QOS_ALERT);
        }
        if (!did_work) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}