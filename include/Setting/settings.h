#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include "driver/uart.h"
#include <string.h>
#include <stdatomic.h>


/* ---- Configuraciones del sistema ---- */
#define SETTINGS_UART_PORT_NUM        UART_NUM_0
#define SETTINGS_UART_BAUD_RATE       115200
#define SETTINGS_BUFFER_SIZE          256
#define MQTTS_PREFIX                  "mqtts://"
#define MQTTS_PREFIX_LEN              (sizeof(MQTTS_PREFIX) - 1)
#define MAX_FREQ                      240
#define MID_FREQ                      160
#define MIN_FREQ                      80


/* ---- Macros de longitudes ---- */
#define MAC           18
#define DEVICE_NAME   20
#define ID_NETWORK    20
#define ID_EDGE       20
#define URL_HTTPS     60
#define WIFI_SSID     32
#define WIFI_PASSWORD 64
#define WIFI_IP       16
#define MQTT_URI      40
#define MQTT_USER     20
#define MQTT_PASS     30
#define MAX_TOPIC     73


/* ---- Comandos disponibles ---- */
#define CMD_SET_WIFI_SSID            "W_SSID"
#define CMD_SET_WIFI_PASS            "W_PASS"
#define CMD_SET_MQTT_URI             "M_URI"
#define CMD_SET_NETWORK              "NET"
#define CMD_SET_URL_HTTPS            "URL_BYPASS"
#define CMD_SET_EDGE                 "EDGE"
#define CMD_SET_DEVICE_NAME          "NAME"
#define CMD_SET_SAMPLE               "SAMPLE"
#define CMD_SET_ENERGY_MODE          "ENERGY"
#define CMD_DELETE_LINKAGE_FLAG      "DELETE_LINKAGE"
#define CMD_SHOW_CONFIG              "SHOW"
#define CMD_EXIT                     "EXIT"
#define CMD_CHANGE                   "Y"
#define CMD_NOT_CHANGE               "N"
#define CMD_HELP                     "HELP"


typedef enum {
    LOW_CONSUMPTION = 0,
    BALANCED = 1,
    PERFORMANCE = 2,
} energy_mode_t;


typedef enum {
    FIRMWARE_OK,
    HANDSHAKE,
    PING,
    QUEUE_EMPTY,
    LINKAGE_REQUEST
} topic_general;


typedef struct {
    struct {
        char mac_address[MAC];
        char device_name[DEVICE_NAME];
        atomic_uint_fast32_t sample_rate;
        _Atomic energy_mode_t energy_mode;
    } node;
    struct {
        char id_network[ID_NETWORK];
        char id_edge[ID_EDGE];
        atomic_uint_fast32_t balance_epoch;
        char url_https[URL_HTTPS];
        uint8_t linkage_flag;
    } network;
    struct {
        uint8_t ssid[WIFI_SSID];
        atomic_uint_fast8_t ssid_len;
        uint8_t password[WIFI_PASSWORD];
        atomic_uint_fast8_t pass_len;
        char ip[WIFI_IP];
    } wifi;
    struct {
        char uri[MQTT_URI];

        // Publica
        char topic_data[MAX_TOPIC];
        char topic_alert_air[MAX_TOPIC];
        char topic_alert_temp[MAX_TOPIC];
        char topic_monitor[MAX_TOPIC];
        char topic_settings[MAX_TOPIC];
        char topic_settings_ok[MAX_TOPIC];
        char topic_hub_firmware_ok[MAX_TOPIC];
        char topic_handshake_to_edge[MAX_TOPIC];
        char topic_ping[MAX_TOPIC];
        char topic_empty_queue[MAX_TOPIC];
        char topic_linkage_request[MAX_TOPIC];

        // Escucha
        char topic_edge_state_normal[MAX_TOPIC];
        char topic_edge_state_balance[MAX_TOPIC];
        char topic_edge_state_safe[MAX_TOPIC];
        char topic_edge_phase[MAX_TOPIC];
        char topic_edge_handshake[MAX_TOPIC];
        char topic_heartbeat[MAX_TOPIC];
        char topic_new_firmware[MAX_TOPIC];
        char topic_new_settings[MAX_TOPIC];
        char topic_edge_setting_ok[MAX_TOPIC];
        char topic_delete_hub[MAX_TOPIC];
        char topic_active_hub[MAX_TOPIC];
        char topic_linkage_ack[MAX_TOPIC];
    } mqtt;
} settings_t;


