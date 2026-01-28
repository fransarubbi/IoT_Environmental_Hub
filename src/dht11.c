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
#include "Message/message.h"
#include "Time/time.h"


static const char *TAG = "DHT11";
static QueueHandle_t g_receive_queue = NULL;
static rmt_channel_handle_t g_rx_channel = NULL;
static rmt_symbol_word_t g_rx_buffer[120];
static uint8_t num_symbols;
typedef enum {INIT_DHT11, NORMAL_DHT11, ALERT_DHT11} state_dht11_t;


uint8_t dht11_get_temperature(dht11_data_t *dhtt) {
    return dhtt->temperature;
}


uint8_t dht11_get_humidity(dht11_data_t *dhtt) {
    return dhtt->humidity;
}


size_t dht11_struct_get_size(void) {
    return sizeof(dht11_data_t);
}



/**
 * @brief  Callback de interrupcion RMT para la recepcion de datos.
 *
 * Esta funcion es llamada automaticamente por el driver RMT una vez que
 * un paquete de datos ha sido recibido completamente. Su propósito es
 * pasar los datos recibidos a una cola de FreeRTOS para que sean procesados
 * por una tarea de mayor prioridad.
 *
 * @param  channel  El handle del canal RMT que activo el callback.
 * @param  edata    Puntero a la estructura de datos que contiene
 * la información del evento de recepcion, incluyendo los simbolos RMT.
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
 * @brief Configura GPIO con pull-up para DHT11
 *
 * @return esp_err_t Devuelve ESP_OK si fue exitosa la configuracion, sino ESP_FAIL.
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
 * @brief Configura RMT para recepcion (RX).
 *
 * @return esp_err_t Devuelve ESP_OK si la configuracion fue exitosa, sino ESP_FAIL.
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
 * @brief Envia señal de inicio al DHT11 y luego recibe los datos a traves del RMT.
 *
 * Se realiza el handshake segun el protocolo que implementa el DHT11. Para poder
 * sincronizar la transmision, se realiza un pequeño polling del pin para poder recibir
 * los datos correctamente.
 *
 * @return esp_err_t  Devuelve ESP_OK si la señal de inicio fue enviada correctamente
 * y se recibieron los datos. Si algo falla, se retorna el mensaje de error.
 */
static esp_err_t dht11_start_and_receive(void) {

    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = 900000,
        .flags.en_partial_rx = false
    };
    esp_err_t ret = ESP_OK;
    uint32_t timeout_us; // Para los timeouts de espera

    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0);
    ets_delay_us(DHT11_START_SIGNAL_LOW);  // 20ms LOW
    gpio_set_level(DHT11_PIN, 1);
    ets_delay_us(DHT11_START_SIGNAL_HIGH); // 40us HIGH
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);   // liberar la linea

    // Esperar a que el sensor ponga la línea en BAJO (inicio del handshake)
    timeout_us = 0;
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
 * Esta funcion procesa los símbolos RMT recibidos y los convierte en los
 * 5 bytes de datos del sensor. Finalmente guarda los resultados en la estructura
 * global dht11_data_t.
 *
 * @return esp_err_t  Devuelve ESP_OK si la decodificacion es exitosa y el checksum es valido.
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
 * @brief Funcion de lectura de datos
 *
 * Llama a dos funciones, la que se encarga del handshake y transmision de datos
 * y la funcion que luego decodifica la informacion.
 *
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
 * @brief Funcion de inicializacion del DHT11.
 */
esp_err_t dht11_init(void) {
    esp_err_t ret = dht11_gpio_init();
    if (ret != ESP_OK) return ret;
    ret = dht11_rmt_rx_config();
    if (ret != ESP_OK) return ret;
    return ESP_OK;
}


/**
 * @brief Tarea que monitorea temperatura DHT11 y detecta cambios bruscos
 *
 * Implementa una máquina de estados que detecta incrementos rapidos de temperatura
 * y publica alertas MQTT, tanto cuando se genera el aumento como cuando se estabiliza de nuevo
 *
 * Estados:
 * - INIT: Inicializacion, lee temperatura base.
 * - NORMAL: Monitoreo normal, cuando se detecta el aumento de temperatura se emite la alerta.
 * - ALERT: Permanece en este estado hasta que la temperatura se estabilice, luego emite una alerta.
 *
 * @param pvParameter Parámetro de la tarea (no usado).
 */
void dht11_task(void *pvParameter) {
    static state_dht11_t state_dht11 = INIT_DHT11;
    static uint32_t counter = 0;
    static float ema_temp = 20.0f;            // Media movil de la temperatura
    static float ema_error = 0.0f;            // Media movil del error (ruido normal)
    static float temp_before_alert = 0.0f;
    static dht11_data_t dht11;
    mqtt_packet_t packet;

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {

        uint32_t slices = (settings_get_node_sample_rate() * 60)/(DHT11_DELAY/1000);
        counter++;

        if (counter >= slices) {
            counter = 0;
            if (dht11_read_data(&dht11) != ESP_OK) {
                dht11.temperature = 0;
                dht11.humidity = 0;
            }
            xQueueSend(queues.dht11_buffer, &dht11, portMAX_DELAY);
            xEventGroupSetBits(event_group.collector_events, DHT11_DATA_READY);
        }
        else {
            if (dht11_read_data(&dht11) != ESP_OK) {
                dht11.temperature = 0;
                dht11.humidity = 0;
            }
            float temp_actual = dht11.temperature;
            float error_actual = temp_actual - ema_temp;
            float error_abs = fabsf(error_actual);
            float umbral_alerta_dinamico = (K_SENSIBILIDAD * ema_error) + UMBRAL_MINIMO_ABS;

            switch (state_dht11) {
            case INIT_DHT11:
                    ema_temp = dht11.temperature;
                    ema_error = 0.0f;
                    state_dht11 = NORMAL_DHT11;
                    break;

            case NORMAL_DHT11:
                    if (error_abs > umbral_alerta_dinamico) {
                        state_dht11 = ALERT_DHT11;
                        temp_before_alert = ema_temp;   // Guardamos la "normalidad" previa
                        if (generate_message_alert_temp(&packet, (uint8_t)temp_before_alert, (uint8_t)temp_actual)) {
                            if (xQueueSend(queues.alert_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                                ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                                free(packet.payload);
                            }
                        } else {
                            ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                        }
                    } else {
                        ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * ema_error);
                    }
                    ema_temp = (ALFA_TEMP * temp_actual) + ((1 - ALFA_TEMP) * ema_temp);
                    break;

            case ALERT_DHT11:
                    if (error_abs < (umbral_alerta_dinamico * HYSTERESIS)) {
                        state_dht11 = NORMAL_DHT11;
                        if (generate_message_alert_temp(&packet, (uint8_t)temp_before_alert, (uint8_t)temp_actual)) {
                            if (xQueueSend(queues.alert_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                                ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                                free(packet.payload);
                            }
                        } else {
                            ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
                        }
                        ema_error = (BETA_ERROR * error_abs) + ((1 - BETA_ERROR) * ema_error);
                    }
                    ema_temp = (ALFA_TEMP * temp_actual) + ((1 - ALFA_TEMP) * ema_temp);
                    break;
            }
        }
        xQueueSend(queues.dht11_to_mq135, &dht11, portMAX_DELAY);
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(DHT11_DELAY));
    }
}