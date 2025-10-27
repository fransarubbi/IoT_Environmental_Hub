#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include "driver/uart.h"
#include "string.h"


/* ---- Configuraciones del sistema ---- */
#define SETTINGS_UART_PORT_NUM        UART_NUM_0
#define SETTINGS_UART_BAUD_RATE       115200
#define SETTINGS_MAX_STRING_LEN       30
#define SETTINGS_BUFFER_SIZE          256
#define AES_KEY_LEN                   32
#define MQTTS_PREFIX                  "mqtts://"
#define MQTTS_PREFIX_LEN              (sizeof(MQTTS_PREFIX) - 1)


/* ---- Comandos disponibles ---- */
#define CMD_SET_WIFI_SSID          "SET_WIFI_SSID"
#define CMD_SET_WIFI_PASS          "SET_WIFI_PASS"
#define CMD_SET_MQTT_URI           "SET_MQTT_URI"
#define CMD_SET_MQTT_USER          "SET_MQTT_USER"
#define CMD_SET_MQTT_PASS          "SET_MQTT_PASS"
#define CMD_SET_DEVICE_NAME        "SET_DEVICE_NAME"
#define CMD_SET_SAMPLE             "SET_SAMPLE"
#define CMD_SET_AES_KEY            "SET_AES_KEY"
#define CMD_SET_MQTT_TOPIC         "SET_MQTT_TOPIC"
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
memcpy(dest, src, _len);                 \
dest[_len] = '\0';                       \
} while(0)



/* ---- Estructura de configuracion ---- */
typedef struct {
    char mac_address[18];
    uint8_t wifi_ssid[32];
    uint8_t wifi_ssid_len;
    uint8_t wifi_password[64];
    uint8_t wifi_pass_len;
    char wifi_ip[SETTINGS_MAX_STRING_LEN];
    char mqtt_uri[SETTINGS_MAX_STRING_LEN];
    char mqtt_user[SETTINGS_MAX_STRING_LEN];
    char mqtt_password[SETTINGS_MAX_STRING_LEN];
    char device_name[SETTINGS_MAX_STRING_LEN];
    uint32_t sample_rate;
    char aes_key[AES_KEY_LEN];
    char topic_mqtt[40];
} settings_t;


extern settings_t settings;


/* ---- Funcion de la API ---- */
esp_err_t uart_init(void);


/**
 * @brief Envía texto por UART.
 * @param text String para imprimir por UART.
 */
static inline void uart_send_text(const char *text) {
    uart_write_bytes(SETTINGS_UART_PORT_NUM, text, strlen(text));
}


#endif //SETTINGS_H