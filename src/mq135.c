#include "MQ135/mq135.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"



gas_t co2, co, nh3, c6h6, voc;
static bool gas_initialized = false;

/* ----- Variables para EMA ----- */
static int64_t ema_value_q15 = 0;
static bool ema_initialized_q15 = false;

static adc_oneshot_unit_handle_t adc1_handle;
static adc_oneshot_unit_init_cfg_t init_config1;
static adc_oneshot_chan_cfg_t adc_chan;
static adc_cali_handle_t cali_handle = NULL;
static adc_cali_line_fitting_config_t cali_config;



/**
 * @brief Realiza el precalculo de los parametros de los gases.
 */
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

    voc.A = VOC_A;
    voc.B = VOC_B;
    voc.ATM = ATM_VOC;
    voc.inv_B = 1.0f / VOC_B;
    voc.neg_B = -VOC_B;
    voc.atm_div_A = ATM_VOC / VOC_A;
    voc.R0 = VOC_R0;

    gas_initialized = true;
}


/**
 * @brief Filtro EMA Q15 para suavizar muestras del ADC.
 * @param new_sample Muestra tomada del ADC
 * @return Valor filtrado
 */
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


/**
 * @brief Inicializa el sensor MQ135 y la configuracion del ADC.
 * @return ESP_OK si todo se configura correctamente, sino retorna un mensaje de fallo.
 */
esp_err_t mq135_init(void) {
    memset(&init_config1, 0, sizeof(init_config1));
    init_config1.unit_id = ADC_UNIT_1;
    init_config1.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) return ret;

    // Configuración del canal (GPIO34 -> ADC1_CHANNEL_6)
    memset(&adc_chan, 0, sizeof(adc_chan));
    adc_chan.atten = ADC_ATTEN_DB_12;
    adc_chan.bitwidth = ADC_BITWIDTH_12;
    ret = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &adc_chan);
    if (ret != ESP_OK) return ret;

    memset(&cali_config, 0, sizeof(cali_config));
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (ret != ESP_OK) return ret;

    mq135_init_gas();
    return ESP_OK;
}


/**
 * @brief Obtener resistencia rs
 * @return El valor de la resistencia rs
 */
static float mq135_get_resistance(void) {
    // Validar que los handles estén inicializados
    if (!adc1_handle || !cali_handle) {
        return -1.0f;
    }

    uint32_t adc_sum = 0;
    uint8_t valid_samples = 0;

    // Tomar múltiples muestras y promediarlas
    for (uint8_t i = 0; i < NUMBER_OF_SAMPLES; i++) {
        int raw_reading;
        esp_err_t ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw_reading);

        // Validar lectura exitosa y en rango
        if (ret == ESP_OK && raw_reading >= 0 && raw_reading <= 4095) {
            adc_sum += raw_reading;
            valid_samples++;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Verificar que tengamos suficientes muestras válidas
    if (valid_samples < (NUMBER_OF_SAMPLES / 2)) {
        return -1.0f; // Error: muy pocas muestras válidas
    }

    // Calcular promedio de las muestras válidas
    uint16_t adc_avg = adc_sum / valid_samples;

    // Aplicar filtro EMA una sola vez al promedio
    uint16_t adc_filtered = ema_filter_q15(adc_avg);

    // Convertir ADC a voltaje
    int voltage_mv = 0;
    esp_err_t ret = adc_cali_raw_to_voltage(cali_handle, adc_filtered, &voltage_mv);
    if (ret != ESP_OK) {
        return -1.0f;
    }

    // Convertir a voltios
    float vout = (float)voltage_mv / 1000.0f;

    // Validar que el voltaje esté en un rango razonable
    if (vout < 0.001f || vout > VCC) {
        return INFINITY;
    }

    // Calcular resistencia del sensor usando divisor de tensión
    // Rs = Rload * (Vcc - Vout) / Vout
    float rs = RLOAD * ((VCC - vout) / vout);

    // Validar que la resistencia calculada sea positiva y razonable
    if (rs < 0.0f || !isfinite(rs)) {
        return INFINITY;
    }

    return rs;
}


/**
 * @brief Calcula el factor de correccion.
 * @param temperature Valor de temperatura en determinado instante para mayor precision.
 * @param humidity Valor de humedad en determinado instante para mayor precision.
 * @return Retorna el factor de correccion.
 */
static float mq135_get_correction_factor(float temperature, float humidity) {
    float temp_squared = temperature * temperature;
    float humidity_diff = humidity - RELATIVE_HUMIDITY;
    float correction = CORA * temp_squared - CORB * temperature + CORC - humidity_diff * CORD;
    return correction;
}


/**
 * @brief Calcula la resistencia corregida
 * @param temperature Valor de temperatura en determinado instante para mayor precision.
 * @param humidity Valor de humedad en determinado instante para mayor precision.
 * @return Retorna la resistencia corregida.
 */
float mq135_get_corrected_resistance(float temperature, float humidity) {
    return mq135_get_resistance() / mq135_get_correction_factor(temperature, humidity);
}


/**
 * @brief Calcular la resistencia R0.
 * @param temperature Valor de temperatura en determinado instante para mayor precision.
 * @param humidity Valor de humedad en determinado instante para mayor precision.
 * @param params Estructura del gas particular que se quiere sensar.
 * @return Retorna la R0 corregida.
 */
float mq135_get_corrected_R0(float temperature, float humidity, const gas_t* params) {
    return mq135_get_R0(mq135_get_corrected_resistance(temperature, humidity), params);
}


/**
 * @brief Calcular ppm.
 * @param temperature Valor de temperatura en determinado instante para mayor precision.
 * @param humidity Valor de humedad en determinado instante para mayor precision.
 * @param params Estructura del gas particular que se quiere sensar.
 * @return Retorna el ppm del gas particular.
 */
float mq135_get_corrected_ppm(float temperature, float humidity, const gas_t* params) {
    float corrected_resistance = mq135_get_corrected_resistance(temperature, humidity);
    return mq135_get_ppm(corrected_resistance, params->R0, params);
}

