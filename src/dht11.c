/**
* @file dht11.c
 * @brief Driver y gestión de tareas para el sensor DHT11.
 *
 * Este archivo implementa la lectura del sensor DHT11 utilizando el periférico RMT
 * del ESP32 para una decodificación precisa de los pulsos. Además, gestiona la
 * lógica de negocio asociada: filtrado de datos (EMA), detección de anomalías
 * y gestión de modos de energía (Low, Balanced, Performance).
 */


#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"
#include "System/system.h"
#include <esp_timer.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp32/rom/ets_sys.h"
#include <string.h>
#include <driver/rmt_rx.h>
#include "mpack.h"
#include "Fsm/fsm.h"
#include "Message/message.h"


typedef enum {
    INIT_DHT11,
    NORMAL_DHT11,
    ALERT_DHT11
} state_dht11_t;


typedef struct {
    state_dht11_t state_dht11;
    uint32_t counter;
    float ema_temp;
    float ema_error;
    float temp_before_alert;
    dht11_data_t dht11;
    mqtt_packet_t packet;
} data_t;


static const char *TAG = "DHT11";
static QueueHandle_t g_receive_queue = NULL;
static rmt_channel_handle_t g_rx_channel = NULL;
static rmt_symbol_word_t g_rx_buffer[120];
static uint8_t num_symbols;



uint8_t dht11_get_temperature(const dht11_data_t *dhtt) {
    return dhtt->temperature;
}


uint8_t dht11_get_humidity(const dht11_data_t *dhtt) {
    return dhtt->humidity;
}


size_t dht11_struct_get_size(void) {
    return sizeof(dht11_data_t);
}



/**
 * @brief  Callback de interrupción RMT para la recepción de datos.
 *
 * Esta función es llamada automáticamente por el driver RMT una vez que
 * un paquete de datos ha sido recibido completamente. Su propósito es
 * pasar los datos recibidos a una cola de FreeRTOS para que sean procesados
 * por una tarea de mayor prioridad.
 *
 * @param  channel  El handle del canal RMT que activó el callback.
 * @param  edata    Puntero a la estructura de datos que contiene
 * la información del evento de recepción, incluyendo los símbolos RMT.
 * @param  user_ctx El contexto de usuario pasado durante el registro del callback.
 * En este caso, se usa para pasar el handle de la cola de FreeRTOS.
 *
 * @return Retorna 'true' si una tarea de mayor prioridad debe ser
 * despertada para procesar los datos de la cola, de lo contrario, 'false'.
 */
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel,
                                           const rmt_rx_done_event_data_t *edata,
                                           void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}


/**
 * @brief Configura GPIO con pull-up para DHT11.
 *
 * Configura el pin en modo Open-Drain con resistencia de Pull-Up interna
 * para garantizar la correcta comunicación one-wire.
 *
 * @return esp_err_t Devuelve ESP_OK si fue exitosa la configuración, sino ESP_FAIL.
 */
static esp_err_t dht11_gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT11_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,  // Open-drain bidireccional
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Pull-up habilitado
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    gpio_set_level(DHT11_PIN, 1); // Asegurar estado alto inicial
    vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo para estabilizar el sensor
    return ESP_OK;
}


/**
 * @brief Configura RMT para recepción (RX).
 *
 * Inicializa el canal RMT, configura la resolución del reloj y asigna
 * los buffers de memoria necesarios para capturar los pulsos del sensor.
 *
 * @return esp_err_t Devuelve ESP_OK si la configuración fue exitosa, sino ESP_FAIL.
 */
static esp_err_t dht11_rmt_rx_config(void) {
    rmt_rx_channel_config_t rx_chan_cfg;
    rmt_rx_event_callbacks_t cbs = {0};

    // Crear cola para que el callback ponga los eventos
    g_receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (g_receive_queue == NULL) {
        ESP_LOGE(TAG, "- ERROR: Error creando cola de recepcion -");
        return ESP_FAIL;
    }

    // Configurar canal RX
    rx_chan_cfg.gpio_num = DHT11_PIN;
    rx_chan_cfg.clk_src = RMT_CLK_SRC_REF_TICK;
    rx_chan_cfg.resolution_hz = RMT_CLK_RES_HZ;
    rx_chan_cfg.mem_block_symbols = RMT_BUFFER_SIZE;
    rx_chan_cfg.intr_priority = 3;
    rx_chan_cfg.flags.allow_pd = 0;
    rx_chan_cfg.flags.with_dma = false;
    rx_chan_cfg.flags.invert_in = false;
    rx_chan_cfg.flags.io_loop_back = false;

    esp_err_t ret = rmt_new_rx_channel(&rx_chan_cfg, &g_rx_channel);  // Crea el canal RMT
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error creando canal RMT RX: %s -", esp_err_to_name(ret));
        goto cleanup;
    }

    // Registrar callback
    cbs.on_recv_done = rmt_rx_done_callback;
    ret = rmt_rx_register_event_callbacks(g_rx_channel, &cbs, g_receive_queue);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error registrando callbacks: %s -", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "- INFO: Canal RMT RX configurado correctamente -");
    return ESP_OK;

    cleanup:
        if (g_rx_channel) {
            rmt_del_channel(g_rx_channel);
        }
    if (g_receive_queue) {
        vQueueDelete(g_receive_queue);
        g_receive_queue = NULL;
    }
    return ESP_FAIL;
}


