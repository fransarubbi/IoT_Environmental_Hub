/**
* @file message.h
 * @brief Generación y Parseo de mensajes MPack sobre MQTT.
 *
 * Este módulo define las funciones para serializar estructuras de datos del sistema
 * a formato MPack (MessagePack) para su transmisión, y para deserializar
 * payloads entrantes y extraer comandos o configuraciones.
 */


#ifndef MESSAGE_H
#define MESSAGE_H

#include "Data/data.h"
#include "Fsm/fsm.h"
#include "MQTT/mqtt.h"
#include "MQ135/mq135.h"
#include "Monitor/monitor.h"
#include "Setting/settings.h"

#define FLAG_PHASE_ALERT     1
#define FLAG_PHASE_DATA      2
#define FLAG_PHASE_MONITOR   3

#define MPACK_DATA_SIZE          256
#define MPACK_MQ135_ALERT_SIZE   192
#define MPACK_DHT11_ALERT_SIZE   192
#define MPACK_MONITOR_SIZE       192
#define MPACK_SETTINGS_SIZE      256
#define MPACK_FIRMWARE_OK_SIZE   128
#define MPACK_HANDSHAKE_SIZE     128
#define MPACK_SETTINGS_OK_SIZE   128
#define MPACK_PING_SIZE          128
#define MPACK_EMPTY_SIZE         128

bool generate_message_data(data_sensors_t data, mqtt_packet_t *packet);
bool generate_message_alert_air(mqtt_packet_t *packet, mq135_alert_t alert);
bool generate_message_alert_temp(mqtt_packet_t *packet, uint8_t temp_i, uint8_t temp_a);
bool generate_message_monitor(mqtt_packet_t *packet, stats_monitor_t stats);
bool generate_message_setting_ok(mqtt_packet_t *packet);
bool generate_message_firmware_ok(mqtt_msg_general_t *packet, bool is_ok);
bool generate_message_balance_mode_handshake(mqtt_msg_general_t *packet);
bool generate_message_settings(mqtt_packet_t *packet);
bool generate_message_ping(mqtt_msg_general_t *packet);
bool generate_message_empty_queue(mqtt_msg_general_t *packet, State current_phase);

bool parse_message_state_normal(const char* data, size_t len);
bool parse_message_state_balance(const char* data, size_t len);
bool parse_message_state_safe(const char* data, size_t len);
bool parse_message_phase(const char* data, size_t len);
bool parse_message_handshake(const char* data, size_t len);
bool parse_message_heartbeat(const char* data, size_t len);
bool parse_message_new_firmware(const char* data, size_t len);
bool parse_message_setting(const char* data, size_t len);
bool parse_message_setting_ok(const char* data, size_t len);
bool parse_message_delete(const char* data, size_t len);
bool parse_message_active(const char* data, size_t len);

#endif //MESSAGE_H