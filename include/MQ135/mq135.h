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
#define MQ135_RLOAD_KOHM        1.0f    /*!< Resistencia de carga en kΩ */
#define MQ135_VCC               5.0f    /*!< Tensión de alimentación del módulo (V) */

/* ─────────────────────────────────────────────
 * Calibración del sensor
 * ───────────────────────────────────────────── */
#define MQ135_RZERO_KOHM        8.51f   /*!< Rs en aire limpio medido en runtime (kΩ) */

/* ─────────────────────────────────────────────
 * Curva CO2 – ecuación: PPM = PARA * (Rs/R0)^PARB
 * Derivados de la hoja de datos del MQ135
 * ───────────────────────────────────────────── */
#define MQ135_PARA              116.6020682f
#define MQ135_PARB              (-2.769034857f)

/* CO2 atmosférico de referencia (ppm) */
#define MQ135_ATMOCO2           400.0f

/* ─────────────────────────────────────────────
 * Parámetros del filtro
 * ───────────────────────────────────────────── */
#define MQ135_MEDIAN_WINDOW     9       /*!< Muestras para filtro de mediana */
#define MQ135_EMA_ALPHA         0.2f    /*!< Factor EMA: 0=muy suave … 1=sin filtro */

/* ─────────────────────────────────────────────
 * Rangos válidos de salida
 * ───────────────────────────────────────────── */
#define MQ135_PPM_MIN           10.0f
#define MQ135_PPM_MAX           10000.0f

#define MQ135_LOW_DELAY            10000    // 10 seg
#define MQ135_BALANCED_DELAY       5000     // 5 seg
#define MQ135_PERFORMANCE_DELAY    2000     // 2 seg


/* ─────────────────────────────────────────────
 * Estructura de configuración / estado
 * ───────────────────────────────────────────── */
typedef struct {
    float    rzero_kohm;       /*!< Rzero calibrado (kΩ) */
    float    rload_kohm;       /*!< Resistencia de carga (kΩ) */
    float    ema_alpha;        /*!< Factor de suavizado EMA */
    float    ema_value;        /*!< Último valor EMA (estado interno) */
    bool     ema_initialized;  /*!< Indica si EMA ya tiene primer valor */
} mq135_config_t;


/* ===== Estructura de datos ===== */
typedef struct {
    float co2ppm;
} mq135_data_t;

typedef struct {
    float co2ppm_i;
    float co2ppm_a;
} mq135_alert_t;



typedef enum{INIT_MQ135, NORMAL_MQ135, ALERT_MQ135} state_mq135_t;


/* ----- API publica ----- */
void mq135_task(void *);
esp_err_t mq135_init(void);



#endif // MQ135_H