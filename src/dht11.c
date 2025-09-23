#include "DHT11/dht11.h"
#include <stdint.h>
#include <stdbool.h>
#include <driver/rmt_rx.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp32/rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


static const char *TAG = "DHT11";


static QueueHandle_t g_receive_queue = NULL;    // Cola
static rmt_channel_handle_t g_rx_channel = NULL;   // Canal
static rmt_symbol_word_t g_rx_buffer[RMT_BUFFER_SIZE];  // Buffer del rmt
static bool g_initialized = false;    // flag

dht11_data_t dht11_data;     // Estructura global



/**
 * @brief  Callback de interrupción RMT para la recepcion de datos.
 *
 * Esta funcion es llamada automaticamente por el driver RMT una vez que
 * un paquete de datos ha sido recibido completamente. Su propósito es
 * pasar los datos recibidos a una cola de FreeRTOS para que sean procesados
 * por una tarea de mayor prioridad.
 *
 * @note   Esta función se ejecuta en un contexto de interrupción (ISR),
 * por lo que debe ser lo más corta y rápida posible.
 *
 * @param  channel  El handle del canal RMT que activo el callback.
 * @param  edata    Puntero a la estructura de datos que contiene
 * la información del evento de recepcion, incluyendo los simbolos RMT.
 * @param  user_ctx El contexto de usuario pasado durante el registro del callback.
 * En este caso, se usa para pasar el handle de la cola
 * de FreeRTOS.
 *
 * @return Retorna 'true' si una tarea de mayor prioridad debe ser
 * despertada para procesar los datos de la cola, de lo contrario, 'false'.
 */
static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel,
                                           const rmt_rx_done_event_data_t *edata,
                                           void *user_ctx) {
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // Copiar datos del evento
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}


/**
 * @brief Configura GPIO con pull-up para DHT11
 */
static void dht11_gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT11_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,  // Open-drain bidireccional
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Pull-up habilitado
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error configurando GPIO: %s", esp_err_to_name(ret));
    }
}


/**
 * @brief Configura RMT para recepcion.
 */
static void dht11_rmt_rx_config(void) {
    rmt_rx_channel_config_t rx_chan_cfg;
    rmt_rx_event_callbacks_t cbs = {0};
    esp_err_t ret = ESP_OK;

    // Crear cola para que el callback ponga los eventos
    g_receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (g_receive_queue == NULL) {
        ESP_LOGE(TAG, "ERROR: Error creando cola de recepción");
        return;
    }

    // Configurar canal RX
    rx_chan_cfg.gpio_num = DHT11_PIN;
    rx_chan_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    rx_chan_cfg.resolution_hz = RMT_CLK_RES_HZ;
    rx_chan_cfg.mem_block_symbols = RMT_BUFFER_SIZE;
    rx_chan_cfg.flags.with_dma = false;
    rx_chan_cfg.flags.invert_in = false;
    rx_chan_cfg.flags.io_loop_back = false;

    ret = rmt_new_rx_channel(&rx_chan_cfg, &g_rx_channel);  // Crea el canal RMT
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error creando canal RMT RX: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Registrar callback
    cbs.on_recv_done = rmt_rx_done_callback;
    ret = rmt_rx_register_event_callbacks(g_rx_channel, &cbs, g_receive_queue);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error registrando callbacks: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "INFO: Canal RMT RX configurado correctamente");
    return;

cleanup:
    if (g_rx_channel) {
        rmt_del_channel(g_rx_channel);
    }
    if (g_receive_queue) {
        vQueueDelete(g_receive_queue);
        g_receive_queue = NULL;
    }
}


/**
 * @brief Envia señal de inicio al DHT11 y luego recibe los datos a traves del RMT.
 * @param num_symbols  Entero sin signo de 8 bits para contar la cantidad de
 * datos recibidos.
 * @return esp_err_t  Devuelve ESP_OK si la señal de inicio fue enviada correctamente
 * y se recibieron los datos dentro del tiempo definido por RMT_TIMEOUT. Si algo falla,
 * se retorna el mensaje de error.
 */
static esp_err_t dht11_start_and_receive(uint8_t *num_symbols) {

    if (!g_initialized || g_rx_channel == NULL || g_receive_queue == NULL) {
        ESP_LOGE(TAG, "ERROR: RMT no configurado correctamente.");
        return ESP_ERR_INVALID_STATE;
    }

    // Configurar recepcion
    rmt_receive_config_t rx_cfg = {
        .signal_range_min_ns = 2000,   // filtro de 2 micro seg
        .signal_range_max_ns = 2000000,   // filtro de 200 micro seg
    };

    esp_err_t ret = rmt_enable(g_rx_channel);   // Habilitar RMT
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error activando canal RMT: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);   // Configurar pin como salida
    gpio_set_level(DHT11_PIN, 0);                // Pull down
    ets_delay_us(DHT11_START_SIGNAL_LOW);             // Esperar 20 mili seg
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);   // Configurar pin como entrada

    // Recibir datos con RMT
    ret = rmt_receive(g_rx_channel, g_rx_buffer, sizeof(g_rx_buffer), &rx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error iniciando recepcion: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(DHT11_PIN, 1);          // Pull up
    ets_delay_us(DHT11_START_SIGNAL_HIGH);     // 40 micro seg

    rmt_rx_done_event_data_t event_data;   // Esperar datos con timeout
    if (xQueueReceive(g_receive_queue, &event_data, pdMS_TO_TICKS(RMT_TIMEOUT)) != pdTRUE) {
        ESP_LOGE(TAG, "ERROR: Timeout esperando datos.");
        return ESP_ERR_TIMEOUT;
    }
    rmt_disable(g_rx_channel);   // Deshabilitar RMT

    if (event_data.num_symbols == 0) {    // Validar que se recibieron datos
        ESP_LOGE(TAG, "ERROR: No se recibieron símbolos.");
        return ESP_ERR_INVALID_RESPONSE;
    }
    *num_symbols = event_data.num_symbols;
    ESP_LOGI(TAG, "INFO: Recibidos %d símbolos RMT", event_data.num_symbols);
    return ESP_OK;
}


