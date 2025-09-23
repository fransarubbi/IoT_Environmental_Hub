// ky037.h - Mejorado
#ifndef KY037_H
#define KY037_H

#include "esp_err.h"
#include <stdbool.h>
#include "driver/gpio.h"


#define KY037_PIN GPIO_NUM_4

// Configuraciones adicionales
#define KY037_DEBOUNCE_TIME_MS  50    // Tiempo de debounce en ms
#define KY037_DETECTION_WINDOW  100   // Ventana de detección en ms


// Estados del sensor
typedef enum {
    KY037_NO_SOUND = 0,
    KY037_SOUND_DETECTED = 1
} ky037_state_t;


// Estructura para estadísticas
typedef struct {
    uint32_t total_detections;      // Total de detecciones
    uint32_t last_detection_time;   // Último tiempo de detección (ms)
    uint32_t detection_duration;    // Duración de la última detección
    bool sound_active;              // Estado actual del sonido
} ky037_stats_t;


/**
 * @brief Inicializa el sensor KY037 en modo digital
 * @return ESP_OK si es exitoso
 */
void ky037_init(void);

/**
 * @brief Lee el estado digital del sensor (sin debounce)
 * @return true si hay sonido, false si no
 */
bool ky037_digital_read(void);

/**
 * @brief Lee el estado digital con debounce
 * @return true si hay sonido confirmado, false si no
 */
bool ky037_digital_read_debounced(void);

/**
 * @brief Obtiene las estadísticas del sensor
 * @return Puntero a estructura de estadísticas
 */
ky037_stats_t* ky037_get_stats(void);

/**
 * @brief Resetea las estadísticas del sensor
 */
void ky037_reset_stats(void);

/**
 * @brief Configura callback para detección de sonido
 * @param callback Función a llamar cuando se detecte sonido
 */
void ky037_set_callback(void (*callback)(bool sound_detected));

#endif //KY037_H