#ifndef KY037_H
#define KY037_H

#include "esp_err.h"
#include "driver/gpio.h"


#define KY037_PIN GPIO_NUM_4


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
// Declaraciones de tareas (para uso interno)
void vStatsTask(void *pvParameters);
void vKy037_Get_Stats_Task(void *pvParameters);



#endif //KY037_H