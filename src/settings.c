#include "Settings/settings.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include <errno.h>
#include <stdint.h>



static const char *TAG = "SETTINGS";



settings_t settings;     // Configuracion global del dispositivo
static char uart_buffer[SETTINGS_BUFFER_SIZE];   // Variables internas



/**
 * @brief Inicializa UART para el modo configuración.
 * @return esp_err_t  Devuelve ESP_OK si la inicializacion fue exitosa.
 */
esp_err_t uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = SETTINGS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(SETTINGS_UART_PORT_NUM, SETTINGS_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error instalando driver UART: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(SETTINGS_UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando UART: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}


/**
 * @brief Envía texto por UART.
 * @param text String para imprimir por UART.
 */
static void uart_send_text(const char *text) {
    uart_write_bytes(SETTINGS_UART_PORT_NUM, text, strlen(text));
}


/**
 * @brief Muestra la ayuda de comandos.
 */
static void show_help(void) {
    uart_send_text("\r\n\n");
    uart_send_text("| ====================== COMANDOS DISPONIBLES ====================== |\r\n");
    uart_send_text("| SET_SSID <ssid>           - Configura SSID WiFi                    |\r\n");
    uart_send_text("| SET_PASS <password>       - Configura password WiFi                |\r\n");
    uart_send_text("| SET_MQTT_HOST <host>      - Configura host MQTT                    |\r\n");
    uart_send_text("| SET_MQTT_PORT <port>      - Configura puerto MQTT                  |\r\n");
    uart_send_text("| SET_MQTT_USER <user>      - Configura usuario MQTT                 |\r\n");
    uart_send_text("| SET_MQTT_PASS <pass>      - Configura password MQTT                |\r\n");
    uart_send_text("| SET_DEVICE_NAME <name>    - Configura nombre del dispositivo       |\r\n");
    uart_send_text("| SET_SAMPLE <rate>         - Configura frecuencia de envio de datos |\r\n");
    uart_send_text("| SHOW                      - Muestra configuracion actual           |\r\n");
    uart_send_text("| EXIT                      - Salir                                  |\r\n");
    uart_send_text("| HELP                      - Muestra mensaje de ayuda               |\r\n");
    uart_send_text("| ================================================================== |\r\n");
    uart_send_text("| Info: SET_SAMPLE setea cada cuantos minutos se envian los datos    |\r\n");
    uart_send_text("| ================================================================== |\r\n\r\n");
}


/**
 * @brief Muestra la configuracion actual.
 */
void show_config(void) {
    char temp_buffer[128];

    uart_send_text("\r\n|========================================|\r\n");
    uart_send_text("|========= CONFIGURACION ACTUAL =========|\r\n");
    sprintf(temp_buffer,"| WiFi SSID:        %s\r\n", settings.wifi_ssid);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| WiFi Password:    %s\r\n", strlen(settings.wifi_password) > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Host:        %s\r\n", settings.mqtt_host);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Port:        %u\r\n", settings.mqtt_port);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT User:        %s\r\n", settings.mqtt_user);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Password:    %s\r\n", strlen(settings.mqtt_password) > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Device Name:      %s\r\n", settings.device_name);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Sample Rate:      %lu\r\n", settings.sample_rate);
    uart_send_text(temp_buffer);
    uart_send_text("|========================================|\r\n\r\n");
}


/**
 * @brief Muestra el menu principal de configuracion.
 */
static void show_menu(void) {
    uart_send_text("\r\n");
    uart_send_text("| ============================================================= |\r\n");
    uart_send_text("|                       MODO CONFIGURACION                      |\r\n");
    uart_send_text("| ============================================================= |\r\n");
    uart_send_text("| Use 'HELP' para ver comandos disponibles                      |\r\n");
    uart_send_text("| Use 'SHOW' para ver la configuracion actual                   |\r\n");
    uart_send_text("| Use 'EXIT' para salir                                         |\r\n");
    uart_send_text("| ============================================================= |\r\n");
    uart_send_text("| Info: Los cambios se guardan automaticamente. Para salir      |\r\n");
    uart_send_text("| del modo configuracion deben estar todos los campos completos |\r\n");
    uart_send_text("| ============================================================= |\r\n\r\n");
}


/**
 * @brief Procesa un comando recibido por parametro.
 * @param command String ingresado en UART que corresponde a un comando con su correspondiente parametro.
 * @return bool  Devuelve true unicamente cuando el comando ingresado fue EXIT. Esto finaliza la configuracion
 * cuando todos los campos estan completos. Si algun campo esta vacio, retorna false como los demas comandos.
 */
