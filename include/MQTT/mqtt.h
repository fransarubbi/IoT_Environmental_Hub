#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define MQTT_CONNECTED_BIT    BIT0
#define MQTT_DISCONNECTED_BIT BIT1

#include "esp_err.h"
#include "mqtt_client.h"


typedef struct {
    char *payload;
    size_t len;
} mqtt_packet_t;


esp_err_t mqtt_init(void);
int mqtt_publish(const char *topic, const char *payload, int len, int qos, int retain);


#endif // MQTT_CLIENT_H