/**
 * @brief Envía señal de inicio al DHT11 y luego recibe los datos a través del RMT.
 *
 * Se realiza el handshake según el protocolo que implementa el DHT11. Para poder
 * sincronizar la transmisión, se realiza un pequeño polling del pin para poder recibir
 * los datos correctamente antes de habilitar el RMT.
 *
 * @return esp_err_t  Devuelve ESP_OK si la señal de inicio fue enviada correctamente
 * y se recibieron los datos. Si algo falla (timeout o error RMT), se retorna el código de error.
 */
static esp_err_t dht11_start_and_receive(void) {

    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 900000,
        .flags.en_partial_rx = false
    };
    esp_err_t ret = ESP_OK;
    uint32_t timeout_us = 0; // Para los timeouts de espera

    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0);
    ets_delay_us(DHT11_START_SIGNAL_LOW);  // 20ms LOW
    gpio_set_level(DHT11_PIN, 1);
    ets_delay_us(DHT11_START_SIGNAL_HIGH); // 40us HIGH
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);   // liberar la linea

    // Esperar a que el sensor ponga la línea en BAJO (inicio del handshake)
    while (gpio_get_level(DHT11_PIN) == 1) {
        ets_delay_us(1); // Espera 1µs
        if (++timeout_us > 100) { // El sensor debe responder en ~50µs
            ESP_LOGE(TAG, "- ERROR: Timeout esperando respuesta (LOW) del sensor -");
            return ESP_ERR_TIMEOUT;
        }
    }

    // Esperar a que el sensor ponga la línea en ALTO (pulso "idle" del handshake)
    timeout_us = 0;
    while (gpio_get_level(DHT11_PIN) == 0) {
        ets_delay_us(1); // Espera 1µs
        if (++timeout_us > 120) { // El pulso es de 80µs nominales
            ESP_LOGE(TAG, "- ERROR: Timeout esperando pulso idle (HIGH) del sensor -");
            return ESP_ERR_TIMEOUT;
        }
    }

    ret = rmt_enable(g_rx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error activando canal RMT: %s -", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_receive(g_rx_channel, g_rx_buffer, sizeof(g_rx_buffer), &rx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "- ERROR: Error iniciando recepcion: %s -", esp_err_to_name(ret));
        rmt_disable(g_rx_channel);
        return ret;
    }

    rmt_rx_done_event_data_t event_data;
    if (xQueueReceive(g_receive_queue, &event_data, pdMS_TO_TICKS(RMT_TIMEOUT)) != pdTRUE) {
        ESP_LOGE(TAG, "- ERROR: Timeout esperando datos -");
        rmt_disable(g_rx_channel);
        return ESP_ERR_TIMEOUT;
    }

    rmt_disable(g_rx_channel);

    num_symbols = event_data.num_symbols;
    return ESP_OK;
}


/**
 * @brief Decodifica los datos de los símbolos RMT del sensor DHT11.
 *
 * Esta función procesa los símbolos RMT recibidos y los convierte en los
 * 5 bytes de datos del sensor interpretando la duración de los pulsos altos.
 *
 * @param dht11_data Puntero donde se guardarán los datos decodificados.
 * @return esp_err_t  Devuelve ESP_OK si la decodificación es exitosa y el checksum es válido.
 * Devuelve ESP_FAIL en caso de fallo.
 */
