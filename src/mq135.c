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

#include <esp_random.h>
#include <esp_timer.h>

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
#include "mpack.h"
#include "Fsm/fsm.h"
#include "Message/message.h"


typedef struct {
    state_mq135_t state_mq135;
    uint32_t counter;
    float ema_ppm;
    float ema_error;
    mq135_data_t mq135;
    mq135_alert_t alert;
    mqtt_packet_t packet;
} data_t;


static const char *TAG = "MQ135";
static adc_oneshot_unit_handle_t s_adc_handle  = NULL;
static adc_cali_handle_t         s_cali_handle = NULL;
static bool                      s_cali_enable = false;
static mq135_config_t config = {0};



/* ─────────────────────────────────────────────
 * Utilidad: ordenamiento por inserción (in-place)
 * Usado únicamente sobre arreglos pequeños (≤9 elem)
 * ───────────────────────────────────────────── */
static void insertion_sort(int *arr, int n) {
    for (int i = 1; i < n; i++) {
        int key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}


/* ─────────────────────────────────────────────
 * Calibración ADC (Line Fitting o Curve Fitting)
 * ESP-IDF v5.x provee dos esquemas según el chip.
 * ───────────────────────────────────────────── */
static bool adc_calibration_init(adc_unit_t unit,
                                 adc_channel_t channel,
                                 adc_atten_t atten,
                                 adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret            = ESP_FAIL;
    bool calibrated          = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id  = unit,
            .chan     = channel,
            .atten    = atten,
            .bitwidth = MQ135_ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
            ESP_LOGI(TAG, "Calibración ADC: Curve Fitting activada");
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_cfg = {
            .unit_id  = unit,
            .atten    = atten,
            .bitwidth = MQ135_ADC_BITWIDTH,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
            ESP_LOGD(TAG, "Info: Calibración ADC Line Fitting activada");
        }
    }
#endif

    *out_handle = handle;

    if (!calibrated) {
        ESP_LOGW(TAG, "Warning: Calibración ADC no disponible – se usarán valores raw convertidos");
    }
    return calibrated;
}




esp_err_t mq135_init(void) {

    config.rzero_kohm      = settings_get_node_mq135_r0();
    config.rload_kohm      = MQ135_RLOAD_KOHM;
    config.ema_alpha       = settings_get_node_mq135_alpha_ema();
    config.ema_value       = 0.0f;
    config.ema_initialized = false;

    /* ── Configurar ADC oneshot ── */
    if (s_adc_handle == NULL) {
        adc_oneshot_unit_init_cfg_t adc_cfg = {
            .unit_id  = MQ135_ADC_UNIT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &s_adc_handle));
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = MQ135_ADC_ATTEN,
        .bitwidth = MQ135_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle,
                                               MQ135_ADC_CHANNEL,
                                               &chan_cfg));

    /* ── Calibración ── */
    s_cali_enable = adc_calibration_init(MQ135_ADC_UNIT,
                                         MQ135_ADC_CHANNEL,
                                         MQ135_ADC_ATTEN,
                                         &s_cali_handle);

    ESP_LOGD(TAG, "MQ135 inicializado – GPIO34 / ADC1_CH6");
    ESP_LOGD(TAG, "R0=%.2f kΩ  RL=%.2f kΩ  EMA α=%.2f",
             config.rzero_kohm, config.rload_kohm, config.ema_alpha);

    return ESP_OK;
}


static float mq135_raw_to_voltage(int raw) {
    if (s_cali_enable && s_cali_handle != NULL) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        return (float)mv / 1000.0f;
    }
    // Conversión lineal sin calibración (aproximada)
    return ((float)raw / 4095.0f) * 3.3f;
}


// Rs = RL * (Vcc - Vout) / Vout
static float mq135_voltage_to_rs(float voltage_v, float rload_kohm) {
    if (voltage_v <= 0.0f) {
        return -1.0f;
    }
    return rload_kohm * (MQ135_VCC - voltage_v) / voltage_v;
}


