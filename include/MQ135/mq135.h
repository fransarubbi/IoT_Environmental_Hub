#ifndef MQ135_H
#define MQ135_H


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



/* ----- API publica minima ----- */
esp_err_t mq135_init(void);
float mq135_read_ppm(float temperature_c, float humidity_percent);
void mq135_print_diagnostics(float temperature_c, float humidity_percent);



#endif // MQ135_H