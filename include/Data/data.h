#ifndef SENSORS_H
#define SENSORS_H

#define ID_KY037 0
#define ID_DHT11 1
#define ID_MQ135 2

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_temp_decimal;     // Parte decimal de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    uint8_t dht11_hum_decimal;      // Parte decimal de humedad

    uint8_t air_quality;
} data_sensors_t;


void data_json_encrypt_task(void *);


#endif //SENSORS_H