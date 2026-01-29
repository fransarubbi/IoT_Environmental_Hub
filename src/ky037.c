/**
* @file ky037.c
 * @brief Driver y gestión de tarea para el sensor de sonido KY-037.
 *
 * Implementa una arquitectura de alta eficiencia donde el conteo de pulsos
 * y medición de duración se realiza estrictamente dentro de la ISR,
 * mientras que la tarea de FreeRTOS actúa como coordinadora para reportar
 * los datos periódicamente sin saturar la CPU con cambios de contexto.
 */


#include "Data/data.h"
#include "freertos/task.h"
#include "KY037/ky037.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_timer.h"
#include "Fsm/fsm.h"
#include "Setting/settings.h"
#include "System/system.h"


static const char *TAG = "KY037";


// Estructura volátil para compartir datos entre ISR y Tarea
typedef struct {
    volatile uint32_t counter;
    volatile uint32_t max_duration_us;
    volatile uint64_t start_time_us;
} ky037_isr_data_t;


static ky037_isr_data_t isr_data = {0};
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED; // Para leer/resetear atómicamente


/**
 * @brief Manejador de Interrupciones (ISR) del GPIO.
 *
 * Se ejecuta en cada cambio de estado (Any Edge) del pin del sensor.
 * Realiza cálculos matemáticos rápidos para determinar la duración del pulso
 * y contar eventos sin despertar a la tarea principal, ahorrando CPU.
 *
 * @param arg Argumento de usuario (no utilizado en este caso).
 */
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    int level = gpio_get_level(KY037_PIN);
    uint64_t now = esp_timer_get_time();  // Tiempo en microsegundos

    if (level == 1) {
        isr_data.start_time_us = now;  // Flanco de subida, guardamos inicio
    } else {   // Flanco de bajada, calculamos duración
        if (isr_data.start_time_us > 0) {
            uint32_t duration = (uint32_t)(now - isr_data.start_time_us);

            // Actualizamos estadísticas
            isr_data.counter++;
            if (duration > isr_data.max_duration_us) {
                isr_data.max_duration_us = duration;
            }
            isr_data.start_time_us = 0;
        }
    }
}


/**
 * @brief Tarea principal de gestión del sensor KY-037.
 *
 * Implementa un patrón de doble bucle:
 * 1. Espera pasiva de notificación START.
 * 2. Bucle activo de muestreo periódico.
 *
 * La tarea duerme durante el tiempo configurado en los settings. Al despertar,
 * toma los datos acumulados por la ISR de forma atómica, los envía a la cola
 * del sistema y reinicia los contadores internos.
 *
 * @param pvParameters Parámetros de creación de la tarea (no usado).
 */
void ky037_task(void *pvParameters) {
    ky037_t ky037_msg;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;

            portENTER_CRITICAL(&spinlock);
            memset((void*)&isr_data, 0, sizeof(isr_data));
            portEXIT_CRITICAL(&spinlock);

            while (running) {
                uint32_t sample_rate = settings_get_node_sample_rate();
                if(sample_rate == 0) sample_rate = 1;

                TickType_t delay_ticks = pdMS_TO_TICKS(sample_rate * 60000);

                uint32_t stop_signal = 0;
                BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &stop_signal, delay_ticks);

                if (result == pdFALSE) {
                    portENTER_CRITICAL(&spinlock);
                    ky037_msg.counter = isr_data.counter;
                    ky037_msg.max_duration = isr_data.max_duration_us / 1000; // Convertir us a ms

                    isr_data.counter = 0;
                    isr_data.max_duration_us = 0;
                    portEXIT_CRITICAL(&spinlock);

                    // Enviar a la cola (con timeout corto para no bloquear)
                    if (xQueueSend(queues.ky037_buffer, &ky037_msg, pdMS_TO_TICKS(100)) == pdTRUE) {
                        xEventGroupSetBits(event_group.collector_events, KY037_DATA_READY);
                    } else {
                        ESP_LOGW(TAG, "Cola llena, dato descartado");
                    }

                } else {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        running = false;
                    }
                }
            }
        }
    }
}


/**
 * @brief Inicializa el hardware y recursos para el sensor KY-037.
 *
 * Configura el GPIO, inicializa estructuras de memoria y registra
 * el manejador de interrupciones (ISR).
 *
 * @return esp_err_t ESP_OK si todo es correcto, o código de error.
 */
esp_err_t ky037_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KY037_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;

    // Reset de estructura
    portENTER_CRITICAL(&spinlock);
    memset((void*)&isr_data, 0, sizeof(isr_data));
    portEXIT_CRITICAL(&spinlock);

    // Instalación de ISR
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    ret = gpio_isr_handler_add(KY037_PIN, gpio_isr_handler, NULL);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}


/**
 * @brief Obtiene el valor del contador de un objeto ky037_t.
 * @param ky037 Puntero con los datos.
 * @return Número de eventos detectados.
 */
uint32_t ky037_get_counter(const ky037_t *ky037) {
    return ky037->counter;
}


/**
 * @brief Obtiene la duración máxima registrada de un objeto ky037_t.
 * @param ky037 Puntero con los datos.
 * @return Duración máxima en milisegundos.
 */
uint32_t ky037_get_duration(const ky037_t *ky037) {
    return ky037->max_duration;
}


/**
 * @brief Obtiene el tamaño de la estructura ky037_t.
 * Útil para serialización o manejo de memoria dinámica genérica.
 * @return Tamaño en bytes.
 */
size_t ky037_get_size(void) {
    return sizeof(ky037_t);
}