/**
 * @file mq135.c
 * @brief Driver para sensor de calidad de aire MQ135 (CO2)
 * @author Franco Sarubbi
 * @date 2025
 *
 * Este driver implementa la lectura y calibracion del sensor MQ135 usando
 * el ADC del ESP32 con corrección de temperatura y humedad.
 */

#include "Data/data.h"
#include "MQ135/mq135.h"
#include "MQTT/mqtt.h"
#include "DHT11/dht11.h"
#include "Setting/settings.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#include "System/system.h"



static const char *TAG = "MQ135_CO2";


/* ===== Variables Privadas ===== */
static adc_oneshot_unit_handle_t adc1_handle = NULL;      /**< Handle del ADC Unit 1 */
static adc_oneshot_unit_init_cfg_t init_config1;          /**< Configuración de inicializacion ADC */
static adc_oneshot_chan_cfg_t adc_chan;                   /**< Configuracion del canal ADC */
static adc_cali_handle_t cali_handle = NULL;              /**< Handle de calibracion ADC */
static adc_cali_line_fitting_config_t cali_config;        /**< Configuracion de calibracion lineal */
static int64_t ema_value_q15 = 0;                         /**< Valor actual del filtro EMA en formato Q15 */
static bool ema_initialized_q15 = false;                  /**< Flag de inicializacion del filtro EMA */
static float runtime_R0 = CO2_R0_DEFAULT;                 /**< Resistencia R0 en runtime (puede ser calibrada) */


/* ===== Funciones Privadas ===== */

/**
 * @brief Reinicia el filtro EMA a su estado inicial
 *
 * Esta función limpia el estado interno del filtro de media móvil exponencial,
 * forzando una reinicialización en la próxima muestra.
 */
static inline void reset_ema(void) {
    ema_value_q15 = 0;
    ema_initialized_q15 = false;
}


/**
 * @brief Aplica un filtro de media movil exponencial (EMA) a las muestras del ADC.
 *
 * Implementa un filtro EMA en aritmética de punto fijo Q15 para suavizar
 * las lecturas del ADC y reducir ruido. La primera muestra inicializa el filtro.
 *
 * @param new_sample Nueva muestra del ADC (valor raw de 0-4095).
 * @return Valor filtrado (0-4095).
 *
 * @note Formula: EMA = α × muestra_nueva + (1-α) × EMA_anterior
 * @note α está definido por EMA_ALPHA_Q15 en formato Q15.
 */
static uint16_t ema_filter(const uint16_t new_sample) {
    if (!ema_initialized_q15) {
        ema_value_q15 = ((int64_t)new_sample) << 15;
        ema_initialized_q15 = true;
        return new_sample;
    }
    int64_t sample_q15 = ((int64_t)new_sample) << 15;
    ema_value_q15 = ((EMA_ALPHA_Q15 * sample_q15) +
                     ((EMA_2_15 - EMA_ALPHA_Q15) * ema_value_q15)) >> 15;
    return (uint16_t)(ema_value_q15 >> 15);
}


/**
 * @brief Obtiene la resistencia del sensor MQ135 (Rs) sin corregir.
 *
 * Lee multiples muestras del ADC, las filtra con EMA, calcula el promedio,
 * convierte a voltaje y finalmente calcula la resistencia del sensor usando
 * la ecuacion del divisor de tension.
 *
 * @return Resistencia del sensor en Ohmios (Ω).
 * @retval -1.0f Si hay error de lectura o handles no inicializados.
 * @retval INFINITY Si el voltaje esta fuera de rango o la resistencia es invalida.
 */
