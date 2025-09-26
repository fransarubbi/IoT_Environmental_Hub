#include "DHT11/dht11.h"
#include <esp_timer.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp32/rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


static const char *TAG = "DHT11";


dht11_data_t dht11_data;


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

    gpio_set_level(DHT11_PIN, 1); // Asegurar estado alto inicial
    vTaskDelay(pdMS_TO_TICKS(1000)); // Esperar 1 segundo para estabilizar el sensor
}


/**
 * @brief Funcion de lectura de datos de la API.
 * @return esp_err_t Devuelve un ESP_OK si todo el proceso fue correcto y los
 * datos estan cargados en la estructura dht11_data. Si fallo el proceso, retorna un
 * mensaje de error.
 */
esp_err_t dht11_read_data(void) {
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
void dht11_init(void) {
    dht11_gpio_init();
    dht11_data.humidity = 0;
    dht11_data.hum_decimal = 0;
    dht11_data.temperature = 0;
    dht11_data.temp_decimal = 0;
    dht11_data.checksum = 0;
}

