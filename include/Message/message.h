#ifndef MESSAGE_H
#define MESSAGE_H

#include "Data/data.h"
#include "MQTT/mqtt.h"
#include "MQ135/mq135.h"
#include "Monitor/monitor.h"
#include "Setting/settings.h"

bool generate_message_data(data_sensors_t data, mqtt_packet_t *packet);
bool generate_message_alert_air(mqtt_packet_t *packet, mq135_alert_t alert);
bool generate_message_alert_temp(mqtt_packet_t *packet, uint8_t temp_i, uint8_t temp_a);
bool generate_message_monitor(mqtt_packet_t *packet, stats_monitor_t stats);
bool generate_message_setting_ok(mqtt_packet_t *packet);
bool generate_message_firmware_ok(mqtt_packet_t *packet, bool is_ok);
bool generate_message_balance_mode_handshake(mqtt_packet_t *packet);
bool generate_message_settings(mqtt_packet_t *packet);



bool parse_message_setting(const char* data, size_t len);
bool parse_message_setting_ok(const char* data, size_t len);


#endif //MESSAGE_H