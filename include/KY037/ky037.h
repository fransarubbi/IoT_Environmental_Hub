#ifndef KY037_H
#define KY037_H

#include <esp_timer.h>
#include "esp_err.h"


#define KY037_PIN GPIO_NUM_5
#define KY037_DATA_READY (1 << 2)


/* ----- Estructura para estadísticas internas (usada por la ISR) ----- */
typedef struct {
    uint32_t counter;              // Contador de detecciones
    uint32_t max_duration;         // Duración máxima en el período
    uint32_t init_high_time;       // Tiempo de inicio de nivel alto
} ky037_stats_t;


/* ----- Estructura para estadisticas consolidadas (thread-safe) ----- */
typedef struct {
    uint32_t counter;         // Total de detecciones en el período
    uint32_t max_duration;    // Duración máxima en milisegundos
} ky037_t;



esp_err_t ky037_init(void);
void ky037_task(void *);
uint32_t ky037_get_counter(const ky037_t *ky037);
uint32_t ky037_get_duration(const ky037_t *ky037);
size_t ky037_get_size(void);

#endif //KY037_H