/**
 * @brief Decodifica los datos de los símbolos RMT del sensor DHT11.
 *
 * Esta funcion procesa los símbolos RMT recibidos y los convierte en los
 * 5 bytes de datos del sensor. Se manejan las diferencias entre la primera
 * lectura (que puede no incluir la respuesta del sensor) y las lecturas
 * siguientes. Los datos decodificados se almacenan en la estructura global
 * dht11_data.
 *
 * @param symbols       Array de símbolos RMT recibidos.
 * @param num_symbols   Número total de símbolos en el array.
 * @return esp_err_t    Devuelve ESP_OK si la decodificacion es exitosa y el checksum es valido.
 * Devuelve ESP_FAIL en caso de fallo.
 */
static esp_err_t dht11_decode_data(const rmt_symbol_word_t *symbols, uint8_t num_symbols) {
    int8_t bit = 7;
    uint8_t buffer = 0;
    uint8_t byte = 0;

    if (num_symbols == 41) {
        for (uint8_t i = 0; i < num_symbols; i++) {
            if (symbols[i].duration0 > DHT11_DURATION0_MIN && symbols[i].duration0 < DHT11_DURATION0_MAX) {
                if (symbols[i].level0 == 0 && symbols[i].level1 == 1) {
                    if (symbols[i].duration1 > DHT11_DURATION1_MIN) {
                        if (symbols[i].duration1 > DHT11_DURATION1_BIT1) {
                            buffer = buffer | (0x01 << bit);
                        }
                        bit -= 1;
                        if (bit == -1) {
                            bit = 7;
                            switch (byte) {
                                case 0: dht11_data.humidity = buffer; break;
                                case 1: dht11_data.hum_decimal = buffer; break;
                                case 2: dht11_data.temperature = buffer; break;
                                case 3: dht11_data.temp_decimal = buffer; break;
                                case 4: dht11_data.checksum = buffer; break;
                                default: break;
                            }
                            buffer = 0;
                            byte += 1;
                        }
                    }
                }
            }
        }
    } else if (num_symbols == 42) {
        for (uint8_t i = 1; i < num_symbols; i++) {
            if (symbols[i].duration0 > DHT11_DURATION0_MIN && symbols[i].duration0 < DHT11_DURATION0_MAX) {
                if (symbols[i].level0 == 0 && symbols[i].level1 == 1) {
                    if (symbols[i].duration1 > DHT11_DURATION1_MIN) {
                        if (symbols[i].duration1 > DHT11_DURATION1_BIT1) {
                            buffer = buffer | (0x01 << bit);
                        }
                        bit -= 1;
                        if (bit == -1) {
                            bit = 7;
                            switch (byte) {
                                case 0: dht11_data.humidity = buffer; break;
                                case 1: dht11_data.hum_decimal = buffer; break;
                                case 2: dht11_data.temperature = buffer; break;
                                case 3: dht11_data.temp_decimal = buffer; break;
                                case 4: dht11_data.checksum = buffer; break;
                                default: break;
                            }
                            buffer = 0;
                            byte += 1;
                        }
                    }
                }
            }
        }
    } else {
        return ESP_FAIL;
    }

    // Verificar checksum
    uint8_t checksum = dht11_data.humidity + dht11_data.hum_decimal + dht11_data.temperature + dht11_data.temp_decimal;
    if (checksum != dht11_data.checksum) {
        ESP_LOGE(TAG, "ERROR: Checksum inválido: calculado %d, recibido %d", checksum, dht11_data.checksum);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}


/**
 * @brief Funcion de lectura de datos de la API.
 * @return esp_err_t Devuelve un ESP_OK si todo el proceso fue correcto y los
 * datos estan cargados en la estructura dht11_data. Si fallo el proceso, retorna un
 * mensaje de error.
 */
esp_err_t dht11_read_data(void) {
    uint8_t num_symbols;

    // 1. Enviar señal de inicio
    esp_err_t ret = dht11_start_and_receive(&num_symbols);
    if (ret != ESP_OK) {
        return ret;
    }

    // 2. Procesar datos
    ret = dht11_decode_data(g_rx_buffer, num_symbols);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ERROR: Error en la decodificacion de datos.");
        return ret;
    }
    xQueueReset(g_receive_queue);
    return ESP_OK;
}


/**
 * @brief Funcion de inicializacion del DHT11.
 */
void dht11_init(void) {
    dht11_gpio_init();
    dht11_rmt_rx_config();
    dht11_data.humidity = 0;
    dht11_data.hum_decimal = 0;
    dht11_data.temperature = 0;
    dht11_data.temp_decimal = 0;
    dht11_data.checksum = 0;
    gpio_set_level(DHT11_PIN, 1);
}

