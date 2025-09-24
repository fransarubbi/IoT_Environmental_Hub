#ifndef SENSORS_H
#define SENSORS_H


#include <stdint.h>


typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_temp_decimal;     // Parte decimal de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    uint8_t dht11_hum_decimal;      // Parte decimal de humedad
    uint8_t dht11_checksum;         // Checksum dht11

    uint8_t air_quality;
} data_sensors_t;

extern data_sensors_t data_sensors;


void sensors_init();
void task_sensors(void *);

#endif //SENSORS_H