#ifndef MESSAGE_H
#define MESSAGE_H

#include "Data/data.h"
#include "MQTT/mqtt.h"
#include "MQ135/mq135.h"
#include "Monitor/monitor.h"
#include "Setting/settings.h"

#define NOTIFY_CMD_DESTROY  0x02

#define FLAG_SERVER_VALID    0x01
#define FLAG_ITS_ME          0x02
#define FLAG_ITS_ALL         0x04
#define FLAG_STATE_OK        0x08
#define FLAG_EPOCH_VALID     0x10

bool generate_message_data(data_sensors_t data, mqtt_packet_t *packet);
bool generate_message_alert_air(mqtt_packet_t *packet, mq135_alert_t alert);
bool generate_message_alert_temp(mqtt_packet_t *packet, uint8_t temp_i, uint8_t temp_a);
bool generate_message_monitor(mqtt_packet_t *packet, stats_monitor_t stats);
bool generate_message_setting_ok(mqtt_packet_t *packet);
bool generate_message_firmware_ok(mqtt_packet_t *packet, bool is_ok);
bool generate_message_balance_mode_handshake(mqtt_packet_t *packet);
bool generate_message_settings(mqtt_packet_t *packet);


bool parse_message_state_normal(const char* data, size_t len);
bool parse_message_state_balance(const char* data, size_t len);
bool parse_message_state_safe(const char* data, size_t len);
bool parse_message_handshake(const char* data, size_t len);
bool parse_message_heartbeat(const char* data, size_t len);
bool parse_message_new_firmware(const char* data, size_t len);
bool parse_message_setting(const char* data, size_t len);
bool parse_message_setting_ok(const char* data, size_t len);
bool parse_message_delete(const char* data, size_t len);
bool parse_message_active(const char* data, size_t len);

#endif //MESSAGE_H