static float mq135_get_resistance(void) {
    if (!adc1_handle || !cali_handle) return -1.0f;

    int adc_sum = 0;
    uint16_t valid_samples = 0;

    for (uint16_t i = 0; i < MQ135_NUMBER_OF_SAMPLES; i++) {
        int raw;
        esp_err_t r = adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw);
        if (r == ESP_OK && raw >= 0 && raw <= 4095) {
            uint16_t filtered = ema_filter((uint16_t)raw);
            adc_sum += filtered;
            valid_samples++;
        }
        vTaskDelay(pdMS_TO_TICKS(MQ135_SAMPLE_DELAY_MS));
    }

    if (valid_samples == 0) return -1.0f;

    int adc_avg = adc_sum / valid_samples;
    int voltage_mv = 0;
    if (adc_cali_raw_to_voltage(cali_handle, adc_avg, &voltage_mv) != ESP_OK) {
        return -1.0f;
    }

    float vout = (float)voltage_mv / 1000.0f;
    if (vout < 0.001f || vout >= MQ135_VCC) return INFINITY;

    float rs = MQ135_RLOAD * (MQ135_VCC - vout) / vout;
    if (!isfinite(rs) || rs <= 0.0f) return INFINITY;
    return rs;
}


/**
 * @brief Calcula el factor de correccion por temperatura y humedad.
 *
 * Los sensores MQ135 son sensibles a las condiciones ambientales. Esta funcion
 * calcula un factor de corrección basado en la ecuación empirica del datasheet.
 *
 * @param temperature_c Temperatura ambiente en grados Celsius.
 * @param humidity_percent Humedad relativa en porcentaje (0-100).
 * @return Factor de correccion (tipicamente 0.8 - 1.2).
 * @retval 1.0f Si el factor calculado es inválido (protección contra división por cero).
 */
static float mq135_get_correction_factor(float temperature_c, float humidity_percent) {
    float rh = humidity_percent;
    float corr = CORA * rh * rh - CORB * rh + CORC - CORD * (temperature_c - 20.0f);
    if (!isfinite(corr) || corr <= 0.0f) return 1.0f;
    return corr;
}


/**
 * @brief Obtiene la resistencia del sensor corregida por temperatura y humedad.
 *
 * Lee la resistencia raw del sensor y la corrige usando el factor de corrección
 * ambiental para obtener una lectura mas precisa.
 *
 * @param temperature_c Temperatura ambiente actual en °C
 * @param humidity_percent Humedad relativa actual en %
 * @return Resistencia corregida en Ohmios (Ω)
 * @retval -1.0f Si hay error de lectura o valor invalido.
 *
 * @note Fórmula: Rs_corregida = Rs_raw / factor_correccion.
 */
static float mq135_get_corrected_resistance(float temperature_c, float humidity_percent) {
    float rs = mq135_get_resistance();
    if (rs <= 0 || !isfinite(rs)) return -1.0f;
    float corr = mq135_get_correction_factor(temperature_c, humidity_percent);
    return rs / corr;
}


/**
 * @brief Calcula la concentración de CO2 en PPM.
 *
 * Convierte la resistencia del sensor a concentracion de CO2 usando
 * la ecuacion logaritmica del datasheet del MQ135.
 *
 * @param resistance Resistencia del sensor (Rs) en Ohmios.
 * @param R0 Resistencia de calibracion en aire limpio en Ohmios.
 * @return Concentracion de CO2 en partes por millon (ppm).
 * @retval -1.0f Si los parametros son invalidos.
 *
 * @note Constantes A y B definidas en el header segun curva del datasheet.
 */
static float mq135_calculate_ppm(float resistance, float R0) {
    if (resistance <= 0.0f || R0 <= 0.0f) return -1.0f;
    float ratio = resistance / R0;
    float ppm = CO2_A * powf(ratio, -CO2_B);
    return ppm;
}


/* ===== Funciones Publicas ===== */

/**
 * @brief Inicializa el sensor MQ135 y el ADC del ESP32.
 *
 * Configura el ADC Unit 1 en canal 6 (GPIO34) con atenuacion de 12dB
 * para rango completo 0-3.3V, resolucion de 12 bits, y calibracion lineal.
 * Tambien reinicia el filtro EMA y carga el valor R0 por defecto.
 *
 * @return Estado de la inicializacion.
 * @retval ESP_OK Si la inicializacion fue exitosa.
 * @retval ESP_ERR_* Codigo de error especifico si falla alguna etapa.
 *
 * @warning Esta funcion debe llamarse antes de cualquier lectura del sensor.
 */
