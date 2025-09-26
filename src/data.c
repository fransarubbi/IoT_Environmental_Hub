#include "Data/data.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "AES-CTR/aes-ctr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"


static const char *TAG = "JSON";


void data_json_encrypt_task(void *pvParameters) {
    data_sensors_t data;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(settings.sample_rate*60000));
        memset(&data, 0, sizeof(data));

        esp_err_t ret = dht11_read_data();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "- ERROR: No se pudieron leer los datos -");
        }
        else {
            data.dht11_temperature = dht11_data.temperature;
            data.dht11_humidity = dht11_data.humidity;
            data.dht11_temp_decimal = dht11_data.temp_decimal;
            data.dht11_hum_decimal = dht11_data.hum_decimal;
        }

        if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            data.ky037_counter = ky037_stats.counter;
            data.ky037_max_duration = ky037_stats.max_duration;
            // Reset estadisticas para el siguiente período
            ky037_stats.counter = 0;
            ky037_stats.max_duration = 0;
            xSemaphoreGive(xStatsMutex);
        }

        char json[128];
        char output_base64[256];
        unsigned char iv_out[16];
        snprintf(json, sizeof(json),
        "{\"Contador de pulsos de sonido\": %lu, \"Maxima duracion de pulso\": %lu, "
        "\"Temperatura\": %u.%u, \"Humedad\": %u.%u}",
        (unsigned long) data.ky037_counter,
        (unsigned long) data.ky037_max_duration,
        data.dht11_temperature,
        data.dht11_temp_decimal,
        data.dht11_humidity,
        data.dht11_hum_decimal);
        ESP_LOGI(TAG, "%s", json);
        aes_ctr_encrypt_to_base64((const unsigned char *)json, sizeof(json), iv_out, output_base64, sizeof(output_base64));
    }
}

