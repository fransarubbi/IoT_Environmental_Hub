#ifndef DHT11_H
#define DHT11_H


#define DHT11_PIN                  GPIO_NUM_4      // Pin 4
#define DHT11_START_SIGNAL_LOW     20000    // 20 ms señal baja de inicio
#define DHT11_START_SIGNAL_HIGH    40       // 30 micro seg señal alta de inicio
#define DHT11_LOW_DELAY            10000    // 10 seg
#define DHT11_BALANCED_DELAY       5000     // 5 seg
#define DHT11_PERFORMANCE_DELAY    2000     // 2 seg

#define DHT11_RMT_CHANNEL          RMT_CHANNEL_0   // Canal RMT
#define DHT11_START_SIGNAL_LOW     20000           // 20 ms señal baja de inicio
#define RMT_CLK_RES_HZ             1000000         // Reloj del RMT
#define RMT_BUFFER_SIZE            128             // Tamaño del buffer RMT
#define RMT_TIMEOUT                100             // Timeout de recepcion
#define DHT11_DURATION0_MIN        50              // 50 micro seg bit de inicio min
#define DHT11_DURATION0_MAX        60              // 60 micro seg bit de inicio max
#define DHT11_DURATION1_MIN        20              // Filtrar menores de 20
#define DHT11_DURATION1_BIT1       65              // 65 micro seg para bit 1


#define BETA_ERROR                 0.05f         // Factor de suavizado para el error (0.05 = muy lento)
#define K_SENSIBILIDAD             2.0f          // 2 "sigmas". Más alto = menos sensible
#define UMBRAL_MINIMO_ABS          100.0f
#define HYSTERESIS                 0.8f          // Necesita bajar al 80% del umbral para desactivar

#define DHT11_DATA_READY  (1 << 0)

#include <esp_err.h>
#include <freertos/task.h>


typedef struct {
    uint8_t temperature;    // Parte entera de temperatura
    uint8_t temp_decimal;   // Parte decimal de temperatura
    uint8_t humidity;       // Parte entera de humedad
    uint8_t hum_decimal;    // Parte decimal de humedad
    uint8_t checksum;       // Checksum recibido
} dht11_data_t;



/* ===== API ===== */
esp_err_t dht11_init(void);
void dht11_task(void *);
uint8_t dht11_get_temperature(const dht11_data_t*);
uint8_t dht11_get_humidity(const dht11_data_t*);
size_t dht11_struct_get_size(void);


#endif //DHT11_H