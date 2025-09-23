#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* ---- Configuraciones del sistema ---- */
#define SETTINGS_UART_PORT_NUM        UART_NUM_0
#define SETTINGS_UART_BAUD_RATE       115200
#define SETTINGS_MAX_STRING_LEN       100
#define SETTINGS_BUFFER_SIZE          256

/* ---- Comandos disponibles ---- */
#define CMD_SET_WIFI_SSID          "SET_SSID"
#define CMD_SET_WIFI_PASS          "SET_PASS"
#define CMD_SET_MQTT_HOST          "SET_MQTT_HOST"
#define CMD_SET_MQTT_PORT          "SET_MQTT_PORT"
#define CMD_SET_MQTT_USER          "SET_MQTT_USER"
#define CMD_SET_MQTT_PASS          "SET_MQTT_PASS"
#define CMD_SET_DEVICE_NAME        "SET_DEVICE_NAME"
#define CMD_SET_SAMPLE             "SET_SAMPLE"
#define CMD_SHOW_CONFIG            "SHOW"
#define CMD_EXIT                   "EXIT"
#define CMD_HELP                   "HELP"


/* ---- Estructura de configuracion ---- */
typedef struct {
    char wifi_ssid[SETTINGS_MAX_STRING_LEN];
    char wifi_password[SETTINGS_MAX_STRING_LEN];
    char mqtt_host[SETTINGS_MAX_STRING_LEN];
    uint16_t mqtt_port;
    char mqtt_user[SETTINGS_MAX_STRING_LEN];
    char mqtt_password[SETTINGS_MAX_STRING_LEN];
    char device_name[SETTINGS_MAX_STRING_LEN];
    uint32_t sample_rate;
} settings_t;


extern settings_t settings;


/* ---- Funciones ---- */
esp_err_t uart_init(void);
void show_config(void);
bool setting_mode_start(void);
bool setting_is_device_configured(void);
esp_err_t setting_save_to_nvs(void);
bool setting_load_from_nvs(void);
void show_startup_info(void);


#endif //SETTINGS_H