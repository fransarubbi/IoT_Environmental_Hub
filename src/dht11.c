#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "DHT11/dht11.h"
#include "MQTT/mqtt.h"
#include <esp_timer.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp32/rom/ets_sys.h"
#include <string.h>
#include "Setting/settings.h"
#include "Data/data.h"


static const char *TAG = "DHT11";
static dht11_data_t dht11_data;



/**
 * @brief Configura GPIO con pull-up para DHT11
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
 * @brief Funcion de lectura de datos de la API.
 * @return esp_err_t Devuelve un ESP_OK si todo el proceso fue correcto y los
 * datos estan cargados en la estructura dht11_data. Si fallo el proceso, retorna un
 * mensaje de error.
 */
static esp_err_t dht11_read_data(void) {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    // Reiniciar datos
    memset(&dht11_data, 0, sizeof(dht11_data));

    // Deshabilitar interrupciones
    portENTER_CRITICAL(&mux);
    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0); // Pull down
    ets_delay_us(DHT11_START_SIGNAL_LOW); // 20 ms
    gpio_set_level(DHT11_PIN, 1); // Pull up
    ets_delay_us(DHT11_START_SIGNAL_HIGH); // 40 micro s
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT); // Liberar la linea

    uint32_t timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 1 && timeout++ < 100) ets_delay_us(1);
    if (timeout >= 100) return ESP_ERR_TIMEOUT;

    timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 0 && timeout++ < 100) ets_delay_us(1);
    if (timeout >= 100) return ESP_ERR_TIMEOUT;

    timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 1 && timeout++ < 100) ets_delay_us(1);
    if (timeout >= 100) return ESP_ERR_TIMEOUT;

    // Leer 40 bits (5 bytes)
    uint8_t data[5] = {0};
    for (uint8_t i = 0; i < 40; i++) {
        // Esperar inicio de bit (50 micro s LOW)
        while (gpio_get_level(DHT11_PIN) == 0);

        uint32_t start_time = esp_timer_get_time();
        while (gpio_get_level(DHT11_PIN) == 1);
        uint32_t duration = esp_timer_get_time() - start_time;

        // Si duracion > 40 micro s es un '1', sino es '0'
        if (duration > 40) {
            data[i/8] |= (1 << (7 - (i%8)));
        }
    }

    portEXIT_CRITICAL(&mux);

    dht11_data.humidity = data[0];
    dht11_data.hum_decimal = data[1];
    dht11_data.temperature = data[2];
    dht11_data.temp_decimal = data[3];
    dht11_data.checksum = data[4];

    // Verificar checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != dht11_data.checksum) {
        ESP_LOGE(TAG, "- ERROR: Checksum invalido: calculado %d, recibido %d -", checksum, dht11_data.checksum);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Verificar que los datos no sean todos 0
    if (dht11_data.humidity == 0 && dht11_data.hum_decimal == 0 &&
        dht11_data.temperature == 0 && dht11_data.temp_decimal == 0) {
        ESP_LOGE(TAG, "- ERROR: Datos invalidos (todos 0) -");
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}


/**
 * @brief Funcion de inicializacion del DHT11.
 */
esp_err_t dht11_init(void) {
    esp_err_t ret = dht11_gpio_init();
    dht11_data.humidity = 0;
    dht11_data.hum_decimal = 0;
    dht11_data.temperature = 0;
    dht11_data.temp_decimal = 0;
    dht11_data.checksum = 0;
    return ret;
}


/**
 * @brief Funcion que genera un JSON con la temperatura inicial y la actual.
 *
 * Genera el JSON que sera enviado en caso de una alerta por aumento brusco de
 * temperatura. Por ello sen envia la primer temperatura estable y la temperatura actual.
 *
 * @param output_buffer String formateado en JSON.
 * @param buffer_size Tamaño del string.
 * @param temp_i Temperatura inicial.
 * @param temp_a Temperatura actual.
 */
static void generate_json(char *output_buffer, size_t buffer_size, uint8_t temp_i, uint8_t temp_a) {

    snprintf(output_buffer, buffer_size,
        "{\n"
        "  \"Temperatura inicial\": %u,\n"
        "  \"Temperatura actual\": %u,\n"
        "}",
        temp_i,
        temp_a
    );
}


