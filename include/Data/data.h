#ifndef DATA_H
#define DATA_H

#define JSON_MAX 600
#define QUEUE_LENGTH 100
#define QUEUE 2
#define MS_TO_MIN 60000
#define STACK_COLLECTOR 4000
#define STACK_PUBLISHER 3000
#define STACK_MONITOR 4000
#define STACK_MIC 1000
#define WAIT 1000
#define TIME_SETUP 240000

#define ALL_DATA_READY    (DHT11_DATA_READY | KY037_DATA_READY | MQ135_DATA_READY)


#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "MQ135/mq135.h"


typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    float co2ppm;
    char time[50];
} data_sensors_t;


extern QueueHandle_t data_buffer;
extern QueueHandle_t system_buffer;
extern TaskHandle_t data_ct_handle;
extern TaskHandle_t data_pt_handle;
extern EventGroupHandle_t collector_events;


void data_collection_task(void *pvParameter);
void data_publish_task(void *pvParameter);
void stack_monitor_task(void *pvParameter);


#endif //DATA_H