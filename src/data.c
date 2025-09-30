#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
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
        memset(&data, 0, sizeof(data));

        esp_err_t ret = dht11_read_data();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "- ERROR: No se pudieron leer los datos -");
        }
        else {
            data.dht11_temperature = dht11_data.temperature;
            data.dht11_humidity = dht11_data.humidity;
        }

        data.co2ppm = mq135_get_corrected_ppm((float)data.dht11_temperature, (float)data.dht11_humidity, &co2);
        data.coppm = mq135_get_corrected_ppm((float)data.dht11_temperature, (float)data.dht11_humidity, &co);
        data.nh3ppm = mq135_get_corrected_ppm((float)data.dht11_temperature, (float)data.dht11_humidity, &nh3);
        data.c6h6pm = mq135_get_corrected_ppm((float)data.dht11_temperature, (float)data.dht11_humidity, &c6h6);
        data.no2ppm = mq135_get_corrected_ppm((float)data.dht11_temperature, (float)data.dht11_humidity, &voc);

        if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            data.ky037_counter = ky037_stats.counter;
            data.ky037_max_duration = ky037_stats.max_duration;
            // Reset estadisticas para el siguiente período
            ky037_stats.counter = 0;
            ky037_stats.max_duration = 0;
            xSemaphoreGive(xStatsMutex);
        }

        char json[190];
        char output_base64[256];
        unsigned char iv_out[16];
        snprintf(json, sizeof(json),
        "{\"Contador de pulsos de sonido\": %lu, \"Maxima duracion de pulso\": %lu, "
        "\"Temperatura\": %u, \"Humedad\": %u, \"CO2 ppm:\" %.2f, \"CO ppm:\" %.2f,"
        "\"NH3 ppm:\" %.2f, \"C6H6 ppm:\" %.2f, \"VOC ppm:\" %.2f}",
        (unsigned long) data.ky037_counter, (unsigned long) data.ky037_max_duration,
        data.dht11_temperature, data.dht11_humidity,
        data.co2ppm, data.coppm, data.nh3ppm, data.c6h6pm, data.no2ppm);
        ESP_LOGI(TAG, "%s", json);
        //aes_ctr_encrypt_to_base64((const unsigned char *)json, sizeof(json), iv_out, output_base64, sizeof(output_base64));

        vTaskDelay(pdMS_TO_TICKS(6000));  // settings.sample_rate*60000
    }
}

