#ifndef MQ135_H
#define MQ135_H

#include <freertos/queue.h>
#include <freertos/task.h>
#include "esp_err.h"

#define MQ135_DELAY 10000
#define MQ135_DATA_READY  (1 << 1)

/* ─────────────────────────────────────────────
 * Pines y canal ADC
 * ───────────────────────────────────────────── */
#define MQ135_ADC_UNIT          ADC_UNIT_1
#define MQ135_ADC_CHANNEL       ADC_CHANNEL_6
#define MQ135_ADC_ATTEN         ADC_ATTEN_DB_12
#define MQ135_ADC_BITWIDTH      ADC_BITWIDTH_12

/* ─────────────────────────────────────────────
 * Parámetros del circuito
 * ───────────────────────────────────────────── */
#define MQ135_RLOAD_KOHM        10.0f    
#define MQ135_VCC               5.0f    

/* ─────────────────────────────────────────────
 * Parámetros de Calidad de Aire (Índice VOC)
 * ───────────────────────────────────────────── */
#define MQ135_RATIO_CLEAN       3.6f   /*!< Rs/R0 en aire limpio (100%) */
#define MQ135_RATIO_DIRTY       1.0f   /*!< Rs/R0 en aire viciado (0%) */

/* ─────────────────────────────────────────────
 * Parámetros del filtro y tiempos
 * ───────────────────────────────────────────── */
#define MQ135_MEDIAN_WINDOW     9       

#define MQ135_LOW_DELAY            10000    
#define MQ135_BALANCED_DELAY       5000     
#define MQ135_PERFORMANCE_DELAY    2000     

/* ─────────────────────────────────────────────
 * Estructuras de configuración / estado
 * ───────────────────────────────────────────── */
typedef struct {
    float    rzero_kohm;       
    float    rload_kohm;       
    float    ema_alpha;        
    float    ema_value;        
    bool     ema_initialized;  
} mq135_config_t;

/* ===== Estructura de datos ===== */
typedef struct {
    float air_quality; // Rango: 0.0 a 100.0 (%)
} mq135_data_t;

typedef struct {
    float quality_i; // Calidad inicial al saltar alerta
    float quality_a; // Calidad actual
} mq135_alert_t;

typedef enum{INIT_MQ135, NORMAL_MQ135, ALERT_MQ135} state_mq135_t;

/* ----- API publica ----- */
void mq135_task(void *);
esp_err_t mq135_init(void);

#endif // MQ135_H