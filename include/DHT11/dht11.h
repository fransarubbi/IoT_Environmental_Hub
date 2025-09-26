#ifndef DHT11_H
#define DHT11_H


#define DHT11_PIN                GPIO_NUM_4      // Pin 4
#define DHT11_START_SIGNAL_LOW     20000    // 20 ms señal baja de inicio
#define DHT11_START_SIGNAL_HIGH    40       // 30 micro seg señal alta de inicio


#include <esp_err.h>
#include <stdint.h>


/* ===== Estructura de datos ===== */
typedef struct {
    uint8_t temperature;    // Parte entera de temperatura
    uint8_t temp_decimal;   // Parte decimal de temperatura
    uint8_t humidity;       // Parte entera de humedad
    uint8_t hum_decimal;    // Parte decimal de humedad
    uint8_t checksum;       // Checksum recibido
} dht11_data_t;

extern dht11_data_t dht11_data;


/* ===== Declaracion de funciones de la API ===== */
void dht11_init(void);
esp_err_t dht11_read_data(void);


#endif //DHT11_H