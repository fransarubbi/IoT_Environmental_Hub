/**
 * @file mq135.c
 * @brief Driver para sensor de calidad de aire MQ135 (Índice VOC / Calidad 0-100%)
 * @author Franco Sarubbi
 * @date 2025
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
    float ema_aqi;      // Promedio móvil de calidad del aire
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
 * Utilidad: ordenamiento por inserción
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
 * Calibración ADC
 * ───────────────────────────────────────────── */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle) {
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
        ESP_LOGW(TAG, "Warning: Calibración ADC no disponible");
    }
    return calibrated;
}

esp_err_t mq135_init(void) {
    config.rzero_kohm      = settings_get_node_mq135_r0(); // Asegúrate de que retorne kOhms (ej. 12.5)
    config.rload_kohm      = MQ135_RLOAD_KOHM;
    config.ema_alpha       = settings_get_node_mq135_alpha_ema();
    config.ema_value       = 0.0f;
    config.ema_initialized = false;

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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, MQ135_ADC_CHANNEL, &chan_cfg));

    s_cali_enable = adc_calibration_init(MQ135_ADC_UNIT, MQ135_ADC_CHANNEL, MQ135_ADC_ATTEN, &s_cali_handle);

    ESP_LOGD(TAG, "MQ135 inicializado - R0=%.2f kΩ", config.rzero_kohm);
    return ESP_OK;
}

static float mq135_raw_to_voltage(int raw) {
    if (s_cali_enable && s_cali_handle != NULL) {
        int mv = 0;
        adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        return (float)mv / 1000.0f;
    }
    return ((float)raw / 4095.0f) * 3.3f;
}

static float mq135_voltage_to_rs(float voltage_v, float rload_kohm) {
    if (voltage_v <= 0.0f) {
        return -1.0f;
    }
    return rload_kohm * (MQ135_VCC - voltage_v) / voltage_v;
}

// Convertir Ratio Rs/R0 a porcentaje de Calidad de Aire (0% a 100%)
static float mq135_rs_to_aqi_percent(float rs_kohm, float r0_kohm) {
    if (rs_kohm <= 0.0f || r0_kohm <= 0.0f) {
        return -1.0f;
    }
    float ratio = rs_kohm / r0_kohm;
    
    // Ecuación lineal: 3.6 = 100% (Limpio) | 1.0 = 0% (Viciado)
    float aqi = ((ratio - MQ135_RATIO_DIRTY) / (MQ135_RATIO_CLEAN - MQ135_RATIO_DIRTY)) * 100.0f;
    ESP_LOGI(TAG, "AIRE: %.2f", aqi);

    // Clamp para mantenerlo en un porcentaje lógico
    if (aqi > 100.0f) aqi = 100.0f;
    if (aqi < 0.0f) aqi = 0.0f;

    return aqi;
}

static esp_err_t mq135_read_air_quality(float *aqi_out) {
    int raw_buf[MQ135_MEDIAN_WINDOW];
    for (int i = 0; i < MQ135_MEDIAN_WINDOW; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, MQ135_ADC_CHANNEL, &raw_buf[i]));
        esp_rom_delay_us(200);
    }

    int sorted[MQ135_MEDIAN_WINDOW];
    memcpy(sorted, raw_buf, sizeof(raw_buf));
    insertion_sort(sorted, MQ135_MEDIAN_WINDOW);
    int median_raw = sorted[MQ135_MEDIAN_WINDOW / 2];

    float voltage = mq135_raw_to_voltage(median_raw);

    if (voltage <= 0.01f || voltage >= MQ135_VCC) {
        ESP_LOGW(TAG, "Warning: voltaje fuera de rango: %.3f V", voltage);
        return ESP_ERR_INVALID_STATE;
    }

    float rs = mq135_voltage_to_rs(voltage, config.rload_kohm);
    if (rs < 0.0f) {
        return ESP_ERR_INVALID_STATE;
    }

    float aqi_raw = mq135_rs_to_aqi_percent(rs, config.rzero_kohm);

    if (!config.ema_initialized) {
        config.ema_value = aqi_raw;
        config.ema_initialized = true;
    } else {
        config.ema_value = config.ema_alpha * aqi_raw + (1.0f - config.ema_alpha) * config.ema_value;
    }

    *aqi_out = config.ema_value;
    return ESP_OK;
}