/**
 * @brief Tarea que monitorea temperatura DHT11 y detecta cambios bruscos
 *
 * Implementa una máquina de estados que detecta incrementos rapidos de temperatura
 * (≥2°C iniciales, ≥3°C confirmacion) y publica alertas MQTT.
 *
 * Estados:
 * - INIT: Inicializacion, lee temperatura base.
 * - GET_DATA: Monitoreo normal, detecta primera subida ≥2°C.
 * - DELTA: Verificacion, confirma si es anomalia real o fluctuacion.
 * - ALERT: Alerta activa, publica alarma (solo una vez) hasta que baje.
 *
 * @param pvParameter Parámetro de la tarea (no usado).
 */
void dht11_task(void *pvParameter) {
    static state_dht11_t state_dht11 = INIT;
    static uint8_t old_temp = 0;
    static uint8_t backup_temp = 0;
    static bool alert_sent = false;         // Prevenir spam de alertas
    static uint32_t readings_in_alert = 0;  // Contador para timeout
    static uint32_t counter = 0;
    char json[DHT11_JSON_ALERT];
    dht11_data_t dht11;
    int8_t delta_temp;

    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {

        uint32_t slices = (settings.sample_rate * 60)/(DHT11_DELAY/1000);
        counter++;

        if (counter >= slices) {
            counter = 0;
            if (dht11_read_data() != ESP_OK) {
                dht11.temperature = 0;
                dht11.humidity = 0;
            }
            dht11.temperature = dht11_data.temperature;
            dht11.humidity = dht11_data.humidity;
            xQueueSend(dht11_buffer, &dht11, portMAX_DELAY);
            xEventGroupSetBits(collector_events, DHT11_DATA_READY);
        }
        else {
            if (dht11_read_data() != ESP_OK) {
                dht11.temperature = 0;
                dht11.humidity = 0;
            }

            switch (state_dht11) {  // ===== MAQUINA DE ESTADOS =====
            case INIT:
                old_temp = dht11_data.temperature;
                backup_temp = old_temp;
                state_dht11 = GET_DATA;
                break;

            case GET_DATA:
                delta_temp = (int8_t)(dht11_data.temperature - old_temp);
                backup_temp = old_temp;
                old_temp = dht11_data.temperature;

                if (delta_temp >= 2) {
                    state_dht11 = DELTA;
                    alert_sent = false;  // Resetear flag de alerta
                }
                break;

            case DELTA:
                delta_temp = (int8_t)(dht11_data.temperature - old_temp);
                old_temp = dht11_data.temperature;

                // Caso 1: Sigue subiendo rapidamente → ALERT
                if (delta_temp >= 3) {
                    state_dht11 = ALERT;
                    readings_in_alert = 0;
                }
                else {   // Caso 2: Se estabilizo o bajo
                    // Comparar con temperatura de inicio (backup_temp)
                    int8_t total_delta = (int8_t)(dht11_data.temperature - backup_temp);

                    if (total_delta <= 2) {
                        // Falsa alarma, volver a monitoreo normal
                        state_dht11 = GET_DATA;
                    }
                }
                break;

            case ALERT:
                delta_temp = (int8_t)(dht11_data.temperature - old_temp);

                if (!alert_sent) {
                    generate_json(json, DHT11_JSON_ALERT, backup_temp, dht11_data.temperature);
                    if (mqtt_publish("/dht11/alert", json, (int)strlen(json), 2, 0) == ESP_OK) {
                        alert_sent = true;
                    }
                }

                old_temp = dht11_data.temperature;
                readings_in_alert++;

                // Caso 1: Temperatura empieza a bajar
                if (delta_temp < 0) {
                    state_dht11 = DELTA;
                    readings_in_alert = 0;
                }
                // Caso 2: Temperatura se estabilizo (sin subir mas)
                else if (delta_temp == 0 && readings_in_alert >= 5) {
                    // Después de 5 lecturas estables, considerar que se controlo
                    int8_t total_delta = (int8_t)(dht11_data.temperature - backup_temp);
                    if (total_delta <= 3) {
                        state_dht11 = GET_DATA;
                        readings_in_alert = 0;
                    }
                }
                // Caso 3: Sigue subiendo
                else if (delta_temp > 0) {
                    // Reenviar alerta cada 10 lecturas si sigue subiendo
                    if (readings_in_alert % 10 == 0) {
                        generate_json(json, DHT11_JSON_ALERT, backup_temp, dht11_data.temperature);
                        mqtt_publish("/dht11/alert", json, (int)strlen(json), 2, 0);
                    }
                }
                break;
            }
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(DHT11_DELAY));
    }
}