#ifndef MQ135_H
#define MQ135_H


/* ----- Configuracion de hardware ----- */
#define MQ135_ADC_CHANNEL ADC1_CHANNEL_6   // GPIO34
#define ADC_WIDTH         ADC_WIDTH_BIT_12 // 12 bits: 0–4095
#define ADC_ATTEN         ADC_ATTEN_DB_12  // permite medir hasta ~3.3V
#define VREF              3300             // mV (3.3 V ref)
#define VCC               5.0f             // V (alimentación modulo MQ135)
#define RLOAD             10000.0f         // ohmios (depende del modulo)
#define RESOLUTION        4095.0f          // 2^12 - 1
#define RELATIVE_HUMIDITY 33.0f            // Humedad relativa cuando se calibro el sensor
#define DEFAULT_VREF      1100             // mV
#define NUMBER_OF_SAMPLES  64              // cantidad de lecturas para promediar
/* ----- Concentracion atmosferica para cada gas (ppm) ----- */
#define ATM_CO2 400       // Dioxido de Carbono (CO₂)
#define ATM_CO 0.1        // Monóxido de Carbono (CO)
#define ATM_NH3 0.5       // Amoniaco (NH3)
#define ATM_C6H6 5        // Benceno (C₆H₆)
#define ATM_NO2 10        // Oxido de nitrógeno (NO2)
/* ----- Parametros ----- */
#define CO2_A 116.6020682
#define CO2_B 2.769034857
#define CO_A 522.6779
#define CO_B 3.8465
#define NH3_A 78.9230
#define NH3_B 3.2435
#define C6H6_A 101.3708
#define C6H6_B 2.5082
#define NO2_A 45.0673
#define NO2_B 3.4835
/* ----- Resistencia de calibracion a nivel de CO atmosférico ----- */
#define CO2_R0 930
#define CO_R0 930
#define NH3_R0 930
#define C6H6_R0 930
#define NO2_R0 930
/* ----- Coeficientes para correccion ----- */
#define CORA 0.00035
#define CORB 0.02718
#define CORC 1.39538
#define CORD 0.0018
/* ----- EMA ----- */
#define EMA_ALPHA_Q15  3277   // α = 0.1 en Q15 = 0.1 * 32768 aprox 3277
#define EMA_2_15  32768       // 2^15

#include <stdint.h>
#include <math.h>



/* ----- Estructura para parametros de los gases ----- */
typedef struct {
    float A;
    float B;
    float ATM;
    float inv_B;        // Precalculado: 1/B
    float neg_B;        // Precalculado: -B
    float atm_div_A;    // Precalculado: ATM/A
    float R0;
} gas_t;

extern gas_t co2, co, nh3, c6h6, no2;


/* ----- Funciones inline ----- */
static inline float mq135_get_R0(float resistance, const gas_t* params) {
    return resistance * powf(params->atm_div_A, params->inv_B);
}

static inline float mq135_get_ppm(float resistance, float R0, const gas_t* params) {
    return params->A * powf(resistance / R0, params->neg_B);
}


/* ----- Declaraciones de funciones de la API ----- */
void mq135_init_gas();
void mq135_init();
float mq135_get_corrected_resistance(float, float);
float mq135_get_corrected_R0(float, float, const gas_t*);
float mq135_get_corrected_ppm(float, float, const gas_t*);
void mq135_clear_cache();



#endif //MQ135_H