static void alert_analysis(data_t *data, const bool get_data) {
    if (get_data) {
        float aqi;
        const esp_err_t err  = mq135_read_air_quality(&aqi);
        if (err != ESP_OK) return;
        data->mq135.air_quality = aqi;
    }
    
    const float aqi_actual = data->mq135.air_quality;
    const float error_actual = aqi_actual - data->ema_aqi;
    const float error_abs = fabsf(error_actual);
    
    const float umbral_alerta_dinamico = (K_SENSIBILIDAD * data->ema_error) + UMBRAL_MINIMO_ABS;

    switch (data->state_mq135) {
        case INIT_MQ135:
            data->ema_aqi = data->mq135.air_quality;
            data->ema_error = 0.0f;
            data->state_mq135 = NORMAL_MQ135;
            break;

        case NORMAL_MQ135:
            if (error_abs > umbral_alerta_dinamico) {
                data->state_mq135 = ALERT_MQ135;
                data->alert.quality_i = data->ema_aqi;
                data->alert.quality_a = aqi_actual;
                const State state = atomic_load(&shared_state);
                if (state == STORE) {
                    Event event = eFromStoreToBypass;
                    xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                } else if (state == UPDATE_SCORE) {
                    Event event = eToBypass;
                    xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                }
                if (generate_message_alert_air(&data->packet, data->alert)) {
                    if (xQueueSend(queues.alert_air_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Info: cola llena, descartando paquete");
                        free(data->packet.payload);
                    }
                }
            } else {
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_aqi = (config.ema_alpha * aqi_actual) + ((1 - config.ema_alpha) * data->ema_aqi);
            break;

        case ALERT_MQ135:
            if (error_abs < (umbral_alerta_dinamico * HYSTERESIS)) {
                data->state_mq135 = NORMAL_MQ135;
                const State state = atomic_load(&shared_state);
                if (state == STORE) {
                    Event event = eFromStoreToBypass;
                    xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                } else if (state == UPDATE_SCORE) {
                    Event event = eToBypass;
                    xQueueSend(queues.event, &event, pdMS_TO_TICKS(100));
                }
                if (generate_message_alert_air(&data->packet, data->alert)) {
                    if (xQueueSend(queues.alert_air_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Info: cola llena, descartando paquete");
                        free(data->packet.payload);
                    }
                }
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_aqi = (config.ema_alpha * aqi_actual) + ((1 - config.ema_alpha) * data->ema_aqi);
            break;
    }
}

static void mq135_task_in_balanced_or_performance(uint32_t *counter, uint32_t slices, data_t *data) {
    float aqi;
    bool flag_error = false;
    const esp_err_t err = mq135_read_air_quality(&aqi);

    if (err != ESP_OK) {
        flag_error = true;
    } else {
        data->mq135.air_quality = aqi;
    }

    ESP_LOGD(TAG, "Calidad del Aire: %.1f %%", data->mq135.air_quality);

    if (*counter >= slices) {
        *counter = 0;
        if (flag_error) {
            const mq135_data_t err_data = {0};
            xQueueSend(queues.mq135_buffer, &err_data, pdMS_TO_TICKS(100));
        } else {
            xQueueSend(queues.mq135_buffer, &data->mq135, pdMS_TO_TICKS(100));
        }
        xEventGroupSetBits(event_group.collector_events, MQ135_DATA_READY);
    }

    if (flag_error) {
        return; 
    }
    alert_analysis(data, false);
}

static void init_data(data_t *data) {
    data->state_mq135 = INIT_MQ135;
    data->counter = 0;
    data->ema_aqi = 100.0f; // Asumimos que inicia en aire limpio
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
                        target_delay_ms = MQ135_BALANCED_DELAY; 
                        const uint32_t slices = (sample_rate_min * 60) / (target_delay_ms / 1000);
                        mq135_task_in_balanced_or_performance(&counter, slices, &data); 
                        break;
                    }
                    case PERFORMANCE: {
                        target_delay_ms = MQ135_PERFORMANCE_DELAY; 
                        const uint32_t slices = (sample_rate_min * 60) / (target_delay_ms / 1000);
                        mq135_task_in_balanced_or_performance(&counter, slices, &data); 
                        break;
                    }
                }

                const uint64_t end_time = esp_timer_get_time();
                const uint32_t elapsed_ms = (uint32_t)((end_time - start_time) / 1000);

                TickType_t dynamic_delay = 0;
                if (target_delay_ms > elapsed_ms) {
                    dynamic_delay = pdMS_TO_TICKS(target_delay_ms - elapsed_ms);
                } else {
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