// PPM = PARA * (Rs/R0)^PARB
static float mq135_rs_to_ppm(float rs_kohm, float r0_kohm) {
    if (rs_kohm <= 0.0f || r0_kohm <= 0.0f) {
        return -1.0f;
    }
    float ratio = rs_kohm / r0_kohm;
    return MQ135_PARA * powf(ratio, MQ135_PARB);
}



// Pipeline: muestras raw → mediana → voltaje → Rs → ppm → EMA
static esp_err_t mq135_read_co2_ppm(float *ppm_out) {

    // Tomar MQ135_MEDIAN_WINDOW muestras raw
    int raw_buf[MQ135_MEDIAN_WINDOW];
    for (int i = 0; i < MQ135_MEDIAN_WINDOW; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle,
                                        MQ135_ADC_CHANNEL,
                                        &raw_buf[i]));
        /*
         * Pequeña espera entre muestras para que el ADC SAR
         * se estabilice entre conversiones consecutivas
         */
        esp_rom_delay_us(200);
    }

    // Filtro de mediana (elimina outliers / ruido impulsivo)
    int sorted[MQ135_MEDIAN_WINDOW];
    memcpy(sorted, raw_buf, sizeof(raw_buf));
    insertion_sort(sorted, MQ135_MEDIAN_WINDOW);
    int median_raw = sorted[MQ135_MEDIAN_WINDOW / 2];

    // Convertir a voltaje
    float voltage = mq135_raw_to_voltage(median_raw);

    if (voltage <= 0.01f || voltage >= MQ135_VCC) {
        ESP_LOGW(TAG, "Warning: voltaje fuera de rango: %.3f V – verifica el cableado", voltage);
        return ESP_ERR_INVALID_STATE;
    }

    // Calcular Rs
    float rs = mq135_voltage_to_rs(voltage, config.rload_kohm);
    if (rs < 0.0f) {
        ESP_LOGW(TAG, "Warning: Rs inválido (voltaje=%.3f V)", voltage);
        return ESP_ERR_INVALID_STATE;
    }

    // Convertir a PPM
    float ppm_raw = mq135_rs_to_ppm(rs, config.rzero_kohm);

    // Clamp al rango físico razonable
    if (ppm_raw < MQ135_PPM_MIN) ppm_raw = MQ135_PPM_MIN;
    if (ppm_raw > MQ135_PPM_MAX) ppm_raw = MQ135_PPM_MAX;

    // Filtro EMA
    if (!config.ema_initialized) {
        config.ema_value = ppm_raw;
        config.ema_initialized = true;
    } else {
        config.ema_value = config.ema_alpha * ppm_raw
                       + (1.0f - config.ema_alpha) * config.ema_value;
    }

    *ppm_out = config.ema_value;
    return ESP_OK;
}


