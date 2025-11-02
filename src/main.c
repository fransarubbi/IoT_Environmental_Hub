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
QueueHandle_t dht11_buffer = NULL;
TaskHandle_t data_ct_handle = NULL;
TaskHandle_t data_pt_handle = NULL;
TaskHandle_t xStatsTaskHandle = NULL;
TaskHandle_t dht11_handle = NULL;
EventGroupHandle_t collector_events = NULL;


void app_main(void) {

    data_buffer = xQueueCreate(QUEUE_LENGTH, sizeof(char *));
    system_buffer = xQueueCreate(QUEUE_LENGTH, sizeof(char *));
    dht11_buffer = xQueueCreate(QUEUE_DHT11, sizeof(dht11_data_t));

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

    collector_events = xEventGroupCreate();
    if (!collector_events) return;

    xTaskCreatePinnedToCore(dht11_task, "DHT11Task", STACK_DHT11, NULL, 3, &dht11_handle, 1);
    xTaskCreatePinnedToCore(data_collection_task, "DataCollector", STACK_COLLECTOR, NULL, 4, &data_ct_handle, 1);
    xTaskCreatePinnedToCore(data_publish_task, "DataPublisher", STACK_PUBLISHER, NULL, 4, &data_pt_handle, 0);
    xTaskCreatePinnedToCore(stack_monitor_task, "StackMonitor", STACK_MONITOR, NULL, 3, NULL, 0);
}