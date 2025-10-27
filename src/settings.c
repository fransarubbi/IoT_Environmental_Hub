#include "Setting/settings.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include "nvs_flash.h"
#include "freertos/timers.h"


static const char *TAG = "SETTINGS";
settings_t settings;     // Configuracion global del dispositivo
static char uart_buffer[SETTINGS_BUFFER_SIZE];   // Variables internas
static TimerHandle_t one_shot_timer = NULL;



/**
 * @brief Convierte el string a mayusculas
 * @param str string que se quiere modificar
 */
static void to_uppercase(char *str) {
    for (uint8_t i = 0; str[i] != '\0'; i++) {
        str[i] = (char)toupper((unsigned char)str[i]);
    }
}


/**
 * @brief Callback que setea el flag en true cuando se alcanzo el tiempo.
 * @param xTimer variable timer configurada previamente.
 */
static void timeout_callback(TimerHandle_t xTimer) {
    bool *flag_ptr = (bool *)pvTimerGetTimerID(xTimer);
    *flag_ptr = true;
}


/**
 * @brief Inicializa el timer que determina el margen de tiempo valido para dar una respuesta en
 * la funcion setting_mode_change(). Son 20 segundos.
 * @param timer_flag flag que indica cuando el timer llego al valor seteado. Cuando se le asigna true,
 * se termino el tiempo.
 */
static void timeout_init(bool *timer_flag) {
    one_shot_timer = xTimerCreate(
        "OneShotTimer",
        pdMS_TO_TICKS(20000), // 20 segundos
        pdFALSE,             // Auto-reload = FALSE (una sola vez)
        (void *)timer_flag,
        timeout_callback
    );

    if (one_shot_timer != NULL) {
        xTimerStart(one_shot_timer, 0);
    }
}


/**
 * @brief Inicializa UART para el modo configuración.
 * @return esp_err_t  Devuelve ESP_OK si la inicializacion fue exitosa.
 */
