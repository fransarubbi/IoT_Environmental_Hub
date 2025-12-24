#ifndef DATA_H
#define DATA_H

#define MPACK_DATA_SIZE 1024
#define MS_TO_MIN 60000
#define ALL_DATA_READY    (DHT11_DATA_READY | KY037_DATA_READY | MQ135_DATA_READY)


#include "freertos/FreeRTOS.h"


void data_collection_task(void *pvParameter);
void data_publish_task(void *pvParameter);


#endif //DATA_H