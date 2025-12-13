#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include "driver/uart.h"
#include <string.h>


/* ---- Configuraciones del sistema ---- */
#define SETTINGS_UART_PORT_NUM        UART_NUM_0
#define SETTINGS_UART_BAUD_RATE       115200
#define SETTINGS_MAX_STRING_LEN       30
#define MAX_TOPIC                     50
#define MAX_FREQ                      240
#define MIN_FREQ                      80
#define SETTINGS_BUFFER_SIZE          256
#define AES_KEY_LEN                   32
#define MQTTS_PREFIX                  "mqtts://"
#define MQTTS_PREFIX_LEN              (sizeof(MQTTS_PREFIX) - 1)


/* ---- Comandos disponibles ---- */
#define CMD_SET_WIFI_SSID          "WIFI_SSID"
#define CMD_SET_WIFI_PASS          "WIFI_PASS"
#define CMD_SET_MQTT_URI           "MQTT_URI"
#define CMD_SET_MQTT_USER          "MQTT_USER"
#define CMD_SET_MQTT_PASS          "MQTT_PASS"
#define CMD_SET_MQTT_TOPIC_DATA    "MQTT_TOPIC_DATA"
#define CMD_SET_MQTT_TOPIC_ALERT   "MQTT_TOPIC_ALERT"
#define CMD_SET_MQTT_TOPIC_MONITOR "MQTT_TOPIC_MONITOR"
#define CMD_SET_MQTT_TOPIC_SETTINGS "MQTT_TOPIC_SETTINGS"
#define CMD_SET_MQTT_TOPIC_HANDSHAKE "MQTT_TOPIC_HANDSHAKE"
#define CMD_SET_DEVICE_NAME        "DEVICE_NAME"
#define CMD_SET_SAMPLE             "SAMPLE"
#define CMD_SET_ENERGY_MODE        "ENERGY_MODE"
#define CMD_SHOW_CONFIG            "SHOW"
#define CMD_EXIT                   "EXIT"
#define CMD_CHANGE                 "Y"
#define CMD_NOT_CHANGE             "N"
#define CMD_HELP                   "HELP"


/* ---- Macro para realizar copia de parametro a la variable ---- */
#define SAFE_STRCPY(dest, src) do {      \
size_t _len = strlen(src);               \
size_t _max = sizeof(dest) - 1;          \
if (_len > _max) _len = _max;            \
memset(dest, 0, sizeof(dest));           \
memcpy(dest, src, _len);                 \
dest[_len] = '\0';                       \
} while(0)


/* ---- Macro para copiar datos crudos (ideal para SSID/Password en structs de WiFi) ---- */
#define SAFE_RAW_COPY(dest, src, out_len) do { \
size_t _len = strlen(src);                     \
if (_len > sizeof(dest)) _len = sizeof(dest);  \
memset(dest, 0, sizeof(dest));                 \
memcpy(dest, src, _len);                       \
out_len = (uint8_t)_len;                       \
} while(0)



/* ---- Estructura de configuracion ---- */
typedef struct {
    struct {
        char mac_address[18];
        char device_name[SETTINGS_MAX_STRING_LEN];
        uint32_t sample_rate;
        uint8_t energy_mode;
    } node;
    struct {
        uint8_t ssid[32];
        uint8_t ssid_len;
        uint8_t password[64];
        uint8_t pass_len;
        char ip[SETTINGS_MAX_STRING_LEN];
    } wifi;
    struct {
        char uri[SETTINGS_MAX_STRING_LEN];
        char user[SETTINGS_MAX_STRING_LEN];
        char password[SETTINGS_MAX_STRING_LEN];
        char topic_data[MAX_TOPIC];
        char topic_alert[MAX_TOPIC];
        char topic_monitor[MAX_TOPIC];
        char topic_settings[MAX_TOPIC];
        char topic_handshake[MAX_TOPIC];
    } mqtt;
} settings_t;


extern settings_t settings;


/* ---- Funcion de la API ---- */
esp_err_t uart_init(void);
void process_json_settings(const char *, int);
void process_json_handshake(const char *, int);
void send_settings_task(void *);



#endif //SETTINGS_H