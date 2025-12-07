#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "MQTT/mqtt.h"
#include "Time/time.h"
#include "AES-CTR/aes-ctr.h"
#include "esp_log.h"

#include "System/system.h"

static const char *TAG = "JSON";
static data_sensors_t data;


/**
 * @brief  Convierte el IV binario a string hexadecimal sin espacios.
 *
 * @param iv  IV generado en binario.
 * @param iv_str  String de salida formateado.
 * @param iv_len  Longitud del vector.
 */
void iv_to_string(const unsigned char *iv, char *iv_str, size_t iv_len) {
    static const char hex[] = "0123456789abcdef";

    for (size_t i = 0; i < iv_len; ++i) {
        uint8_t b = iv[i];
        iv_str[i * 2]     = hex[(b >> 4) & 0x0F]; // nibble alto
        iv_str[i * 2 + 1] = hex[b & 0x0F];        // nibble bajo
    }
    iv_str[iv_len * 2] = '\0';
}


/**
 * @brief  Genera un JSON con el texto encriptado y el IV.
 *
 * @param json_message  String que contendra el mensaje resultante.
 * @param encrypted  Texto plano encriptado.
 * @param iv  String IV.
 * @param buffer_size  Tamaño del mensaje.
 */
static void generate_encrypted_message(char *json_message, const char *encrypted, const char *iv, size_t buffer_size) {
    snprintf(json_message, buffer_size,
        "{\n"
        "  \"Texto\": \"%s\",\n"
        "  \"IV\": \"%s\",\n"
        "}",
        encrypted,
        iv
    );
}


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
        "  \"Dispositivo\": \"%s\",\n"
        "  \"IPv4\": \"%s\",\n"
        "  \"WiFi SSID\": \"%s\",\n"
        "  \"MAC\": \"%s\",\n"
        "  \"Fecha\": \"%s\",\n"
        "  \"Contador de pulsos de sonido\": %lu,\n"
        "  \"Maxima duracion de pulso\": %lu,\n"
        "  \"Temperatura\": %u,\n"
        "  \"Humedad\": %u,\n"
        "  \"CO2 ppm\": %.2f,\n"
        "  \"Sample min\": %lu\n"
        "}",
        config->device_name,
        config->wifi_ip,
        (const char*)config->wifi_ssid,
        config->mac_address,
        data.time,
        (unsigned long)data.ky037_counter,
        (unsigned long)data.ky037_max_duration,
        data.dht11_temperature,
        data.dht11_humidity,
        data.co2ppm,
        config->sample_rate
    );
    ESP_LOGI(TAG, "%s", output_buffer);
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
    char iv_str[IV_HEX_LEN];
    unsigned char iv_out[IV_LEN];
    dht11_data_t dht11;
    ky037_stats_t ky037;
    mq135_data_t mq135;

    char *output_base64 = (char*)heap_caps_malloc(JSON_MAX, MALLOC_CAP_8BIT);

    while (1) {

        xEventGroupWaitBits(
            event_group.collector_events,
            ALL_DATA_READY,
            pdTRUE,  // Limpiar bits despues de leer
            pdTRUE,  // Esperar todos los bits
            pdMS_TO_TICKS(portMAX_DELAY)
        );

        char *json = (char*)heap_caps_malloc(JSON_MAX, MALLOC_CAP_8BIT);

        if (!json || !output_base64) {
            ESP_LOGE(TAG, "- ERROR: No hay memoria para buffers JSON -");
            esp_restart();
        }
        memset(json, 0, JSON_MAX);
        memset(output_base64, 0, JSON_MAX);

        get_formated_data(&dht11, &ky037, &mq135);
        generate_json_data(json, JSON_MAX, &settings);

        aes_ctr_encrypt_to_base64((const unsigned char*)json, strlen(json),
                                  iv_out, output_base64, JSON_MAX);

        iv_to_string(iv_out, iv_str, IV_LEN);
        memset(json, 0, JSON_MAX);
        generate_encrypted_message(json, output_base64, iv_str, JSON_MAX);
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
    char *json = NULL;
    char *json_info = NULL;

    while (1) {
        if (xQueueReceive(queues.data_buffer, &json, pdMS_TO_TICKS(100))) {
            xEventGroupWaitBits(
                event_group.mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY
            );
            mqtt_publish(settings.topic_mqtt, json, (int)strlen(json), 2, 0);
            heap_caps_free(json);
        }
        if (xQueueReceive(queues.monitor_buffer, &json_info, 0)) {
            xEventGroupWaitBits(
                event_group.mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY
            );
            mqtt_publish("/devices/esp32/system_status", json_info, (int)strlen(json_info), 2, 0);
            heap_caps_free(json_info);
        }
    }
}