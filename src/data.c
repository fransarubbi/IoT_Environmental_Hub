#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "MQTT/mqtt.h"
#include "Time/time.h"
#include "AES-CTR/aes-ctr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"


static const char *TAG = "JSON";



/**
 * @brief  Convierte el IV binario a string hexadecimal sin espacios.
 *
 * @param iv  IV generado en binario.
 * @param iv_str  String de salida formateado.
 * @param iv_len  Longitud del vector.
 */
void iv_to_string(const unsigned char *iv, char *iv_str, size_t iv_len) {
    for (size_t i = 0; i < iv_len; i++) {
        sprintf(&iv_str[i * 2], "%02x", iv[i]);
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
static void generate_json_data(char *output_buffer, size_t buffer_size, const settings_t *config, const data_sensors_t *data, bool errors) {

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
        "  \"CO ppm\": %.2f,\n"
        "  \"NH3 ppm\": %.2f,\n"
        "  \"C6H6 ppm\": %.2f,\n"
        "  \"NO2 ppm\": %.2f,\n"
        "  \"Sample min\": %lu\n"
        "}",
        config->device_name,
        config->wifi_ip,
        (const char*)config->wifi_ssid,
        config->mac_address,
        data->time,
        (unsigned long)data->ky037_counter,
        (unsigned long)data->ky037_max_duration,
        (!errors) ? data->dht11_temperature : 0,
        (!errors) ? data->dht11_humidity : 0,
        data->co2ppm,
        data->coppm,
        data->nh3ppm,
        data->c6h6ppm,
        data->no2ppm,
        config->sample_rate
    );
}


/**
 * @brief  Recolecta la informacion en el parametro data
 *
 * @param data Puntero de tipo data_sensors_t que recibe la informacion. Para no generar una
 * copia de este campo, se usa un puntero y de esa forma buscar mas eficiencia.
 * @param flag_errors Flag para saber si la lectura del sensor DHT11 fue correcta o no.
 */
static void get_formated_data(data_sensors_t *data, bool *flag_errors) {
    memset(data, 0, sizeof(*data));
    get_time(data->time);

    esp_err_t ret = dht11_read_data();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: No se pudieron leer los datos -");
        *flag_errors = true;
    }
    else {
        data->dht11_temperature = dht11_data.temperature;
        data->dht11_humidity = dht11_data.humidity;
    }

    data->co2ppm = mq135_get_corrected_ppm((float)data->dht11_temperature, (float)data->dht11_humidity, &co2);
    data->coppm = mq135_get_corrected_ppm((float)data->dht11_temperature, (float)data->dht11_humidity, &co);
    data->nh3ppm = mq135_get_corrected_ppm((float)data->dht11_temperature, (float)data->dht11_humidity, &nh3);
    data->c6h6ppm = mq135_get_corrected_ppm((float)data->dht11_temperature, (float)data->dht11_humidity, &c6h6);
    data->no2ppm = mq135_get_corrected_ppm((float)data->dht11_temperature, (float)data->dht11_humidity, &voc);

    if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        data->ky037_counter = ky037_stats.counter;
        data->ky037_max_duration = ky037_stats.max_duration;
        // Reset estadisticas para el siguiente período
        ky037_stats.counter = 0;
        ky037_stats.max_duration = 0;
        xSemaphoreGive(xStatsMutex);
    }

}


/**
 * @brief  Tarea que lee los sensores, formatea los datos y los encola mientras haya espacio en la misma.
 *
 * @param pvParameter
 */
void data_collection_task(void *pvParameter) {
    data_sensors_t data;
    bool flag_errors = false;
    char json[JSON_MAX];
    char output_base64[JSON_MAX];
    char iv_str[33];
    unsigned char iv_out[16];

    while (1) {
        get_formated_data(&data, &flag_errors);
        generate_json_data(json, JSON_MAX, &settings, &data, flag_errors);
        ESP_LOGI(TAG, "%s", json);

        aes_ctr_encrypt_to_base64((const unsigned char *)json,
            strlen(json),
            iv_out,
            output_base64,
            sizeof(output_base64));

        //ESP_LOGI("TEXT", "%s", output_base64);
        //ESP_LOG_BUFFER_HEX("IV", iv_out, 16);
        //ESP_LOG_BUFFER_HEX("KEY", settings.aes_key, 32);

        memset(json, 0, sizeof(json));
        iv_to_string(iv_out, iv_str, 16);
        generate_encrypted_message(json, output_base64, iv_str, 600);
        ESP_LOGI(TAG, "%s", json);
        xQueueSend(data_buffer, json, portMAX_DELAY);


        UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(NULL);
        size_t hwm_bytes = hwm_words * sizeof(StackType_t);
        ESP_LOGI(TAG, "Stack high water mark: %u words (~%u bytes)", (unsigned)hwm_words, (unsigned)hwm_bytes);

        vTaskDelay(pdMS_TO_TICKS(5000));  // settings.sample_rate * 60000
    }
}


/**
 * @brief  Tarea que lee los datos de la cola y los publica al broker mientras la conexion
 * este activa.
 *
 * @param pvParameter
 */
void data_publish_task(void *pvParameter) {
    char json[JSON_MAX];

    while (1) {
        // Espera datos del buffer
        if (xQueueReceive(data_buffer, json, portMAX_DELAY)) {
            xEventGroupWaitBits(
                mqtt_event_group,
                MQTT_CONNECTED_BIT,
                pdFALSE,  // No limpiar el bit
                pdTRUE,   // Esperar todos los bits
                portMAX_DELAY  // O pdMS_TO_TICKS(30000) para timeout
            );

            /*
            UBaseType_t hwm_words = uxTaskGetStackHighWaterMark(NULL);
            size_t hwm_bytes = hwm_words * sizeof(StackType_t);
            ESP_LOGI(TAG, "Stack high water mark: %u words (~%u bytes)", (unsigned)hwm_words, (unsigned)hwm_bytes);
            */

            mqtt_publish(settings.topic_mqtt, json, strlen(json), 2, 0);
        }
    }
}