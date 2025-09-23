#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "KY037/ky037.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"


static const char *TAG = "KY037";


// Variables estáticas
static ky037_stats_t ky037_stats = {0};
static bool last_state = false;
static uint32_t last_change_time = 0;
static void (*sound_callback)(bool) = NULL;


/**
 * @brief Obtiene el tiempo actual en milisegundos
 */
static uint32_t get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void ky037_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KY037_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando pin KY037: %s", esp_err_to_name(ret));
        return;
    }

    // Inicializar estadísticas
    ky037_stats.total_detections = 0;
    ky037_stats.last_detection_time = 0;
    ky037_stats.detection_duration = 0;
    ky037_stats.sound_active = false;

    // Estado inicial
    last_state = ky037_digital_read();
    last_change_time = get_time_ms();

    ESP_LOGI(TAG, "KY037 inicializado en GPIO %d", KY037_PIN);
}

bool ky037_digital_read(void) {
    return (gpio_get_level(KY037_PIN) == 1);
}

bool ky037_digital_read_debounced(void) {
    bool current_state = ky037_digital_read();
    uint32_t current_time = get_time_ms();

    // Si el estado cambió
    if (current_state != last_state) {
        // Si ha pasado suficiente tiempo desde el último cambio
        if ((current_time - last_change_time) >= KY037_DEBOUNCE_TIME_MS) {
            // Actualizar estadísticas
            if (current_state && !last_state) {
                // Detección de sonido iniciada
                ky037_stats.total_detections++;
                ky037_stats.last_detection_time = current_time;
                ky037_stats.sound_active = true;

                // Llamar callback si está configurado
                if (sound_callback != NULL) {
                    sound_callback(true);
                }

                ESP_LOGI(TAG, "Sonido detectado (detección #%lu)",
                        ky037_stats.total_detections);
            }
            else if (!current_state && last_state) {
                // Fin de detección de sonido
                ky037_stats.detection_duration = current_time - ky037_stats.last_detection_time;
                ky037_stats.sound_active = false;

                // Llamar callback si está configurado
                if (sound_callback != NULL) {
                    sound_callback(false);
                }

                ESP_LOGI(TAG, "Fin de detección (duración: %lu ms)",
                        ky037_stats.detection_duration);
            }

            last_state = current_state;
            last_change_time = current_time;
        }
    }

    return last_state;
}

ky037_stats_t* ky037_get_stats(void) {
    return &ky037_stats;
}

void ky037_reset_stats(void) {
    ky037_stats.total_detections = 0;
    ky037_stats.last_detection_time = 0;
    ky037_stats.detection_duration = 0;
    ky037_stats.sound_active = false;
    ESP_LOGI(TAG, "Estadísticas reseteadas");
}

void ky037_set_callback(void (*callback)(bool sound_detected)) {
    sound_callback = callback;
    ESP_LOGI(TAG, "Callback %s", callback ? "configurado" : "eliminado");
}