esp_err_t mq135_init(void) {
    memset(&init_config1, 0, sizeof(init_config1));
    init_config1.unit_id = ADC_UNIT_1;
    init_config1.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: adc_oneshot_new_unit failed: %d -", ret);
        return ret;
    }

    memset(&adc_chan, 0, sizeof(adc_chan));
    adc_chan.atten = ADC_ATTEN_DB_12;
    adc_chan.bitwidth = ADC_BITWIDTH_12;
    ret = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_6, &adc_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: adc_oneshot_config_channel failed: %d -", ret);
        return ret;
    }

    memset(&cali_config, 0, sizeof(cali_config));
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: adc_cali_create_scheme_line_fitting failed: %d -", ret);
        return ret;
    }

    reset_ema();
    runtime_R0 = CO2_R0_DEFAULT;
    return ESP_OK;
}


/**
 * @brief Lee la concentracion de CO2 en PPM con correccion ambiental.
 *
 * Esta es la funcion principal de lectura del sensor. Obtiene la resistencia
 * corregida del sensor y la convierte a concentracion de CO2 usando el
 * valor R0 actual (calibrado o por defecto).
 *
 * @param temperature_c Temperatura ambiente actual en grados Celsius.
 * @param humidity_percent Humedad relativa actual en porcentaje (0-100).
 * @return Concentracion de CO2 en partes por millon (ppm).
 * @retval -1.0f Si hay error de lectura o valor invalido.
 */
float mq135_read_ppm(float temperature_c, float humidity_percent) {
    float rs_corr = mq135_get_corrected_resistance(temperature_c, humidity_percent);
    if (rs_corr <= 0 || !isfinite(rs_corr)) return -1.0f;
    return mq135_calculate_ppm(rs_corr, runtime_R0);
}


/**
 * @brief Imprime informacion de diagnostico del sensor MQ135.
 *
 * Funcion util para debugging y verificacion del hardware. Muestra lecturas
 * raw del ADC, voltajes, estimaciones de Rs con diferentes valores de RLOAD,
 * y el estado actual de calibracion.
 *
 * @param temperature_c Temperatura ambiente para calculos de correccion (°C)
 * @param humidity_percent Humedad relativa para calculos de correccion (%)
 */
void mq135_print_diagnostics(float temperature_c, float humidity_percent) {
    if (!adc1_handle || !cali_handle) {
        ESP_LOGW(TAG, "- ERROR: ADC no inicializado -");
        return;
    }

    uint32_t adc_sum = 0;
    int samples = 10;
    for (int i = 0; i < samples; i++) {
        int raw;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_6, &raw);
        adc_sum += raw;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    int adc_avg = adc_sum / samples;
    int voltage_mv = 0;
    if (adc_cali_raw_to_voltage(cali_handle, adc_avg, &voltage_mv) != ESP_OK) {
        ESP_LOGW(TAG, "- WARNING: No se pudo convertir raw->mV -");
        return;
    }

    ESP_LOGI(TAG, "- INFO: ADC raw avg: %d -", adc_avg);
    ESP_LOGI(TAG, "- INFO: Voltage: %d mV (%.3f V) -", voltage_mv, voltage_mv / 1000.0f);

    float vout = (float)voltage_mv / 1000.0f;
    float rs10 = 10000.0f * (MQ135_VCC - vout) / vout;
    float rs15 = 15000.0f * (MQ135_VCC - vout) / vout;
    float rs20 = 20000.0f * (MQ135_VCC - vout) / vout;

    ESP_LOGI(TAG, "- INFO: Rs estimadas con distintos RLOAD: 10k=%.1fΩ, 15k=%.1fΩ, 20k=%.1fΩ -", rs10, rs15, rs20);

    float rs_raw = mq135_get_resistance();
    float rs_corr = mq135_get_corrected_resistance(temperature_c, humidity_percent);
    ESP_LOGI(TAG, "- INFO: Rs raw: %.3f Ω, Rs corregida: %.3f Ω -", rs_raw, rs_corr);
    ESP_LOGI(TAG, "- INFO: R0 runtime: %.3f Ω (macro por defecto: %.3f Ω) -", runtime_R0, (double)CO2_R0_DEFAULT);
}


