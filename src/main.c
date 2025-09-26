#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "Data/data.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"




static const char *TAG = "MAIN";
SemaphoreHandle_t config_done_sem;
QueueHandle_t queue_data = NULL;


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

    queue_data = xQueueCreate(10, sizeof(data_sensors_t));
    if (queue_data == NULL) {
        ESP_LOGE(TAG, "- ERROR: Error creando la cola de sensores -");
        return;
    }

    // Crear tarea de configuracion UART
    xTaskCreate(uart_config_task, "uart_config_task", 4096, NULL, 6, NULL);

    // Esperar a que la configuracion este lista
    if (xSemaphoreTake(config_done_sem, portMAX_DELAY)) {
        dht11_init();
        ky037_init();
        xTaskCreate(data_json_encrypt_task, "data_json_encrypt_task", 4096, NULL, 6, NULL);
    }
}