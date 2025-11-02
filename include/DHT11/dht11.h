#ifndef DHT11_H
#define DHT11_H


#define DHT11_PIN                GPIO_NUM_4      // Pin 4
#define DHT11_START_SIGNAL_LOW     20000    // 20 ms señal baja de inicio
#define DHT11_START_SIGNAL_HIGH    40       // 30 micro seg señal alta de inicio
#define DHT11_DELAY 10000    // 10 seg
#define DHT11_JSON_ALERT 100
#define STACK_DHT11 2000

#define DHT11_DATA_READY  (1 << 0)
#define MQ135_DATA_READY  (1 << 1)


#include <esp_err.h>
#include <stdint.h>
#include <freertos/task.h>


/* ===== Estructura de datos ===== */
typedef struct {
    uint8_t temperature;    // Parte entera de temperatura
    uint8_t temp_decimal;   // Parte decimal de temperatura
    uint8_t humidity;       // Parte entera de humedad
    uint8_t hum_decimal;    // Parte decimal de humedad
    uint8_t checksum;       // Checksum recibido
} dht11_data_t;


extern TaskHandle_t dht11_handle;
extern QueueHandle_t dht11_buffer;
typedef enum{INIT, GET_DATA, DELTA, ALERT} state_dht11_t;


/* ===== API ===== */
esp_err_t dht11_init(void);
void dht11_task(void *);


#endif //DHT11_H