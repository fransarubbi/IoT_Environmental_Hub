#ifndef MQ135_H
#define MQ135_H


/* ----- Configuracion de hardware ----- */
#define VCC               5.0f             // V (alimentación modulo MQ135)
#define RLOAD             12800.0f         // ohmios (depende del modulo)
#define RELATIVE_HUMIDITY 20.0f            // Valor de humedad cuando se calibro
#define NUMBER_OF_SAMPLES  128              // cantidad de lecturas para promediar
/* ----- Concentracion atmosferica para cada gas (ppm) ----- */
#define ATM_CO2 425       // Dioxido de Carbono (CO₂)
#define ATM_CO 0.1f        // Monóxido de Carbono (CO)
#define ATM_NH3 0.5f       // Amoniaco (NH3)
#define ATM_C6H6 0.005f        // Benceno (C₆H₆)
#define ATM_VOC 1.0f        // Oxido de nitrógeno (NO2)
/* ----- Parametros ----- */
#define CO2_A 114.0f
#define CO2_B 2.88f
#define CO_A 569.0f
#define CO_B 3.93f
#define NH3_A 101.0f
#define NH3_B 2.49f
#define C6H6_A 77.7f
#define C6H6_B 3.19f
#define VOC_A 34.8f
#define VOC_B 3.43f
/* ----- Resistencia de calibracion a nivel de CO atmosferico ----- */
#define CO2_R0 39238.0f
#define CO_R0 2745.0f
#define NH3_R0 2567.0f
#define C6H6_R0 1183.0f
#define VOC_R0 15047.0f
/* ----- Coeficientes para correccion ----- */
#define CORA 0.00035f
#define CORB 0.02718f
#define CORC 1.39538f
#define CORD 0.0018f
/* ----- EMA ----- */
#define EMA_ALPHA_Q15  1638   // α = 0.05 en Q15 = 0.05 * 32768 aprox 1638
#define EMA_2_15  32768       // 2^15



#include "esp_err.h"
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

extern gas_t co2, co, nh3, c6h6, voc;


/* ----- Funciones inline ----- */
static inline float mq135_get_R0(float resistance, const gas_t* params) {
    return resistance * powf(params->atm_div_A, params->inv_B);
}


static inline float mq135_get_ppm(float resistance, float R0, const gas_t* params) {
    return params->A * powf(resistance / R0, params->neg_B);
}


/* ----- Declaraciones de funciones de la API ----- */
esp_err_t mq135_init();
void mq135_init_gas();
float mq135_get_corrected_resistance(float, float);
float mq135_get_corrected_R0(float, float, const gas_t*);
float mq135_get_corrected_ppm(float, float, const gas_t*);


#endif //MQ135_H