static esp_err_t dht11_decode_data(dht11_data_t *dht11_data) {
    int8_t bit = 7;
    uint8_t buffer = 0;
    uint8_t byte = 0;

    for (uint8_t i = 0; i < 40; i++) {
        if (g_rx_buffer[i].duration0 > DHT11_DURATION0_MIN && g_rx_buffer[i].duration0 < DHT11_DURATION0_MAX) {
            if (g_rx_buffer[i].level0 == 0 && g_rx_buffer[i].level1 == 1) {
                if (g_rx_buffer[i].duration1 > DHT11_DURATION1_MIN) {
                    if (g_rx_buffer[i].duration1 > DHT11_DURATION1_BIT1) {
                        buffer = buffer | (0x01 << bit);
                    }
                    bit -= 1;
                    if (bit == -1) {
                        bit = 7;
                        switch (byte) {
                            case 0: dht11_data->humidity = buffer; break;
                            case 1: dht11_data->hum_decimal = buffer; break;
                            case 2: dht11_data->temperature = buffer; break;
                            case 3: dht11_data->temp_decimal = buffer; break;
                            case 4: dht11_data->checksum = buffer; break;
                            default: break;
                        }
                        buffer = 0;
                        byte += 1;
                    }
                }
            }
        }
    }

    // Verificar checksum
    uint8_t checksum = dht11_data->humidity + dht11_data->hum_decimal + dht11_data->temperature + dht11_data->temp_decimal;
    if (checksum != dht11_data->checksum) {
        ESP_LOGE(TAG, "- ERROR: Checksum invalido: calculado %d, recibido %d -", checksum, dht11_data->checksum);
        return ESP_FAIL;
    }
    return ESP_OK;
}


/**
 * @brief Función de lectura de datos completa.
 *
 * Orquesta el proceso de lectura: llama al handshake/recepción, luego decodifica
 * la información y limpia los buffers.
 *
 * @param dht11_data Puntero donde se almacenarán los datos leídos.
 * @return esp_err_t Devuelve un ESP_OK si el proceso fue exitoso, sino ESP_FAIL.
 */
static esp_err_t dht11_read_data(dht11_data_t *dht11_data) {

    esp_err_t ret = dht11_start_and_receive();
    if (ret != ESP_OK) {
        return ret;
    }

    /*
    ESP_LOGI(TAG, "Cantidad de simbolos: %u", num_symbols);
    for (uint8_t i = 0; i < num_symbols; i++) {
        ESP_LOGI(TAG, "D0: %u S0: %u  -  D1: %u S1: %u", g_rx_buffer[i].duration0, g_rx_buffer[i].level0, g_rx_buffer[i].duration1, g_rx_buffer[i].level1);
    }*/

    ret = dht11_decode_data(dht11_data);
    if (ret != ESP_OK) return ret;
    xQueueReset(g_receive_queue);
    memset(g_rx_buffer, 0, sizeof(g_rx_buffer));
    return ESP_OK;
}


/**
 * @brief Función de inicialización del DHT11.
 *
 * Configura los GPIOs necesarios y el periférico RMT.
 * @return ESP_OK si todo fue exitoso.
 */
esp_err_t dht11_init(void) {
    esp_err_t ret = dht11_gpio_init();
    if (ret != ESP_OK) return ret;
    ret = dht11_rmt_rx_config();
    if (ret != ESP_OK) return ret;
    return ESP_OK;
}


/**
 * @brief Analiza los datos del sensor en busca de anomalías.
 *
 * Utiliza un algoritmo de Media Móvil Exponencial (EMA) para detectar desviaciones
 * bruscas en la temperatura.
 * - En estado NORMAL: Si el error absoluto supera el umbral dinámico, pasa a ALERTA.
 * - En estado ALERT: Si el error baja por debajo del umbral (con histéresis), vuelve a NORMAL.
 *
 * Genera paquetes MQTT de alerta cuando se producen transiciones de estado.
 *
 * @param data Puntero a la estructura de contexto del sensor.
 * @param get_data Indica si se debe realizar una nueva lectura física (true) o usar los datos existentes (false).
 */
