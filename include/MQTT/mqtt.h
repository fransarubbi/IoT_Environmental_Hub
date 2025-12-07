#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define MQTT_CONNECTED_BIT    BIT0
#define MQTT_DISCONNECTED_BIT BIT1

#include "esp_err.h"
#include "mqtt_client.h"


typedef struct {
    esp_mqtt_client_handle_t client;   // handler de ESP-IDF para el cliente MQTT
    esp_mqtt_client_config_t config;   // configuracion (URI, credenciales, etc.)
} mqtt_client_t;


extern mqtt_client_t mqtt;


esp_err_t mqtt_init(void);
esp_err_t mqtt_publish(const char *topic, const char *payload, int len, int qos, int retain);



#endif // MQTT_CLIENT_H