static bool process_command(const char *command) {
    char cmd[32];
    char param[SETTINGS_MAX_STRING_LEN];
    char *endptr;

    // Parsear comando y parámetro
    int parsed = sscanf(command, "%31s %99s", cmd, param);

    if (parsed < 1) {
        uart_send_text("- ERROR: Comando invalido. Use HELP para ver los comandos disponibles -\r\n");
        return false;
    }

    // Procesar comandos
    if (strcmp(cmd, CMD_HELP) == 0) {
        show_help();
    }
    else if (strcmp(cmd, CMD_SHOW_CONFIG) == 0) {
        show_config();
    }
    else if (strcmp(cmd, CMD_SET_WIFI_SSID) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <SSID> -\r\n");
        } else {
            strncpy(settings.wifi_ssid, param, SETTINGS_MAX_STRING_LEN - 1);
            settings.wifi_ssid[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("- INFO: SSID configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_WIFI_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
        } else {
            strncpy(settings.wifi_password, param, SETTINGS_MAX_STRING_LEN - 1);
            settings.wifi_password[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("- INFO: Password WiFi configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_MQTT_HOST) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <host> -\r\n");
        } else {
            strncpy(settings.mqtt_host, param, SETTINGS_MAX_STRING_LEN - 1);
            settings.mqtt_host[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("- INFO: MQTT Host configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_MQTT_PORT) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <port> -\r\n");
        } else {
            errno = 0;
            unsigned long val = strtoul(param, &endptr, 10);
            if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
                uart_send_text("- ERROR: Ingrese un numero de puerto valido -\r\n");
            }
            uint16_t port = (uint16_t)val;
            if (port > 0) {
                settings.mqtt_port = port;
                uart_send_text("- INFO: Puerto MQTT configurado correctamente -\r\n");
            } else {
                uart_send_text("- ERROR: Puerto invalido (1-65535)\r\n");
            }
        }
    }
    else if (strcmp(cmd, CMD_SET_MQTT_USER) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro usuario -\r\n");
        } else {
            strncpy(settings.mqtt_user, param, SETTINGS_BUFFER_SIZE - 1);
            settings.mqtt_user[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("- INFO: Usuario MQTT configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_MQTT_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
        } else {
            strncpy(settings.mqtt_password, param, SETTINGS_MAX_STRING_LEN - 1);
            settings.mqtt_password[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("- INFO: Password MQTT configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_DEVICE_NAME) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <name> -\r\n");
        } else {
            strncpy(settings.device_name, param, SETTINGS_MAX_STRING_LEN - 1);
            settings.device_name[SETTINGS_MAX_STRING_LEN - 1] = '\0';
            uart_send_text("INFO: Nombre del dispositivo configurado correctamente -\r\n");
        }
    }
    else if (strcmp(cmd, CMD_SET_SAMPLE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <rate> -\r\n");
        }
        else {
            errno = 0;
            unsigned long val = strtoul(param, &endptr, 10);
            if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
                uart_send_text("- ERROR: Ingrese un numero de muestreo valido -\r\n");
            }
            if (val > 0) {
                settings.sample_rate = val;
                uart_send_text("- INFO: Muestreo configurado correctamente -\r\n");
            } else {
                uart_send_text("- ERROR: Ingrese un numero de muestreo valido -\r\n");
            }
        }
    }
    else if (strcmp(cmd, CMD_EXIT) == 0 && setting_is_device_configured()) {
        esp_err_t ret = setting_save_to_nvs();
        if (ret == ESP_OK) {
            uart_send_text("- INFO: Configuracion guardada correctamente. Saliendo del modo configuracion -\r\n");
            return true;
        }
        uart_send_text("- ERROR: No se pudo guardar la configuracion -\r\n");
    }
    else if (strcmp(cmd, CMD_EXIT) == 0 && !setting_is_device_configured()) {
        uart_send_text("- INFO: Para salir del modo configuracion, deben estar todos los campos configurados -\r\n");
    }
    else {
        uart_send_text("- ERROR: Comando desconocido. Use HELP para ver comandos disponibles -\r\n");
    }

    return false;
}


/**
 * @brief Configuracion manual del sistema a traves de UART.
 * @return bool Devuelve true cuando el proceso termina con exito, sino retorna false.
 */
bool setting_mode_start(void) {
    char buffer_aux[SETTINGS_MAX_STRING_LEN];
    char c;
    bool flag = false;

    // Inicializar UART
    esp_err_t ret = uart_init();
    if (ret != ESP_OK) {
        return false;
    }

    show_menu();
    strcpy(buffer_aux, "config>  ");
    uart_send_text(buffer_aux);

    while (!flag) {
        int bytes = uart_read_bytes(SETTINGS_UART_PORT_NUM, (uint8_t*)&c, 1, portMAX_DELAY);

        if (bytes > 0) {
            if (c == '\n') {
                flag = process_command(uart_buffer);
                memset(uart_buffer, 0, sizeof(uart_buffer));
                memset(uart_buffer, 0, sizeof(uart_buffer));
                memset(buffer_aux, 0, sizeof(buffer_aux));
                strcpy(buffer_aux, "config>  ");
                show_menu();
                uart_send_text(buffer_aux);
            }
            else {
                char tmp[2] = {c,'\0'};   // convierte el char en un string de 1 char
                strcat(uart_buffer, tmp);
                strcat(buffer_aux, tmp);
                uart_send_text("\r");
                uart_send_text(buffer_aux);
            }
        }
    }

    uart_send_text("\r\n- INFO: Modo configuracion finalizado -\r\n");
    uart_send_text("------------ Iniciando aplicacion ------------\r\n\r\n");
    return true;
}


/**
 * @brief Guardar en memoria no volatil (NVS) la configuracion.
 * @return esp_err_t  Devuelve ESP_OK cuando el almacenamiento fue correcto.
 */
esp_err_t setting_save_to_nvs(void) {
    nvs_handle_t nvs_handle;

    esp_err_t ret = nvs_open("device_setting", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(nvs_handle, "wifi_ssid", settings.wifi_ssid);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_str(nvs_handle, "wifi_password", settings.wifi_password);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_str(nvs_handle, "mqtt_host", settings.mqtt_host);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_u16(nvs_handle, "mqtt_port", settings.mqtt_port);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_str(nvs_handle, "mqtt_user", settings.mqtt_user);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_str(nvs_handle, "mqtt_password", settings.mqtt_password);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_str(nvs_handle, "device_name", settings.device_name);
    if (ret != ESP_OK) goto exit;

    ret = nvs_set_u32(nvs_handle, "sample_rate", settings.sample_rate);
    if (ret != ESP_OK) goto exit;

    ret = nvs_commit(nvs_handle);

    exit:
        nvs_close(nvs_handle);
        return ret;
}


/**
 * @brief Cargar configuracion desde la memoria no volatil (NVS).
 * @return bool  Devuelve true cuando se ha cargado correctamente la configuracion.
 * Sino retorna false.
 */
bool setting_load_from_nvs(void) {
    nvs_handle_t nvs_handle;
    size_t required_size;

    esp_err_t ret = nvs_open("device_setting", NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) return false;

    required_size = sizeof(settings.wifi_ssid);
    ret = nvs_get_str(nvs_handle, "wifi_ssid", settings.wifi_ssid, &required_size);
    if (ret != ESP_OK) goto exit;

    required_size = sizeof(settings.wifi_password);
    ret = nvs_get_str(nvs_handle, "wifi_password", settings.wifi_password, &required_size);
    if (ret != ESP_OK) goto exit;

    required_size = sizeof(settings.mqtt_host);
    ret = nvs_get_str(nvs_handle, "mqtt_host", settings.mqtt_host, &required_size);
    if (ret != ESP_OK) goto exit;

    ret = nvs_get_u16(nvs_handle, "mqtt_port", &settings.mqtt_port);
    if (ret != ESP_OK) goto exit;

    required_size = sizeof(settings.mqtt_user);
    ret = nvs_get_str(nvs_handle, "mqtt_user", settings.mqtt_user, &required_size);
    if (ret != ESP_OK) goto exit;

    required_size = sizeof(settings.mqtt_password);
    ret = nvs_get_str(nvs_handle, "mqtt_password", settings.mqtt_password, &required_size);
    if (ret != ESP_OK) goto exit;

    required_size = sizeof(settings.device_name);
    ret = nvs_get_str(nvs_handle, "device_name", settings.device_name, &required_size);
    if (ret != ESP_OK) goto exit;

    ret = nvs_get_u32(nvs_handle, "sample_rate", &settings.sample_rate);
    if (ret == ESP_OK) {
        nvs_close(nvs_handle);
        return true;
    }

    exit:
        nvs_close(nvs_handle);
        return false;
}


/**
 * @brief Verifica que todos los campos esten completos.
 * @return bool  Devuelve true cuando la configuracion esta completa. Sino retorna false.
 */
bool setting_is_device_configured(void) {
    if (strlen(settings.wifi_ssid) > 0 && strlen(settings.wifi_password) > 0
        && strlen(settings.mqtt_host) > 0 && settings.mqtt_port != 0
        && strlen(settings.mqtt_user) > 0 && strlen(settings.mqtt_password) > 0
        && strlen(settings.device_name) > 0 && settings.sample_rate > 0) {
        return true;
    }
    return false;
}

