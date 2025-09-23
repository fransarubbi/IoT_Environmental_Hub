#ifndef SENSORS_H
#define SENSORS_H

#define SAMPLE 2000

#include <stdint.h>
#include "freertos/queue.h"

typedef struct {
    uint8_t air_quality;
    uint8_t temperature;
    uint8_t humidity;
    uint8_t noise;
} data_sensors_t;

extern QueueHandle_t data_sensors_queue;

void sensors_init();
void task_sensors(void *);

#endif //SENSORS_H