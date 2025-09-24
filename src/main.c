#include "Settings/settings.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "MQTT/mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"




void app_main(void) {
    xTaskCreate(uart_config_task, "uart_config_task", 4096, NULL, 5, NULL);
    dht11_init();
    ky037_init();
    vTaskDelay(pdMS_TO_TICKS(1000));  // Pausa para estabilizacion
}