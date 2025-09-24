#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "KY037/ky037.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include "Settings/settings.h"
#include "Data/data.h"


static const char *TAG = "KY037";


static ky037_stats_t ky037_stats;                // Estructura de estadisticas
static TaskHandle_t xStatsTaskHandle = NULL;     // Handle de la tarea que procesa eventos (notificaciones desde ISR)
static TaskHandle_t xGetStatsTaskHandle = NULL;  // Handle de la tarea que consolida estadísticas periódicamente
static SemaphoreHandle_t xStatsMutex = NULL;     // Mutex para proteger acceso concurrente a ky037_stats

// Variables para ISR
static volatile uint32_t isr_init_high_time = 0;     // Guarda el tiempo de inicio del pulso alto
static volatile bool isr_service_installed = false;  // Flag que indica si ya se instalo el servicio de ISR del driver GPIO


/**
 * @brief Obtiene el tiempo actual en milisegundos
 */
static uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);  // /1000 para convertir de micro seg a ms
}


/**
 * @brief ISR que maneja interrupciones del GPIO
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xStatsTaskHandle != NULL) {   // Solo notificar si la tarea existe
        vTaskNotifyGiveFromISR(xStatsTaskHandle, &xHigherPriorityTaskWoken);  // Despertar a la tarea para que atienda la interrupcion
        if (xHigherPriorityTaskWoken) {   // Si la tarea que estaba ejecutandose es de menor prioridad, minimizar la latencia del context switching
            portYIELD_FROM_ISR();
        }
    }
}


/**
 * @brief Tarea que procesa las interrupciones del sensor y calcula estadisticas
 */
void vStatsTask(void *pvParameters) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // Espera indefinidamente una notificacion de la ISR
        uint32_t current_time = get_time_ms();
        int gpio_level = gpio_get_level(KY037_PIN);

        // Tomar mutex para acceso seguro a variables compartidas
        if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (gpio_level == 1) {    // Flanco de subida - inicio de detección
                isr_init_high_time = current_time;
            }
            else {
                if (isr_init_high_time > 0) {     // Flanco de bajada - fin de detección
                    uint32_t duration = current_time - isr_init_high_time;
                    ky037_stats.counter++;
                    if (duration > ky037_stats.max_duration) {
                        ky037_stats.max_duration = duration;
                    }
                    ESP_LOGI(TAG, "KY037 - Counter: %lu, Max: %lu\r\n",
                     ky037_stats.counter, ky037_stats.max_duration);
                    isr_init_high_time = 0;    // Reset
                }
            }
            xSemaphoreGive(xStatsMutex);
        }
    }
}


/**
 * @brief Tarea que retorna estadisticas periodicamente
 */
void vKy037_Get_Stats_Task(void *pvParameters) {
    const TickType_t xFrequency = pdMS_TO_TICKS(settings.sample_rate * 60000);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        if (xSemaphoreTake(xStatsMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Actualizar estadísticas consolidadas
            data_sensors.ky037_counter = ky037_stats.counter;
            data_sensors.ky037_max_duration = ky037_stats.max_duration;
            // Reset estadisticas para el siguiente período
            ky037_stats.counter = 0;
            ky037_stats.max_duration = 0;
            ESP_LOGI(TAG, "KY037 FINAL - Counter: %lu, Max: %lu\r\n",
                     data_sensors.ky037_counter, data_sensors.ky037_max_duration);
            xSemaphoreGive(xStatsMutex);
        }
    }
}


/**
 * @brief Inicializa el sensor KY037 con interrupciones en ambos flancos
 * @return esp_err_t  Devuelve ESP_OK si las configuraciones se hicieron con exito.
 */
esp_err_t ky037_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KY037_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,  // Interrupciones en ambos flancos
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error configurando GPIO: %s -", esp_err_to_name(ret));
        return ret;
    }

    xStatsMutex = xSemaphoreCreateMutex();
    if (xStatsMutex == NULL) {
        ESP_LOGE(TAG, "- ERROR: Error creando semaforo -");
        return ESP_ERR_NO_MEM;
    }

    memset(&ky037_stats, 0, sizeof(ky037_stats_t));
    isr_init_high_time = 0;

    BaseType_t task_result = xTaskCreate(
        vStatsTask,
        "ky037_stats",
        2048,
        NULL,
        5,
        &xStatsTaskHandle
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "- ERROR: Error creando tarea vStatsTask -");
        vSemaphoreDelete(xStatsMutex);
        return ESP_ERR_NO_MEM;
    }

    task_result = xTaskCreate(
        vKy037_Get_Stats_Task,
        "ky037_get_stats",
        2048,
        NULL,
        4,
        &xGetStatsTaskHandle
    );

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "- ERROR: Error creando tarea vKy037_Get_Stats_Task -");
        vTaskDelete(xStatsTaskHandle);
        vSemaphoreDelete(xStatsMutex);
        return ESP_ERR_NO_MEM;
    }

    if (!isr_service_installed) {
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "- ERROR: Error instalando servicio ISR: %s -", esp_err_to_name(ret));
            vTaskDelete(xStatsTaskHandle);
            vTaskDelete(xGetStatsTaskHandle);
            vSemaphoreDelete(xStatsMutex);
            return ret;
        }
        isr_service_installed = true;
    }

    ret = gpio_isr_handler_add(KY037_PIN, gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error añadiendo ISR handler: %s", esp_err_to_name(ret));
        vTaskDelete(xStatsTaskHandle);
        vTaskDelete(xGetStatsTaskHandle);
        vSemaphoreDelete(xStatsMutex);
        return ret;
    }
    return ESP_OK;
}



