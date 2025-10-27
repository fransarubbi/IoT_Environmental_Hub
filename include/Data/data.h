#ifndef DATA_H
#define DATA_H

#define JSON_MAX 550


#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    float co2ppm;
    float coppm;
    float nh3ppm;
    float c6h6ppm;
    float no2ppm;
    char time[50];
} data_sensors_t;


extern QueueHandle_t data_buffer;
extern TaskHandle_t data_ct_handle;
extern TaskHandle_t data_pt_handle;


void data_collection_task(void *pvParameter);
void data_publish_task(void *pvParameter);


#endif //DATA_H