void alert_analysis(data_t *data, bool get_data) {

    if (get_data) {
        if (dht11_read_data(&data->dht11) != ESP_OK) {
            return;
        }
    }
    const float temp_actual = data->dht11.temperature;
    const float error_actual = temp_actual - data->ema_temp;
    const float error_abs = fabsf(error_actual);
    const float umbral_alerta_dinamico = (K_SENSIBILIDAD * data->ema_error) + UMBRAL_MINIMO_ABS;

    switch (data->state_dht11) {
        case INIT_DHT11:
            data->ema_temp = data->dht11.temperature;
            data->ema_error = 0.0f;
            data->state_dht11 = NORMAL_DHT11;
            break;

        case NORMAL_DHT11:
            if (error_abs > umbral_alerta_dinamico) {
                data->state_dht11 = ALERT_DHT11;
                data->temp_before_alert = data->ema_temp;   // Guardamos la "normalidad" previa
                if (generate_message_alert_temp(&data->packet, (uint8_t)data->temp_before_alert, (uint8_t)temp_actual)) {
                    if (xQueueSend(queues.alert_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                        free(data->packet.payload);
                    }
                } else {
                    ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                }
            } else {
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_temp = (ALFA_TEMP * temp_actual) + ((1 - ALFA_TEMP) * data->ema_temp);
            break;

        case ALERT_DHT11:
            if (error_abs < (umbral_alerta_dinamico * HYSTERESIS)) {
                data->state_dht11 = NORMAL_DHT11;
                if (generate_message_alert_temp(&data->packet, (uint8_t)data->temp_before_alert, (uint8_t)temp_actual)) {
                    if (xQueueSend(queues.alert_buffer, &data->packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                        free(data->packet.payload);
                    }
                } else {
                    ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                }
                data->ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * data->ema_error);
            }
            data->ema_temp = (ALFA_TEMP * temp_actual) + ((1 - ALFA_TEMP) * data->ema_temp);
            break;
    }
}


/**
 * @brief Lógica para modos Balanced y Performance.
 *
 * En estos modos, se muestrea el sensor frecuentemente para analizar alertas,
 * pero solo se envía el reporte periódico de datos cuando el contador alcanza 'slices'.
 *
 * @param counter Puntero al contador de ciclos actual. Se reinicia al enviar datos.
 * @param slices Número de ciclos necesarios para enviar un reporte periódico.
 * @param data Puntero a la estructura de contexto.
 */
void dht11_task_in_balanced_or_performance(uint32_t *counter, uint32_t slices, data_t *data) {

    bool flag_error = false;

    if (dht11_read_data(&data->dht11) != ESP_OK) {
        flag_error = true;
    }

    if (*counter >= slices) {
        *counter = 0;
        if (flag_error) {
            dht11_data_t dht11;
            dht11.temperature = 0;
            dht11.humidity = 0;
            xQueueSend(queues.dht11_buffer, &dht11, portMAX_DELAY);
        }
        else {
            xQueueSend(queues.dht11_buffer, &data->dht11, pdMS_TO_TICKS(100));
        }
        xEventGroupSetBits(event_group.collector_events, DHT11_DATA_READY);
    }

    if (flag_error) {
        return;
    }
    alert_analysis(data, false);
}


/**
 * @brief Inicializa la estructura de datos local con valores por defecto.
 * @param data Puntero a la estructura data_t.
 */
static void init_data(data_t *data) {
    data->state_dht11 = INIT_DHT11;
    data->counter = 0;
    data->ema_temp = 20.0f;
    data->ema_error = 0.0f;
    data->temp_before_alert = 0.0f;
}


/**
 * @brief Tarea principal (FreeRTOS Task) para la gestión del DHT11.
 *
 * Implementa un patrón de doble bucle: espera pasivamente una notificación START
 * y luego entra en un bucle de trabajo activo. Ajusta dinámicamente el tiempo
 * de muestreo según el modo de energía configurado (LOW, BALANCED, PERFORMANCE).
 *
 * @param pvParameter Parámetro de tarea (no utilizado).
 */
void dht11_task(void *pvParameter) {
    static data_t data;
    init_data(&data);

    uint32_t counter = 0;
    uint32_t notification = 0;
    TickType_t dynamic_delay = pdMS_TO_TICKS(DHT11_LOW_DELAY); // Valor default seguro

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;
            counter = 0;

            while (running) {
                uint32_t sample_rate_min = settings_get_node_sample_rate();
                if (sample_rate_min == 0) sample_rate_min = 1;

                const energy_mode_t mode = settings_get_node_energy_mode();
                counter++;

                switch (mode) {
                    case LOW_CONSUMPTION: {
                        dynamic_delay = pdMS_TO_TICKS(DHT11_LOW_DELAY);
                        counter = 0;
                        alert_analysis(&data, true);
                        break;
                    }
                    case BALANCED: {
                        dynamic_delay = pdMS_TO_TICKS(DHT11_BALANCED_DELAY);
                        const uint32_t slices = (sample_rate_min * 60) / (DHT11_BALANCED_DELAY / 1000);
                        dht11_task_in_balanced_or_performance(&counter, slices, &data);
                        break;
                    }
                    case PERFORMANCE: {
                        dynamic_delay = pdMS_TO_TICKS(DHT11_PERFORMANCE_DELAY);
                        const uint32_t slices = (sample_rate_min * 60) / (DHT11_PERFORMANCE_DELAY / 1000);
                        dht11_task_in_balanced_or_performance(&counter, slices, &data);
                        break;
                    }
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