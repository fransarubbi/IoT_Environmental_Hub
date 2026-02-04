/**
* @file data.h
 * @brief Definición de estructuras y constantes para la gestión de datos del sistema.
 *
 * Este archivo define los niveles de Calidad de Servicio (QoS) para MQTT según el
 * protocolo de coordinación, así como la estructura unificada de datos de sensores
 * y los prototipos de las tareas principales de recolección y publicación.
 */


#ifndef DATA_H
#define DATA_H

#include "freertos/FreeRTOS.h"

#define MS_TO_MIN        60000   /**< Conversión de milisegundos a minutos. */
#define ALL_DATA_READY   (DHT11_DATA_READY | KY037_DATA_READY | MQ135_DATA_READY)
#define NOT_RETAIN       0
#define QOS_DATA         0
#define QOS_MONITOR      0
#define QOS_SETTING      1
#define QOS_ALERT        1
#define QOS_FIRMWARE     1
#define QOS_HANDSHAKE    1
#define QOS_PING         1
#define QOS_EMPTY        1


/**
 * @brief Estructura de lecturas de sensores.
 * Se utiliza para pasar un "snapshot" temporal de todos los sensores
 * desde la tarea de recolección hacia la generación del paquete MPack.
 */
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