static void alert_analysis(data_t *data, const bool get_data) {

    if (get_data) {
        float ppm;
        const esp_err_t err  = mq135_read_co2_ppm(&ppm);
        if (err != ESP_OK) return;
        if (ppm < 350.0f) return; // Evita envenenar el EMA en modo Low Consumption
        data->mq135.co2ppm = ppm;
    }
    const float ppm_actual = data->mq135.co2ppm;
    const float error_actual = ppm_actual - data->ema_ppm;
    const float error_abs = fabsf(error_actual);
    const float umbral_alerta_dinamico = (K_SENSIBILIDAD * data->ema_error) + UMBRAL_MINIMO_ABS;

    switch (data->state_mq135) {
        case INIT_MQ135:
            data->ema_ppm = data->mq135.co2ppm;
            data->ema_error = 0.0f;
            data->state_mq135 = NORMAL_MQ135;
            break;

        case NORMAL_MQ135:
            if (error_abs > umbral_alerta_dinamico) {
                data->state_mq135 = ALERT_MQ135;
                data->alert.co2ppm_i = data->ema_ppm;
                data->alert.co2ppm_a = ppm_actual;
                if (generate_message_alert_air(&data->packet, data->alert)) {
                    if (xQueueSend(queues.alert_air_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Info: cola llena, descartando paquete");
                        free(data->packet.payload);
                    }
                } else {
                    ESP_LOGE(TAG, "Error: fallo al generar paquete (RAM)");
                }
            } else {
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_ppm = (config.ema_alpha * ppm_actual) + ((1 - config.ema_alpha) * data->ema_ppm);
            break;

        case ALERT_MQ135:
            if (error_abs < (umbral_alerta_dinamico * HYSTERESIS)) {
                data->state_mq135 = NORMAL_MQ135;
                if (generate_message_alert_air(&data->packet, data->alert)) {
                    if (xQueueSend(queues.alert_air_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Info: cola llena, descartando paquete");
                        free(data->packet.payload);
                    }
                } else {
                    ESP_LOGE(TAG, "Error: fallo al generar paquete (RAM)");
                }
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_ppm = (config.ema_alpha * ppm_actual) + ((1 - config.ema_alpha) * data->ema_ppm);
            break;
    }
}


static void mq135_task_in_balanced_or_performance(uint32_t *counter, uint32_t slices, data_t *data) {
    float ppm;
    bool flag_error = false;
    const esp_err_t err = mq135_read_co2_ppm(&ppm);

    if (err != ESP_OK) flag_error = true;

    ESP_LOGD(TAG, "PPM: %f", data->mq135.co2ppm);

    if (ppm < 350.0f) {
        flag_error = true;
    } else {
        data->mq135.co2ppm = ppm;
    }

    if (*counter >= slices) {
        *counter = 0;
        if (flag_error) {
            // Mandamos un dato vacio para no bloquear el ALL_DATA_READY
            const mq135_data_t err_data = {0};
            xQueueSend(queues.mq135_buffer, &err_data, pdMS_TO_TICKS(100));
        } else {
            xQueueSend(queues.mq135_buffer, &data->mq135, pdMS_TO_TICKS(100));
        }
        // Bandera corregida:
        xEventGroupSetBits(event_group.collector_events, MQ135_DATA_READY);
    }

    if (flag_error) {
        return; // Evita envenenar el EMA con valores erroneos
    }
    alert_analysis(data, false);
}


static void init_data(data_t *data) {
    data->state_mq135 = INIT_MQ135;
    data->counter = 0;
    data->ema_ppm = 440.0f;
    data->ema_error = 0.0f;
}


void mq135_task(void *pvParameters) {
    static data_t data;
    init_data(&data);

    uint32_t counter = 0;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;
            counter = 0;

            while (running) {
                const uint64_t start_time = esp_timer_get_time();

                uint32_t sample_rate_min = settings_get_node_sample_rate();
                if (sample_rate_min == 0) sample_rate_min = 1;

                const energy_mode_t mode = settings_get_node_energy_mode();
                counter++;

                uint32_t target_delay_ms = 0;

                switch (mode) {
                    case LOW_CONSUMPTION: {
                        target_delay_ms = MQ135_LOW_DELAY;
                        counter = 0;
                        alert_analysis(&data, true);
                        break;
                    }
                    case BALANCED: {
                        target_delay_ms = MQ135_BALANCED_DELAY; // <-- O DHT11_BALANCED_DELAY
                        const uint32_t slices = (sample_rate_min * 60) / (target_delay_ms / 1000);
                        mq135_task_in_balanced_or_performance(&counter, slices, &data); // o dht11_...
                        break;
                    }
                    case PERFORMANCE: {
                        target_delay_ms = MQ135_PERFORMANCE_DELAY; // <-- O DHT11_PERFORMANCE_DELAY
                        const uint32_t slices = (sample_rate_min * 60) / (target_delay_ms / 1000);
                        mq135_task_in_balanced_or_performance(&counter, slices, &data); // o dht11_...
                        break;
                    }
                }

                const uint64_t end_time = esp_timer_get_time();
                const uint32_t elapsed_ms = (uint32_t)((end_time - start_time) / 1000);

                TickType_t dynamic_delay = 0;
                if (target_delay_ms > elapsed_ms) {
                    dynamic_delay = pdMS_TO_TICKS(target_delay_ms - elapsed_ms);
                } else {
                    // Si el sensor fue tan lento que se pasó del tiempo, no dormimos nada
                    dynamic_delay = 0;
                }

                uint32_t stop_signal = 0;
                const BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &stop_signal, dynamic_delay);

                if (result == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        running = false;
                    }
                }
            }
        }
    }
}