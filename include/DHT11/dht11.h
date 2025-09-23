#ifndef DHT11_H
#define DHT11_H


#define DHT11_PIN                GPIO_NUM_4      // Pin 4
#define DHT11_RMT_CHANNEL        RMT_CHANNEL_0   // Canal RMT
#define DHT11_START_SIGNAL_LOW     20000    // 20 ms señal baja de inicio
#define DHT11_START_SIGNAL_HIGH    30       // 30 micro seg señal alta de inicio
#define RMT_CLK_RES_HZ             1000000    // Reloj del RMT
#define RMT_BUFFER_SIZE            64          // Tamaño del buffer RMT
#define RMT_TIMEOUT                10        // Timeout de recepcion
#define DHT11_DURATION0_MIN        50       // 50 micro seg bit de inicio min
#define DHT11_DURATION0_MAX        60       // 60 micro seg bit de inicio max
#define DHT11_DURATION1_MIN        20       // Filtrar menores de 20
#define DHT11_DURATION1_BIT1       65       // 65 micro seg para bit 1


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