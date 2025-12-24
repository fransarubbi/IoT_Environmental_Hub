#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include "driver/uart.h"
#include <string.h>


/* ---- Configuraciones del sistema ---- */
#define SETTINGS_UART_PORT_NUM        UART_NUM_0
#define SETTINGS_UART_BAUD_RATE       115200
#define SETTINGS_BUFFER_SIZE          256
#define MQTTS_PREFIX                  "mqtts://"
#define MQTTS_PREFIX_LEN              (sizeof(MQTTS_PREFIX) - 1)
#define MAX_FREQ                      240
#define MIN_FREQ                      80
#define MPACK_SETTINGS_SIZE           1024

#define FLAG_SERVER_VALID    (1 << 0)  // 0000 0001 (Hex: 0x01)
#define FLAG_CLIENT_VALID    (1 << 1)  // 0000 0010 (Hex: 0x02)
#define FLAG_ITS_ME          (1 << 2)  // 0000 0100 (Hex: 0x04)
#define FLAG_ITS_ALL         (1 << 3)  // 0000 1000 (Hex: 0x08)



/* ---- Macros de longitudes ---- */
#define MAC           18
#define DEVICE_NAME   20
#define WIFI_SSID     32
#define WIFI_PASSWORD 64
#define WIFI_IP       16
#define MQTT_URI      40
#define MQTT_USER     20
#define MQTT_PASS     30
#define MAX_TOPIC     50


/* ---- Comandos disponibles ---- */
#define CMD_SET_WIFI_SSID            "W_SSID"
#define CMD_SET_WIFI_PASS            "W_PASS"
#define CMD_SET_MQTT_URI             "M_URI"
#define CMD_SET_MQTT_USER            "M_USER"
#define CMD_SET_MQTT_PASS            "M_PASS"
#define CMD_SET_MQTT_TOPIC_DATA      "M_T_DATA"
#define CMD_SET_MQTT_TOPIC_ALERT     "M_T_ALERT"
#define CMD_SET_MQTT_TOPIC_MONITOR   "M_T_MONITOR"
#define CMD_SET_MQTT_TOPIC_SETTINGS  "M_T_SETTINGS"
#define CMD_SET_MQTT_TOPIC_HANDSHAKE "M_T_HANDSHAKE"
#define CMD_SET_DEVICE_NAME          "NAME"
#define CMD_SET_SAMPLE               "SAMPLE"
#define CMD_SET_ENERGY_MODE          "E_MODE"
#define CMD_SHOW_CONFIG              "SHOW"
#define CMD_EXIT                     "EXIT"
#define CMD_CHANGE                   "Y"
#define CMD_NOT_CHANGE               "N"
#define CMD_HELP                     "HELP"



/* ---- Funciones de la API ---- */
esp_err_t uart_init(void);
bool parse_mpack_settings(const char *msg, size_t len);
bool parse_mpack_handshake(const char *msg, size_t len);

void send_settings_task(void *);
void settings_init(void);
void settings_set_node_mac(const char* mac);
void settings_get_node_mac(char* dest, size_t dest_size);
void settings_get_node_device_name(char* dest, size_t dest_size);
uint32_t settings_get_node_sample_rate(void);
uint8_t settings_get_node_energy_mode(void);
void settings_get_wifi_ssid(uint8_t* dest, size_t dest_size);
uint8_t settings_get_wifi_ssid_len(void);
void settings_get_wifi_password(uint8_t* dest, size_t dest_size);
uint8_t settings_get_wifi_pass_len(void);
void settings_set_wifi_ip(const char* ip);
void settings_get_wifi_ip(char* dest, size_t dest_size);
void settings_get_mqtt_uri(char* dest, size_t dest_size);
void settings_get_mqtt_user(char* dest, size_t dest_size);
void settings_get_mqtt_password(char* dest, size_t dest_size);
void settings_get_mqtt_topic_data(char* dest, size_t dest_size);
void settings_get_mqtt_topic_alert(char* dest, size_t dest_size);
void settings_get_mqtt_topic_monitor(char* dest, size_t dest_size);
void settings_get_mqtt_topic_settings(char* dest, size_t dest_size);
void settings_get_mqtt_topic_handshake(char* dest, size_t dest_size);


#endif //SETTINGS_H