extern settings_t settings;


esp_err_t uart_init(void);
void safe_strcpy(char *dest, const char *src, size_t dest_size);
void safe_string_copy(char* dest, const char* src, size_t size);
esp_err_t setting_save_to_nvs(void);
void send_settings_task(void *);
void settings_init(void);

// Setters
void settings_set_node_mac(const char* mac);
void settings_set_energy_mode(energy_mode_t mode);
void settings_empty_network(void);
void settings_set_balance_epoch(uint32_t bal);
void settings_set_wifi_ip(const char* ip);
void settings_set_linkage_ok(void);

// Getters
void settings_get_node_mac(char* dest, size_t dest_size);
void settings_get_node_device_name(char* dest, size_t dest_size);
uint32_t settings_get_node_sample_rate(void);
energy_mode_t settings_get_node_energy_mode(void);
void settings_get_network(char* dest, size_t dest_size);
void settings_get_network_id_edge(char* dest, size_t dest_size);
uint8_t settings_get_network_linkage_flag(void);
uint32_t settings_get_balance_epoch(void);
void settings_get_url_https(char* dest, size_t dest_size);
void settings_get_wifi_ssid(uint8_t* dest, size_t dest_size);
uint8_t settings_get_wifi_ssid_len(void);
void settings_get_wifi_password(uint8_t* dest, size_t dest_size);
uint8_t settings_get_wifi_pass_len(void);
void settings_get_wifi_ip(char* dest, size_t dest_size);
void settings_get_mqtt_uri(char* dest, size_t dest_size);
void settings_get_mqtt_user(char* dest, size_t dest_size);
void settings_get_mqtt_password(char* dest, size_t dest_size);
void settings_get_mqtt_topic_data(char* dest, size_t dest_size);
void settings_get_mqtt_topic_alert_air(char* dest, size_t dest_size);
void settings_get_mqtt_topic_alert_temp(char* dest, size_t dest_size);
void settings_get_mqtt_topic_settings_ok(char* dest, size_t dest_size);
void settings_get_mqtt_topic_hub_firmware_ok(char* dest, size_t dest_size);
void settings_get_mqtt_topic_handshake_to_edge(char* dest, size_t dest_size);
void settings_get_mqtt_topic_monitor(char* dest, size_t dest_size);
void settings_get_mqtt_topic_settings(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_state_balance(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_state_normal(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_state_safe(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_phase(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_handshake(char* dest, size_t dest_size);
void settings_get_mqtt_topic_heartbeat(char* dest, size_t dest_size);
void settings_get_mqtt_topic_new_firmware(char* dest, size_t dest_size);
void settings_get_mqtt_topic_new_settings(char* dest, size_t dest_size);
void settings_get_mqtt_topic_edge_setting_ok(char* dest, size_t dest_size);
void settings_get_mqtt_topic_delete_hub(char* dest, size_t dest_size);
void settings_get_mqtt_topic_active_hub(char* dest, size_t dest_size);
void settings_get_mqtt_topic_ping(char* dest, size_t dest_size);
void settings_get_mqtt_topic_empty_queue(char* dest, size_t dest_size);
void settings_get_mqtt_topic_linkage_request(char* dest, size_t dest_size);
void settings_get_mqtt_topic_linkage_ack(char* dest, size_t dest_size);


#endif //SETTINGS_H