/**
 * @brief Funcion que genera un JSON con la temperatura inicial y la actual.
 *
 * Genera el JSON que sera enviado en caso de una alerta por aumento brusco de ppm.
 *
 * @param output_buffer String formateado en JSON.
 * @param buffer_size Tamaño del string.
 * @param alert Estructura con los valores iniciales y actuales.
 */
static void generate_json(char *output_buffer, size_t buffer_size, mq135_alert_t alert) {

    snprintf(output_buffer, buffer_size,
        "{\n"
        "  \"co2 ppm inicial\": %f,\n"
        "  \"co2 ppm actual\": %f,\n"
        "}",
        alert.co2ppm_i,
        alert.co2ppm_a
    );
}


void mq135_task(void *pvParameters) {
    static state_mq135_t state_mq135 = INIT_MQ135;
    static uint32_t counter = 0;
    static float ema_ppm = 440.0f;            // Media movil de la temperatura
    static float ema_error = 0.0f;            // Media movil del error (ruido normal)
    static mq135_data_t mq135;
    static mq135_alert_t alert;
    static dht11_data_t dht11;
    char json[MQ135_JSON_ALERT];

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        uint32_t slices = (settings.sample_rate * 60)/(DHT11_DELAY/1000);
        counter++;

        if (xQueueReceive(queues.dht11_to_mq135, &dht11, portMAX_DELAY)) {
            mq135.co2ppm = mq135_read_ppm((float)dht11.temperature, (float)dht11.humidity);
        }

        if (counter >= slices) {
            counter = 0;
            xQueueSend(queues.mq135_buffer, &mq135, portMAX_DELAY);
            xEventGroupSetBits(event_group.collector_events, MQ135_DATA_READY);
        }
        else {
            float ppm_actual = mq135.co2ppm = mq135_read_ppm((float)dht11.temperature, (float)dht11.humidity);
            float error_actual = ppm_actual - ema_ppm;
            float error_abs = fabsf(error_actual);
            float umbral_alerta_dinamico = (K_SENSIBILIDAD * ema_error) + UMBRAL_MINIMO_ABS;

            switch (state_mq135) {
            case INIT_MQ135:
                    ema_ppm = mq135.co2ppm;
                    ema_error = 0.0f;
                    state_mq135 = NORMAL_MQ135;
                    break;

            case NORMAL_MQ135:
                    if (error_abs > umbral_alerta_dinamico) {
                        state_mq135 = ALERT_MQ135;
                        alert.co2ppm_i = ema_ppm;
                        alert.co2ppm_a = ppm_actual;
                        generate_json(json, DHT11_JSON_ALERT, alert);
                        mqtt_publish("/mq135/alert/on_alert", json, (int)strlen(json), 2, 0);
                    } else {
                        ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * ema_error);
                    }
                    ema_ppm = (ALFA_TEMP * ppm_actual) + ((1 - ALFA_TEMP) * ema_ppm);
                    break;

            case ALERT_MQ135:
                    if (error_abs < (umbral_alerta_dinamico * HYSTERESIS)) {
                        state_mq135 = NORMAL_MQ135;

                        generate_json(json, DHT11_JSON_ALERT, alert);
                        mqtt_publish("/mq135/alert/off_alert", json, (int)strlen(json), 2, 0);

                        ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * ema_error);
                    }
                    ema_ppm = (ALFA_TEMP * ppm_actual) + ((1 - ALFA_TEMP) * ema_ppm);
                    break;
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(MQ135_DELAY));
    }
}