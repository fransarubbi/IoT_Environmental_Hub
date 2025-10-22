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


void data_json_encrypt_task(void *pvParameters) {
    data_sensors_t data;

    while (1) {
        memset(&data, 0, sizeof(data));

        get_time(data.time);

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

        char json[500];
        //char output_base64[256];
        //unsigned char iv_out[16];
        snprintf(json, sizeof(json),
        "{\"Dispositivo\": %s, \n"
        "\"IPv4\": %s, \n"
        "\"WiFi SSID\": %s, \n"
        "\"MAC\": %s, \n"
        "\"Fecha\": %s, \n"
        "\"Contador de pulsos de sonido\": %lu, \n"
        "\"Maxima duracion de pulso\": %lu, \n"
        "\"Temperatura\": %u, \n"
        "\"Humedad\": %u, \n"
        "\"CO2 ppm:\" %.2f, \n"
        "\"CO ppm:\" %.2f, \n"
        "\"NH3 ppm:\" %.2f, \n"
        "\"C6H6 ppm:\" %.2f, \n"
        "\"VOC ppm:\" %.2f}",
        settings.device_name, settings.wifi_ip, (const char*)settings.wifi_ssid, settings.mac_address, data.time, (unsigned long) data.ky037_counter, (unsigned long) data.ky037_max_duration,
        data.dht11_temperature, data.dht11_humidity, data.co2ppm, data.coppm, data.nh3ppm, data.c6h6pm, data.no2ppm);
        ESP_LOGI(TAG, "%s", json);
        //aes_ctr_encrypt_to_base64((const unsigned char *)json, sizeof(json), iv_out, output_base64, sizeof(output_base64));
        mqtt_publish("test/topic", json, sizeof(json), 2, 0);
        vTaskDelay(pdMS_TO_TICKS(6000));  // settings.sample_rate*60000
    }
}

