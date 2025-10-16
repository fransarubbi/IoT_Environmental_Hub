#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "Data/data.h"
#include "MQ135/mq135.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "Wifi/wifi.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"



static const char *TAG = "MAIN";
SemaphoreHandle_t config_done_sem;


/**
 * @brief Tarea de configuracion del sistema a traves de UART
 * @param pvParameters
 */
static void uart_config_task(void *pvParameters) {
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
    xSemaphoreGive(config_done_sem);   // Cuando esta todo listo, liberar el semaforo
    vTaskDelete(NULL); // Termina esta tarea
}





void app_main(void) {
    config_done_sem = xSemaphoreCreateBinary();
    if (config_done_sem == NULL) {
        ESP_LOGE(TAG, "- ERROR: Error creando el semaforo -");
        return;
    }

    // Crear tarea de configuracion UART
    xTaskCreate(uart_config_task, "uart_config_task", 4096, NULL, 6, NULL);

    // Esperar a que la configuracion este lista
    if (xSemaphoreTake(config_done_sem, portMAX_DELAY)) {
        esp_err_t retWiFi = wifi_init();
        if (retWiFi == ESP_OK) {
            esp_err_t retLittleFS = littlefs_init();
            esp_err_t retMQTT = mqtt_init();
            if (retMQTT == ESP_OK && retLittleFS == ESP_OK) {
                esp_err_t retMQ135 = mq135_init();
                esp_err_t retDHT11 = dht11_init();
                esp_err_t retKY037 = ky037_init();

                if (retMQ135 == ESP_OK && retDHT11 == ESP_OK && retKY037 == ESP_OK) {
                    //vTaskDelay(pdMS_TO_TICKS(240000));   // 4 minutos de estabilizacion del mq135
                    xTaskCreate(data_json_encrypt_task, "data_json_encrypt_task", 4096, NULL, 6, NULL);
                }
            }
        }
    }
}