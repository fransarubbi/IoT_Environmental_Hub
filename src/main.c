#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "Data/data.h"
#include "MQ135/mq135.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "Wifi/wifi.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"
#include "Time/time.h"



void app_main(void) {

    esp_err_t retUART = uart_init();
    if (retUART != ESP_OK) return;

    esp_err_t retWiFi = wifi_init();
    if (retWiFi != ESP_OK) return;

    esp_err_t retTime = time_init();
    if (retTime != ESP_OK) return;

    esp_err_t retMQTT = mqtt_init();
    if (retMQTT != ESP_OK) return;

    esp_err_t retMQ135 = mq135_init();
    esp_err_t retDHT11 = dht11_init();
    esp_err_t retKY037 = ky037_init();
    if (retMQ135 == ESP_OK && retDHT11 == ESP_OK && retKY037 == ESP_OK) {
        //vTaskDelay(pdMS_TO_TICKS(240000));   // 4 minutos de estabilizacion del mq135
        xTaskCreate(data_json_encrypt_task, "data_json_encrypt_task", 4096, NULL, 6, NULL);
    }
}