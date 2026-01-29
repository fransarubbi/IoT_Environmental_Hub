#ifndef MQ135_H
#define MQ135_H


#include <freertos/queue.h>
#include <freertos/task.h>
#include "esp_err.h"


/* ----- Configuracion hardware y muestreo ----- */
#define MQ135_VCC            5.0f       // V alimentacion del modulo
#define MQ135_RLOAD          10000.0f   // ohmios
#define MQ135_NUMBER_OF_SAMPLES 128     // muestras ADC para promediar (ajustable)
#define MQ135_SAMPLE_DELAY_MS  10    // ms entre muestras (ajustable)

/* ----- Parametros CO2 (curva) ----- */
#define CO2_A 114.0f
#define CO2_B 2.88f
#define ATM_CO2 425.0f
#define CO2_R0_DEFAULT 61534.0f

/* ----- Correccion temperatura/humedad (comunmente usada) ----- */
#define CORA 0.00035f
#define CORB 0.02718f
#define CORC 1.39538f
#define CORD 0.0018f
#define EMA_ALPHA_Q15 1638
#define EMA_2_15 32768

#define MQ135_JSON_ALERT 100
#define MQ135_DELAY 10000
#define MQ135_DATA_READY  (1 << 1)

#define STACK_MQ135 8000
#define MPACK_MQ135_ALERT_SIZE 1024
#define MQ135_LOW_DELAY            10000    // 10 seg
#define MQ135_BALANCED_DELAY       5000     // 5 seg
#define MQ135_PERFORMANCE_DELAY    2000     // 2 seg

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
float mq135_read_ppm(float temperature_c, float humidity_percent);
void mq135_print_diagnostics(float temperature_c, float humidity_percent);



#endif // MQ135_H