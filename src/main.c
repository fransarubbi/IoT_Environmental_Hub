#include "Settings/settings.h"
#include "nvs_flash.h"
#include "MQTT/mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"



static const char *TAG = "MAIN";



/*void dht11_task(void *pvParameters) {
    ESP_LOGI(TAG, "INFO: Iniciando tarea DHT11...");
    while (1) {
        // Leer datos del sensor
        esp_err_t ret = dht11_read_data();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ERROR: No se pudieron leer los datos.");
        }
        else {
            ESP_LOGI(TAG, "DHT11 - Temperatura: %d.%d°C, Humedad: %d.%d%%",
                     dht11_data.temperature, dht11_data.temp_decimal,
                     dht11_data.humidity, dht11_data.hum_decimal);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}*/



void uart_config_task(void *pvParameters) {
    // 1. Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Cargar configuración si existe
    if (!setting_load_from_nvs()) {   //    No existe
        bool flag = false;
        while (!flag) {
            flag = setting_mode_start();
            vTaskDelay(10 / portTICK_PERIOD_MS); // Ceder CPU y evitar watchdog
        }
    }
    else {   // Existe
        ret = uart_init();
    }
    show_config();
    vTaskDelete(NULL); // Termina esta tarea
}



void app_main(void) {
    dht11_init();
    ky037_init();

    vTaskDelay(pdMS_TO_TICKS(1000));  // Pausa para estabilizacion

    xTaskCreate(uart_config_task, "uart_config_task", 4096, NULL, 5, NULL);
}