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


QueueHandle_t data_buffer = NULL;
QueueHandle_t system_buffer = NULL;
TaskHandle_t data_ct_handle = NULL;
TaskHandle_t data_pt_handle = NULL;
TaskHandle_t xStatsTaskHandle = NULL;


void app_main(void) {

    data_buffer = xQueueCreate(QUEUE_LENGTH, sizeof(char *));
    system_buffer = xQueueCreate(QUEUE_LENGTH, sizeof(char *));

    esp_err_t retMQ135 = ESP_FAIL;
    esp_err_t retDHT11 = ESP_FAIL;
    esp_err_t retKY037 = ESP_FAIL;

    if (uart_init() != ESP_OK) return;
    if (wifi_init() != ESP_OK) return;
    if (time_init() != ESP_OK) return;
    if (mqtt_init() != ESP_OK) return;

    while (1) {

        if (retMQ135 != ESP_OK) retMQ135 = mq135_init();
        if (retDHT11 != ESP_OK) retDHT11 = dht11_init();
        if (retKY037 != ESP_OK) retKY037 = ky037_init();

        if (retMQ135 == ESP_OK && retDHT11 == ESP_OK && retKY037 == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(WAIT));  // 1 seg para evitar consumir CPU entre los intentos de inicializacion
    }
    //vTaskDelay(pdMS_TO_TICKS(TIME_SETUP));   // 4 minutos de estabilizacion del mq135
    xTaskCreate(stack_monitor_task, "StackMonitor", STACK_MONITOR, NULL, 1, NULL);
    xTaskCreatePinnedToCore(data_collection_task, "DataCollector", STACK_COLLECTOR, NULL, 5, &data_ct_handle, 0);
    xTaskCreatePinnedToCore(data_publish_task, "DataPublisher", STACK_PUBLISHER, NULL, 6, &data_pt_handle, 1);
}