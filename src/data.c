#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "MQTT/mqtt.h"
#include "Time/time.h"
#include "esp_log.h"
#include "System/system.h"


static data_sensors_t data;


/**
 * @brief Genera una cadena JSON con los datos del dispositivo y sensores.
 *
 * @param output_buffer  Puntero al buffer char donde se escribira el string JSON.
 * @param buffer_size  Tamaño maximo del buffer.
 * @param config  Estructura con la configuracion del dispositivo.
 * @param data  Estructura con las lecturas de los sensores.
 * @param errors  Flag de errores para la temperatura/humedad.
 */
static void generate_json_data(char *output_buffer, size_t buffer_size, const settings_t *config) {

    snprintf(output_buffer, buffer_size,
        "{\n"
        "  \"ID\": \"%s\",\n"
        "  \"destination_type\": SERVER,\n"
        "  \"destination_id\": SERVER0,\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"device_name\": \"%s\",\n"
        "  \"ipv4\": \"%s\",\n"
        "  \"wifi_ssid\": \"%s\",\n"
        "  \"pulse_counter\": %lu,\n"
        "  \"pulse_max_duration\": %lu,\n"
        "  \"temperature\": %u,\n"
        "  \"humidity\": %u,\n"
        "  \"co2_ppm\": %.2f,\n"
        "  \"sample\": %lu\n"
        "}",
        config->node.mac_address,
        data.time,
        config->node.device_name,
        config->wifi.ip,
        (const char*)config->wifi.ssid,
        (unsigned long)data.ky037_counter,
        (unsigned long)data.ky037_max_duration,
        data.dht11_temperature,
        data.dht11_humidity,
        data.co2ppm,
        config->node.sample_rate
    );
}


/**
 * @brief  Recolecta la informacion en el parametro data
 *
 * @param data Puntero de tipo data_sensors_t que recibe la informacion. Para no generar una
 * copia de este campo, se usa un puntero y de esa forma buscar mas eficiencia.
 */
static void get_formated_data(dht11_data_t *dht11, ky037_stats_t *ky037, mq135_data_t *mq135) {
    memset(&data, 0, sizeof(data));
    get_time(data.time);

    if (xQueueReceive(queues.data_buffer, dht11, 0)) {
        data.dht11_temperature = dht11->temperature;
        data.dht11_humidity = dht11->humidity;
    }

    if (xQueueReceive(queues.ky037_buffer, ky037, 0)) {
        data.ky037_counter = ky037->counter;
        data.ky037_max_duration = ky037->max_duration;
    }

    if (xQueueReceive(queues.mq135_buffer, mq135, 0)) {
        data.co2ppm = mq135->co2ppm;
    }
}


/**
 * @brief  Tarea que lee los sensores, formatea los datos y los encola mientras haya espacio en la misma.
 *
 * @param pvParameter
 */
void data_collection_task(void *pvParameter) {
    dht11_data_t dht11;
    ky037_stats_t ky037;
    mq135_data_t mq135;

    while (1) {
        xEventGroupWaitBits(
            event_group.collector_events,
            ALL_DATA_READY,
            pdTRUE,  // Limpiar bits despues de leer
            pdTRUE,  // Esperar todos los bits
            pdMS_TO_TICKS(portMAX_DELAY)
        );
        char *json = (char*)heap_caps_malloc(JSON_MAX, MALLOC_CAP_8BIT);
        get_formated_data(&dht11, &ky037, &mq135);
        generate_json_data(json, JSON_MAX, &settings);
        xQueueSend(queues.data_buffer, &json, portMAX_DELAY);
    }
}


/**
 * @brief  Tarea que lee los datos de la cola y los publica al broker mientras la conexion
 * este activa.
 *
 * @param pvParameter
 */
void data_publish_task(void *pvParameter) {
    char *json_data = NULL;
    char *json_info = NULL;
    char *json_sett = NULL;

    while (1) {
        if (xQueueReceive(queues.data_buffer, &json_data, pdMS_TO_TICKS(100))) {
            xEventGroupWaitBits(
                event_group.mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY
            );
            mqtt_publish(settings.mqtt.topic_data, json_data, (int)strlen(json_data), 2, 0);
            heap_caps_free(json_data);
        }
        if (xQueueReceive(queues.monitor_buffer, &json_info, 0)) {
            xEventGroupWaitBits(
                event_group.mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY
            );
            mqtt_publish(settings.mqtt.topic_monitor, json_info, (int)strlen(json_info), 2, 0);
            heap_caps_free(json_info);
        }
        if (xQueueReceive(queues.settings_buffer, &json_sett, 0)) {
            xEventGroupWaitBits(
                event_group.mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY
            );
            mqtt_publish(settings.mqtt.topic_settings, json_sett, (int)strlen(json_sett), 2, 0);
            heap_caps_free(json_sett);
        }
    }
}