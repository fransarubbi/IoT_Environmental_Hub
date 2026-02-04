#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#define MQTT_CONNECTED_BIT    BIT0
#define MQTT_DISCONNECTED_BIT BIT1

#include "esp_err.h"
#include "mqtt_client.h"
#include "Setting/settings.h"


typedef struct {
    char *payload;
    size_t len;
} mqtt_packet_t;


typedef struct {
    char topic[MAX_TOPIC];
    char *payload;
    size_t len;
} mqtt_msg_to_parse_t;


typedef struct {
    topic_general topic;
    char *payload;
    size_t len;
} mqtt_msg_general_t;


esp_err_t mqtt_init(void);
int mqtt_publish(const char *topic, const char *payload, int len, int qos, int retain);
void mqtt_enable_subscribe_topics(void);


#endif // MQTT_CLIENT_H
