#include "Data/data.h"
#include "freertos/task.h"
#include "KY037/ky037.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

#include "Setting/settings.h"
#include "System/system.h"

static const char *TAG = "KY037";


ky037_stats_t ky037_stats;    // Estructura de estadisticas

// Variables para ISR
static volatile uint32_t isr_init_high_time = 0;     // Guarda el tiempo de inicio del pulso alto
static volatile bool isr_service_installed = false;  // Flag que indica si ya se instalo el servicio de ISR del driver GPIO



/**
 * @brief ISR que maneja interrupciones del GPIO
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (task_handle.ky037_handle != NULL) {   // Solo notificar si la tarea existe
        vTaskNotifyGiveFromISR(task_handle.ky037_handle, &xHigherPriorityTaskWoken);  // Despertar a la tarea para que atienda la interrupcion
        if (xHigherPriorityTaskWoken) {   // Si la tarea que estaba ejecutandose es de menor prioridad, minimizar la latencia del context switching
            portYIELD_FROM_ISR();
        }
    }
}


/**
 * @brief Tarea que procesa las interrupciones del sensor y calcula estadisticas
 */
void ky037_task(void *pvParameters) {
    const TickType_t period_ticks = pdMS_TO_TICKS(settings.node.sample_rate * MS_TO_MIN);
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        // Calcular tiempo restante hasta el proximo envío
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - last_wake_time;
        TickType_t remaining = (elapsed < period_ticks) ? (period_ticks - elapsed) : 0;

        // Esperar notificación ISR con timeout del tiempo restante
        if (ulTaskNotifyTake(pdTRUE, remaining) > 0) {
            // --- Procesamiento de ISR ---
            int gpio_level = gpio_get_level(KY037_PIN);
            uint32_t current_time = get_time_ms();

            if (gpio_level == 1) {
                isr_init_high_time = current_time;
            }
            else if (isr_init_high_time > 0) {
                uint32_t duration = current_time - isr_init_high_time;
                ky037_stats.counter++;
                if (duration > ky037_stats.max_duration) {
                    ky037_stats.max_duration = duration;
                }
                isr_init_high_time = 0;
            }
        }

        // Verificar si cumplio el periodo (tolerancia de 1 tick)
        now = xTaskGetTickCount();
        if ((now - last_wake_time) >= period_ticks) {
            // Enviar datos
            ky037_stats_t ky037 = ky037_stats;
            xQueueSend(queues.ky037_buffer, &ky037, portMAX_DELAY);
            xEventGroupSetBits(event_group.collector_events, KY037_DATA_READY);

            // Resetear estadisticas
            ky037_stats.counter = 0;
            ky037_stats.max_duration = 0;

            // Actualizar referencia temporal
            last_wake_time += period_ticks;
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

    memset(&ky037_stats, 0, sizeof(ky037_stats_t));

    isr_init_high_time = 0;

    // Instalar servicio ISR si aún no esta instalado
    if (!isr_service_installed) {
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "- ERROR: Error instalando servicio ISR: %s -", esp_err_to_name(ret));
            vTaskDelete(task_handle.ky037_handle);
            return ret;
        }
        isr_service_installed = true;
    }

    // Añadir handler ISR para el pin KY037
    ret = gpio_isr_handler_add(KY037_PIN, gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error añadiendo ISR handler: %s -", esp_err_to_name(ret));
        vTaskDelete(task_handle.ky037_handle);
        return ret;
    }

    return ESP_OK;
}






