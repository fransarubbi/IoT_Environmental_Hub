#ifndef DATA_H
#define DATA_H


#define MPACK_DATA_SIZE  1024
#define MS_TO_MIN        60000
#define ALL_DATA_READY   (DHT11_DATA_READY | KY037_DATA_READY | MQ135_DATA_READY)
#define NOT_RETAIN       0
#define QOS_DATA         0
#define QOS_MONITOR      0
#define QOS_SETTING      1
#define QOS_ALERT        1


#include "freertos/FreeRTOS.h"

typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    float co2ppm;
    uint64_t time;
} data_sensors_t;


void data_collection_task(void *pvParameter);
void data_publish_task(void *pvParameter);


#endif //DATA_H