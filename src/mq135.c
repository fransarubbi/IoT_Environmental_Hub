#include "MQ135/mq135.h"
#include "ADC_MANAGER/adc_manager.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <math.h>


/* ----- Cache para valores calculados ----- */
static float cached_correction_factor = -1.0f;
static float last_temp = -999.0f;
static float last_humidity = -999.0f;

/* ----- Constantes precalculadas para evitar divisiones repetitivas ----- */
static esp_adc_cal_characteristics_t adc_chars;

/* ----- Inicializar parametros precalculados ----- */
gas_t co2, co, nh3, c6h6, no2;
static bool gas_initialized = false;

/* ----- Variables para EMA ----- */
static int64_t ema_value_q15 = 0;
static bool ema_initialized_q15 = false;



/* ----- Precargar valores de gases ----- */
void mq135_init_gas() {
    if (gas_initialized) return;

    co2.A = CO2_A;
    co2.B = CO2_B;
    co2.ATM = ATM_CO2;
    co2.inv_B = 1.0f / CO2_B;
    co2.neg_B = -CO2_B;
    co2.atm_div_A = ATM_CO2 / CO2_A;
    co2.R0 = CO2_R0;

    co.A = CO_A;
    co.B = CO_B;
    co.ATM = ATM_CO;
    co.inv_B = 1.0f / CO_B;
    co.neg_B = -CO_B;
    co.atm_div_A = ATM_CO / CO_A;
    co.R0 = CO_R0;

    nh3.A = NH3_A;
    nh3.B = NH3_B;
    nh3.ATM = ATM_NH3;
    nh3.inv_B = 1.0f / NH3_B;
    nh3.neg_B = -NH3_B;
    nh3.atm_div_A = ATM_NH3 / NH3_A;
    nh3.R0 = NH3_R0;

    c6h6.A = C6H6_A;
    c6h6.B = C6H6_B;
    c6h6.ATM = ATM_C6H6;
    c6h6.inv_B = 1.0f / C6H6_B;
    c6h6.neg_B = -C6H6_B;
    c6h6.atm_div_A = ATM_C6H6 / C6H6_A;
    c6h6.R0 = C6H6_R0;

    no2.A = NO2_A;
    no2.B = NO2_B;
    no2.ATM = ATM_NO2;
    no2.inv_B = 1.0f / NO2_B;
    no2.neg_B = -NO2_B;
    no2.atm_div_A = ATM_NO2 / NO2_A;
    no2.R0 = NO2_R0;

    gas_initialized = true;
}


/* ----- Funcion del filtro EMA Q15 ----- */
static uint16_t ema_filter_q15(const uint16_t new_sample) {
    if (!ema_initialized_q15) {
        ema_value_q15 = ((int64_t)new_sample) << 15;
        ema_initialized_q15 = true;
        return new_sample;
    }
    // ema = alpha*sample + (1-alpha)*ema
    int64_t sample_q15 = ((int64_t)new_sample) << 15;
    ema_value_q15 = ((EMA_ALPHA_Q15 * sample_q15) +
                        ((EMA_2_15 - EMA_ALPHA_Q15) * ema_value_q15)) >> 15;

    return (uint16_t)(ema_value_q15 >> 15);
}


void mq135_init() {
    // Configuro ancho y atenuación del ADC
    adc1_config_channel_atten(MQ135_ADC_CHANNEL, ADC_ATTEN_DB_12);
    // Caracterizo el ADC para corregir Vref y curva
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12,
                             ADC_WIDTH_BIT_12, DEFAULT_VREF, &adc_chars);
    mq135_init_gas();
}


static float mq135_get_resistance() {
    uint16_t adc_reading = 0;
    for (uint8_t i = 0; i < NUMBER_OF_SAMPLES; i++) {
        uint16_t raw = adc1_get_raw(MQ135_ADC_CHANNEL);
        adc_reading = ema_filter_q15(raw);
        vTaskDelay(pdMS_TO_TICKS(1));   // Delay no bloqueante de 1ms (convierte ms a ticks)
    }
    // Convertir a voltaje corregido
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    float vout = (float)voltage_mv / 1000.0f;  // En voltios
    if (vout < 0.001f) return INFINITY;        // Evitar division por cero
    float rs = RLOAD * ((VCC / vout) - 1.0f);
    return rs;
}


static float mq135_get_correction_factor(float temperature, float humidity) {
    // Cache del factor de correccion
    if (fabsf(temperature - last_temp) < 0.1f && fabsf(humidity - last_humidity) < 0.1f) {
        if (cached_correction_factor >= 0) return cached_correction_factor;
    }

    float temp_squared = temperature * temperature;
    float humidity_diff = humidity - RELATIVE_HUMIDITY;
    float correction = CORA * temp_squared - CORB * temperature + CORC - humidity_diff * CORD;

    cached_correction_factor = correction;
    last_temp = temperature;
    last_humidity = humidity;
    return correction;
}


float mq135_get_corrected_resistance(float temperature, float humidity) {
    return mq135_get_resistance() / mq135_get_correction_factor(temperature, humidity);
}


float mq135_get_corrected_R0(float temperature, float humidity, const gas_t* params) {
    return mq135_get_R0(mq135_get_corrected_resistance(temperature, humidity), params);
}


float mq135_get_corrected_ppm(float temperature, float humidity, const gas_t* params) {
    float corrected_resistance = mq135_get_corrected_resistance(temperature, humidity);
    return mq135_get_ppm(corrected_resistance, params->R0, params);
}


/* ----- Función para limpiar cache ----- */
void mq135_clear_cache() {
    cached_correction_factor = -1.0f;
    last_temp = -999.0f;
    last_humidity = -999.0f;
}