static esp_err_t uart_config(void) {
    const uart_config_t uart_conf = {
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

    ret = uart_param_config(SETTINGS_UART_PORT_NUM, &uart_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error configurando UART: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}


/**
 * @brief Muestra la ayuda de comandos.
 */
static void show_help(void) {
    uart_send_text("\r\n\n");
    uart_send_text("| ================================================================== |\r\n");
    uart_send_text("| ------- Puede usar mayusculas o minusculas, es indistinto! ------- |\r\n");
    uart_send_text("| ====================== COMANDOS DISPONIBLES ====================== |\r\n");
    uart_send_text("| SET_WIFI_SSID <ssid>      - Configura SSID WiFi                    |\r\n");
    uart_send_text("| SET_WIFI_PASS <password>  - Configura password WiFi                |\r\n");
    uart_send_text("| SET_MQTT_URI <uri>        - Configura uri MQTT                     |\r\n");
    uart_send_text("| SET_MQTT_USER <user>      - Configura usuario MQTT                 |\r\n");
    uart_send_text("| SET_MQTT_PASS <pass>      - Configura password MQTT                |\r\n");
    uart_send_text("| SET_DEVICE_NAME <name>    - Configura nombre del dispositivo       |\r\n");
    uart_send_text("| SET_SAMPLE <rate>         - Configura frecuencia de envio de datos |\r\n");
    uart_send_text("| SET_AES_KEY <key>         - Configura clave de cifrado de AES-CTR  |\r\n");
    uart_send_text("| SET_MQTT_TOPIC <topic>    - Configura el topico de publicacion     |\r\n");
    uart_send_text("| SHOW                      - Muestra configuracion actual           |\r\n");
    uart_send_text("| EXIT                      - Salir                                  |\r\n");
    uart_send_text("| HELP                      - Muestra mensaje de ayuda               |\r\n");
    uart_send_text("| ================================================================== |\r\n");
    uart_send_text("| Info: SET_SAMPLE setea cada cuantos minutos se envian los datos    |\r\n");
    uart_send_text("| Info: Debe ingresar mqtts:// obligatoriamente en la uri de MQTT    |\r\n");
    uart_send_text("| ================================================================== |\r\n\r\n");
}


/**
 * @brief Muestra la configuracion actual.
 */
void show_config(void) {
    char temp_buffer[128];

    uart_send_text("\r\n|========================================|\r\n");
    uart_send_text("|========= CONFIGURACION ACTUAL =========|\r\n");
    sprintf(temp_buffer,"| WiFi SSID:        %s\r\n", (const char*)settings.wifi_ssid);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| WiFi Password:    %s\r\n", settings.wifi_pass_len > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Uri:         %s\r\n", settings.mqtt_uri);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT User:        %s\r\n", settings.mqtt_user);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Password:    %s\r\n", strlen(settings.mqtt_password) > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico:      %s\r\n", settings.topic_mqtt);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Device Name:      %s\r\n", settings.device_name);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Sample Rate:      %lu\r\n", settings.sample_rate);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer,"| AES Key:           %s\r\n", strlen(settings.aes_key) > 0 ? "**configurado**" : "no configurado");
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
 * @brief Muestra el menu de consulta para cambiar o no una configuracion existente.
 */
static void show_menu_change_settings(void) {
    uart_send_text("\r\n");
    uart_send_text("| ========================================================== |\r\n");
    uart_send_text("|      Se ha detectado una configuracion guardada en NVS     |\r\n");
    uart_send_text("|     ¿Desea cambiar algun atributo de la configuracion?     |\r\n");
    uart_send_text("|  Tiene 20 seg para responder. Por omision se considera 'n' |\r\n");
    uart_send_text("| ========================================================== |\r\n");
    uart_send_text(" > Ingrese y para cambiar la configuracion\r\n");
    uart_send_text(" > Ingrese n para usar la configuracion actual\r\n");
}


/**
 * @brief Verifica que todos los campos esten completos.
 * @return bool  Devuelve true cuando la configuracion esta completa. Sino retorna false.
 */
static bool setting_is_device_configured(void) {
    if (settings.wifi_ssid_len > 0 && settings.wifi_pass_len > 0
        && strlen(settings.mqtt_uri) > 0 && strlen(settings.mqtt_user) > 0
        && strlen(settings.mqtt_password) > 0 && strlen(settings.device_name) > 0
        && settings.sample_rate > 0 && strlen(settings.aes_key) > 0
        && strlen(settings.topic_mqtt) > 0) {
        return true;
        }
    return false;
}


/**
 * @brief Guardar en memoria no volatil (NVS) la configuracion.
 * @return esp_err_t  Devuelve ESP_OK cuando el almacenamiento fue correcto.
 */
static esp_err_t setting_save_to_nvs(void) {
    nvs_handle_t h;
    esp_err_t ret = nvs_open("device_setting", NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    if ((ret = nvs_set_blob(h, "wifi_ssid", settings.wifi_ssid, settings.wifi_ssid_len)) != ESP_OK) goto exit;
    if ((ret = nvs_set_blob(h, "wifi_password", settings.wifi_password, settings.wifi_pass_len)) != ESP_OK) goto exit;

    if ((ret = nvs_set_str(h, "mqtt_uri", settings.mqtt_uri)) != ESP_OK) goto exit;
    if ((ret = nvs_set_str(h, "mqtt_user", settings.mqtt_user)) != ESP_OK) goto exit;
    if ((ret = nvs_set_str(h, "mqtt_password", settings.mqtt_password)) != ESP_OK) goto exit;
    if ((ret = nvs_set_str(h, "mqtt_topic", settings.topic_mqtt)) != ESP_OK) goto exit;
    if ((ret = nvs_set_str(h, "device_name", settings.device_name)) != ESP_OK) goto exit;

    if ((ret = nvs_set_u32(h, "sample_rate", settings.sample_rate)) != ESP_OK) goto exit;

    if ((ret = nvs_set_blob(h, "aes_key", settings.aes_key, AES_KEY_LEN)) != ESP_OK) goto exit;

    ret = nvs_commit(h);

    exit:
        nvs_close(h);
    return ret;
}


/**
 * @brief Cargar configuracion desde la memoria no volatil (NVS).
 * @return bool  Devuelve true cuando se ha cargado correctamente la configuracion.
 * Sino retorna false.
 */
static bool setting_load_from_nvs(void) {
    nvs_handle_t h;
    esp_err_t ret = nvs_open("device_setting", NVS_READONLY, &h);
    if (ret != ESP_OK) return false;

    size_t size;

    size = sizeof(settings.wifi_ssid);
    ret = nvs_get_blob(h, "wifi_ssid", settings.wifi_ssid, &size);
    if (ret != ESP_OK || size == 0) goto exit;
    settings.wifi_ssid_len = size;

    size = sizeof(settings.wifi_password);
    ret = nvs_get_blob(h, "wifi_password", settings.wifi_password, &size);
    if (ret != ESP_OK || size == 0) goto exit;
    settings.wifi_pass_len = size;

    size = sizeof(settings.mqtt_uri);
    if (nvs_get_str(h, "mqtt_uri", settings.mqtt_uri, &size) != ESP_OK) goto exit;

    size = sizeof(settings.mqtt_user);
    if (nvs_get_str(h, "mqtt_user", settings.mqtt_user, &size) != ESP_OK) goto exit;

    size = sizeof(settings.mqtt_password);
    if (nvs_get_str(h, "mqtt_password", settings.mqtt_password, &size) != ESP_OK) goto exit;

    size = sizeof(settings.topic_mqtt);
    if (nvs_get_str(h, "mqtt_topic", settings.topic_mqtt, &size) != ESP_OK) goto exit;

    size = sizeof(settings.device_name);
    if (nvs_get_str(h, "device_name", settings.device_name, &size) != ESP_OK) goto exit;

    if (nvs_get_u32(h, "sample_rate", &settings.sample_rate) != ESP_OK) goto exit;

    size = sizeof(settings.aes_key);
    ret = nvs_get_blob(h, "aes_key", settings.aes_key, &size);
    if (ret != ESP_OK || size != AES_KEY_LEN) goto exit;

    nvs_close(h);
    return true;

    exit:
        nvs_close(h);
    return false;
}


/**
 * @brief Procesa un comando recibido por parametro.
 * @param command String ingresado en UART que corresponde a un comando con su correspondiente parametro.
 * @return bool  Devuelve true unicamente cuando el comando ingresado fue EXIT. Esto finaliza la configuracion
 * cuando todos los campos estan completos. Si algun campo esta vacio, retorna false como los demas comandos.
 */
static bool process_command(const char *command) {
    char cmd[32];
    char param[100];
    char *endptr;

    // Parsear comando y parámetro
    int parsed = sscanf(command, "%31s %99[^\n]", cmd, param);

    if (parsed < 1) {
        uart_send_text("- ERROR: Comando invalido. Use HELP para ver los comandos disponibles -\r\n");
        return false;
    }

    to_uppercase(cmd);

    // Procesar comandos
    if (strcmp(cmd, CMD_HELP) == 0) {
        show_help();
        return false;
    }

    if (strcmp(cmd, CMD_SHOW_CONFIG) == 0) {
        show_config();
        return false;
    }

    if (strcmp(cmd, CMD_SET_WIFI_SSID) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <SSID> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.wifi_ssid, param);
        settings.wifi_ssid_len = strlen((char *)settings.wifi_ssid);
        uart_send_text("- INFO: SSID configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_WIFI_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.wifi_password, param);
        settings.wifi_pass_len = strlen((char *)settings.wifi_password);
        uart_send_text("- INFO: Password WiFi configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_URI) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <uri> -\r\n");
            return false;
        }
        // Verificar el prefijo de seguridad MQTTS
        if (strncmp(param, MQTTS_PREFIX, MQTTS_PREFIX_LEN) != 0) {
            uart_send_text("- ERROR: MQTT uri erroneo. Falta mqtts:// como primer parametro -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt_uri, param);
        uart_send_text("- INFO: MQTT uri configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_USER) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro usuario -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt_user, param);
        uart_send_text("- INFO: Usuario MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt_password, param);
        uart_send_text("- INFO: Password MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.topic_mqtt, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_DEVICE_NAME) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <name> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.device_name, param);
        uart_send_text("INFO: Nombre del dispositivo configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_SAMPLE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <rate> -\r\n");
            return false;
        }
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
        return false;
    }

    if (strcmp(cmd, CMD_SET_AES_KEY) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <key> -\r\n");
            return false;
        }
        if (strlen(param) == AES_KEY_LEN) {
            memcpy(settings.aes_key, param, AES_KEY_LEN);
            uart_send_text("- INFO: Clave AES configurada correctamente -\r\n");
        }
        else {
            uart_send_text("- ERROR: La clave debe tener 32 caracteres obligatoriamente -\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_EXIT) == 0 && setting_is_device_configured()) {
        esp_err_t ret = setting_save_to_nvs();
        if (ret == ESP_OK) {
            uart_send_text("\n- INFO: Configuracion guardada correctamente. Saliendo del modo configuracion -\r\n");
            return true;
        }
        uart_send_text("- ERROR: No se pudo guardar la configuracion -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_EXIT) == 0 && !setting_is_device_configured()) {
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
static bool setting_mode_start(void) {
    char buffer_aux[100];
    char c;
    bool flag = false;

    show_menu();
    strcpy(buffer_aux, "config>  ");
    uart_send_text(buffer_aux);

    while (1) {
        int bytes = uart_read_bytes(SETTINGS_UART_PORT_NUM, (uint8_t*)&c, 1, portMAX_DELAY);

        if (bytes > 0) {
            if (c == '\n') {
                uart_send_text("\r\n");
                flag = process_command(uart_buffer);
                if (flag) break;
                memset(uart_buffer, 0, sizeof(uart_buffer));
                uart_send_text("\r\n");  // Nueva linea
                show_menu();
                uart_send_text("config>  ");
            }
            else if (c == '\r') {  // Ignorar carriage return si viene separado
                continue;
            }
            else {
                char tmp[2] = {c, '\0'};
                strcat(uart_buffer, tmp);
                uart_send_text(tmp);  // Solo imprime el caracter nuevo
            }
        }
    }
    return true;
}


/**
 * @brief Permite modificar las configuraciones preexistentes. Se debe responder en un lapso de
 * 20 segundos, sino se considera que no se desea alterar ninguna configuracion
 * @return bool Devuelve true cuando se ingresa 'y' porque se va a modificar, o false cuando se
 * ingresa 'n' o se alcanzo el tiempo maximo y no se va a modificar ningun parametro.
 */
static bool setting_mode_change(void) {
    char buffer_aux[100];
    char c, cmd[2];
    bool flag = false;

    timeout_init(&flag);
    show_menu_change_settings();
    strcpy(buffer_aux, "config>  ");
    uart_send_text(buffer_aux);

    while (1) {
        if (flag) {
            if (one_shot_timer != NULL) {
                xTimerStop(one_shot_timer, 0);
            }
            return false;
        }

        int bytes = uart_read_bytes(SETTINGS_UART_PORT_NUM, (uint8_t*)&c, 1, pdMS_TO_TICKS(100));

        if (bytes > 0) {
            if (c == '\n') {
                uart_send_text("\r\n");
                int parsed = sscanf(uart_buffer, "%c", cmd);
                if (parsed < 1) {
                    uart_send_text("- ERROR: Comando invalido. Ingrese y o n -\r\n");
                }
                else {
                    to_uppercase(cmd);
                    if (strcmp(cmd, CMD_CHANGE) == 0) {
                        memset(uart_buffer, 0, sizeof(uart_buffer));
                        return true;
                    }
                    if (strcmp(cmd, CMD_NOT_CHANGE) == 0) {
                        return false;
                    }
                    uart_send_text("- ERROR: Comando invalido. Ingrese y o n -\r\n");
                }
                memset(uart_buffer, 0, sizeof(uart_buffer));
                uart_send_text("\r\n");
                show_menu_change_settings();
                uart_send_text("config>  ");
            }
            else if (c == '\r') {
                continue;
            }
            else {
                char tmp[2] = {c, '\0'};
                strcat(uart_buffer, tmp);
                uart_send_text(tmp);
            }
        }
    }
}


/**
 * @brief Tarea de configuracion del sistema a traves de UART
 * @return esp_err_t Devuelve ESP_OK si la configuracion se pudo aplicar correctamente
 */
esp_err_t uart_init(void) {

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "- WARNING: Borrando NVS -");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "- INFO: NVS inicializado correctamente -");

    ret = uart_config();
    if (ret != ESP_OK) return ESP_FAIL;

    // Cargar configuracion si existe
    if (!setting_load_from_nvs()) {   // No existe
        memset(&settings, 0, sizeof(settings_t));
        bool flag = false;
        while (!flag) {
            flag = setting_mode_start();
        }
    }
    else {  // Si existe
        if (setting_mode_change()) {  // Cambiar parametros de la configuracion
            bool flag = false;
            while (!flag) {
                flag = setting_mode_start();
            }
        }
    }
    show_config();
    return ESP_OK;
}