#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "Setting/settings.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include "nvs_flash.h"
#include "cJSON.h"
#include "esp_pm.h"
#include "Data/data.h"
#include "Time/time.h"
#include "System/system.h"


static const char *TAG = "SETTINGS";
settings_t settings;     // Configuracion global del dispositivo
static char uart_buffer[SETTINGS_BUFFER_SIZE];   // Variables internas
static TimerHandle_t one_shot_timer = NULL;



/**
 * @brief Envía texto por UART.
 * @param text String para imprimir por UART.
 */
static inline void uart_send_text(const char *text) {
    uart_write_bytes(SETTINGS_UART_PORT_NUM, text, strlen(text));
}


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
 * @brief Configura el modo de operacion del microcontrolador
 * @param mhz Frecuencia de la CPU
 * @param flag Flag para permitir que entre en sueño ligero en idle
 * @return ESP_OK en caso de exito, otro en caso de fallo
 */
static esp_err_t set_cpu_frequency(int mhz, bool flag) {
    esp_pm_config_t pm_config = {
        .max_freq_mhz = mhz,
        .min_freq_mhz = mhz,
        .light_sleep_enable = flag     // true para que duerma en idle (ahorra más bateria)
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    return ret;
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
    uart_send_text("| ============================================================================================= |\r\n");
    uart_send_text("| --------------------- Puede usar mayusculas o minusculas, es indistinto! -------------------- |\r\n");
    uart_send_text("| ==================================== COMANDOS DISPONIBLES =================================== |\r\n");
    uart_send_text("| WIFI_SSID <ssid>                    - Configura SSID WiFi                                     |\r\n");
    uart_send_text("| WIFI_PASS <password>                - Configura password WiFi                                 |\r\n");
    uart_send_text("| MQTT_URI <uri>                      - Configura uri MQTT                                      |\r\n");
    uart_send_text("| MQTT_USER <user>                    - Configura usuario MQTT                                  |\r\n");
    uart_send_text("| MQTT_PASS <pass>                    - Configura password MQTT                                 |\r\n");
    uart_send_text("| MQTT_TOPIC_DATA <topic>             - Configura el topico de publicacion de datos             |\r\n");
    uart_send_text("| MQTT_TOPIC_ALERT <topic>            - Configura el topico de publicacion de alertas           |\r\n");
    uart_send_text("| MQTT_TOPIC_MONITOR <topic>          - Configura el topico de publicacion de monitoreo         |\r\n");
    uart_send_text("| MQTT_TOPIC_SETTINGS <topic>         - Configura el topico de pub/esc de la configuracion      |\r\n");
    uart_send_text("| MQTT_TOPIC_HANDSHAKE <topic>        - Configura el topico de escucha de confirmaciones        |\r\n");
    uart_send_text("| DEVICE_NAME <name>                  - Configura nombre del dispositivo                        |\r\n");
    uart_send_text("| SAMPLE <rate>                       - Configura frecuencia de envio de datos                  |\r\n");
    uart_send_text("| ENERGY_MODE <energy>                - Configura modo de energia                               |\r\n");
    uart_send_text("| SHOW                                - Muestra configuracion actual                            |\r\n");
    uart_send_text("| EXIT                                - Salir                                                   |\r\n");
    uart_send_text("| HELP                                - Muestra mensaje de ayuda                                |\r\n");
    uart_send_text("| ============================================================================================= |\r\n");
    uart_send_text("| Info: SET_SAMPLE setea cada cuantos minutos se envian los datos                               |\r\n");
    uart_send_text("| Info: SET_ENERGY_MODE [0 = Bajo consumo, 1 = Alto consumo]                                    |\r\n");
    uart_send_text("| Info: MQTT_TOPIC_SETTINGS sirve como topico de cliente y servidor en simultaneo               |\r\n");
    uart_send_text("| Info: Debe ingresar mqtts:// obligatoriamente en la uri de MQTT                               |\r\n");
    uart_send_text("| ============================================================================================= |\r\n\r\n");
}


/**
 * @brief Muestra la configuracion actual.
 */
void show_config(void) {
    char temp_buffer[200];

    uart_send_text("\r\n|============================================|\r\n");
    uart_send_text("|=========== CONFIGURACION ACTUAL ===========|\r\n");
    sprintf(temp_buffer, "| WiFi SSID:        %s\r\n", (const char*)settings.wifi.ssid);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| WiFi Password:    %s\r\n", settings.wifi.pass_len > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Uri:         %s\r\n", settings.mqtt.uri);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT User:        %s\r\n", settings.mqtt.user);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Password:    %s\r\n", strlen(settings.mqtt.password) > 0 ? "**configurado**" : "no configurado");
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico Datos:   %s\r\n", settings.mqtt.topic_data);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico Alerta:  %s\r\n", settings.mqtt.topic_alert);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico Monitor: %s\r\n", settings.mqtt.topic_monitor);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico Configuracion: %s\r\n", settings.mqtt.topic_settings);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Topico Handshake: %s\r\n", settings.mqtt.topic_handshake);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Device Name:      %s\r\n", settings.node.device_name);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Sample Rate:      %lu\r\n", settings.node.sample_rate);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Modo Energia:     %u\r\n", settings.node.energy_mode);
    uart_send_text(temp_buffer);
    uart_send_text("|============================================|\r\n\r\n");
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
    if (settings.wifi.ssid_len > 0 && settings.wifi.pass_len > 0
        && strlen(settings.mqtt.uri) > 0 && strlen(settings.mqtt.user) > 0
        && strlen(settings.mqtt.password) > 0 && strlen(settings.node.device_name) > 0
        && settings.node.sample_rate > 0 && strlen(settings.mqtt.topic_data) > 0
        && strlen(settings.mqtt.topic_alert) > 0 && strlen(settings.mqtt.topic_monitor) > 0
        && (settings.node.energy_mode == 0 || settings.node.energy_mode == 1)
        && strlen(settings.mqtt.topic_settings) > 0 && strlen(settings.mqtt.topic_handshake) > 0) {
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

    ret = nvs_set_blob(h, "config", &settings, sizeof(settings_t));

    if (ret == ESP_OK) ret = nvs_commit(h);

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

    size_t size = 0;
    ret = nvs_get_blob(h, "config", NULL, &size); // Obtenemos solo el tamaño primero

    if (ret != ESP_OK || size != sizeof(settings_t)) {  // Si no existe la configuración o el tamaño de la estructura cambió, no leer nada
        nvs_close(h);
        return false;
    }

    // Leemos la estructura completa
    ret = nvs_get_blob(h, "config", &settings, &size);
    nvs_close(h);

    if (ret != ESP_OK) return false;

    if (settings.node.energy_mode) {
        set_cpu_frequency(MAX_FREQ, false);
    } else {
        set_cpu_frequency(MIN_FREQ, true);
    }

    return true;
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
        SAFE_STRCPY(settings.wifi.ssid, param);
        settings.wifi.ssid_len = strlen((char *)settings.wifi.ssid);
        uart_send_text("- INFO: SSID configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_WIFI_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.wifi.password, param);
        settings.wifi.pass_len = strlen((char *)settings.wifi.password);
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
        SAFE_STRCPY(settings.mqtt.uri, param);
        uart_send_text("- INFO: MQTT uri configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_USER) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro usuario -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.user, param);
        uart_send_text("- INFO: Usuario MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.password, param);
        uart_send_text("- INFO: Password MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_DATA) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.topic_data, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_ALERT) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.topic_alert, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_MONITOR) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.topic_monitor, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_SETTINGS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.topic_settings, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_HANDSHAKE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.mqtt.topic_handshake, param);
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_DEVICE_NAME) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <name> -\r\n");
            return false;
        }
        SAFE_STRCPY(settings.node.device_name, param);
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
            settings.node.sample_rate = val;
            uart_send_text("- INFO: Muestreo configurado correctamente -\r\n");
        } else {
            uart_send_text("- ERROR: Ingrese un numero de muestreo valido -\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_ENERGY_MODE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <energy> -\r\n");
            return false;
        }
        errno = 0;
        unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE)) {
            uart_send_text("- ERROR: Ingrese un modo de energia valido -\r\n");
        }
        if (val == 0 || val == 1) {
            settings.node.energy_mode = val;
            if (val) set_cpu_frequency(MAX_FREQ, false);
            else set_cpu_frequency(MIN_FREQ, true);
            uart_send_text("- INFO: Modo de energia configurado correctamente -\r\n");
        } else {
            uart_send_text("- ERROR: Ingrese un modo de energia valido -\r\n");
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


static void generate_json_settings(char *output_buffer, size_t buffer_size) {
    char time[30];
    get_time(time);

    snprintf(output_buffer, buffer_size,
        "{\n"
        "  \"ID\": \"%s\",\n"
        "  \"destination_type\": SERVER,\n"
        "  \"destination_id\": SERVER0,\n"
        "  \"timestamp\": \"%s\",\n"
        "  \"wifi_ssid\": \"%s\",\n"
        "  \"wifi_password\": \"%s\",\n"
        "  \"mqtt_uri\": \"%s\",\n"
        "  \"mqtt_user\": \"%s\",\n"
        "  \"mqtt_pass\": \"%s\",\n"
        "  \"topic_data\": \"%s\",\n"
        "  \"topic_alert\": \"%s\",\n"
        "  \"topic_monitor\": \"%s\",\n"
        "  \"topic_settings\": \"%s\",\n"
        "  \"topic_handshake\": \"%s\",\n"
        "  \"device_name\": \"%s\",\n"
        "  \"sample\": %lu\n"
        "  \"energy_mode\": %u\n"
        "}",
        settings.node.mac_address,
        time,
        (char *)settings.wifi.ssid,
        (char *)settings.wifi.password,
        settings.mqtt.uri,
        settings.mqtt.user,
        settings.mqtt.password,
        settings.mqtt.topic_data,
        settings.mqtt.topic_alert,
        settings.mqtt.topic_monitor,
        settings.mqtt.topic_settings,
        settings.mqtt.topic_handshake,
        settings.node.device_name,
        settings.node.sample_rate,
        settings.node.energy_mode
    );
}


/**
 * @brief Procesar comando recibido por MQTT desde el broker para modificar la configuracion del sistema.
 *
 * @param data Mensaje JSON.
 * @param data_len Longitud del mensaje JSON.
 */
void process_json_settings(const char *data, int data_len) {

    char *json_str = malloc(data_len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "- ERROR: No hay memoria -");
        return;
    }
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "- ERROR: JSON invalido -");
        free(json_str);
        return;
    }

    bool is_valid_target = false;
    cJSON *obj_id = cJSON_GetObjectItem(root, "ID");
    cJSON *obj_dtype = cJSON_GetObjectItem(root, "destination_type");
    cJSON *obj_did = cJSON_GetObjectItem(root, "destination_id");

    // Logica de validacion
    if (cJSON_IsString(obj_id) && strcmp(obj_id->valuestring, "SERVER0") == 0) {
        if (cJSON_IsString(obj_dtype) && strcmp(obj_dtype->valuestring, "NODE") == 0) {
            if (cJSON_IsString(obj_did)) {
                // Verificamos si es para "all" o para este nodo
                if (strcmp(obj_did->valuestring, "all") == 0 ||
                    strcmp(obj_did->valuestring, settings.node.mac_address) == 0) {
                    is_valid_target = true;
                }
            }
        }
    }

    // Si la validacion fallo, salimos
    if (!is_valid_target) {
        ESP_LOGW(TAG, "- JSON ignorado: No es para este dispositivo o ID incorrecto -");
        cJSON_Delete(root);
        free(json_str);
        return;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        const char *key = item->string;

        if (cJSON_IsString(item)) {
            const char *value = item->valuestring;

            if (strcmp(key, "wifi_ssid") == 0) {
                SAFE_RAW_COPY(settings.wifi.ssid, value, settings.wifi.ssid_len);
            }
            else if (strcmp(key, "wifi_password") == 0) {
                SAFE_RAW_COPY(settings.wifi.password, value, settings.wifi.pass_len);
            }
            else if (strcmp(key, "mqtt_uri") == 0) {
                SAFE_STRCPY(settings.mqtt.uri, value);
            }
            else if (strcmp(key, "mqtt_user") == 0) {
                SAFE_STRCPY(settings.mqtt.user, value);
            }
            else if (strcmp(key, "mqtt_pass") == 0) {
                SAFE_STRCPY(settings.mqtt.password, value);
            }
            else if (strcmp(key, "topic_data") == 0) {
                SAFE_STRCPY(settings.mqtt.topic_data, value);
            }
            else if (strcmp(key, "topic_alert") == 0) {
                SAFE_STRCPY(settings.mqtt.topic_alert, value);
            }
            else if (strcmp(key, "topic_monitor") == 0) {
                SAFE_STRCPY(settings.mqtt.topic_monitor, value);
            }
            else if (strcmp(key, "device_name") == 0) {
                SAFE_STRCPY(settings.node.device_name, value);
            }
        }
        else if (cJSON_IsNumber(item)) {
            if (strcmp(key, "sample") == 0) {
                if (item->valueint > 0) {
                    settings.node.sample_rate = item->valueint;
                }
            }
            else if (strcmp(key, "energy_mode") == 0) {
                int mode = item->valueint;
                if (mode == 0 || mode == 1) {
                    settings.node.energy_mode = mode;
                    if (mode == 1) set_cpu_frequency(MAX_FREQ, false);
                    else set_cpu_frequency(MIN_FREQ, true);
                }
            }
        }
    }

    // Guardar cambios
    esp_err_t ret = setting_save_to_nvs();
    cJSON_Delete(root);
    free(json_str);
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


void process_json_handshake(const char *data, int data_len) {
    char *json_str = malloc(data_len + 1);
    if (!json_str) {
        ESP_LOGE(TAG, "- ERROR: No hay memoria -");
        return;
    }
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "- ERROR: JSON invalido -");
        free(json_str);
        return;
    }

    bool is_valid_target = false;
    cJSON *obj_id = cJSON_GetObjectItem(root, "ID");
    cJSON *obj_dtype = cJSON_GetObjectItem(root, "destination_type");
    cJSON *obj_did = cJSON_GetObjectItem(root, "destination_id");

    // Logica de validacion
    if (cJSON_IsString(obj_id) && strcmp(obj_id->valuestring, "SERVER0") == 0) {
        if (cJSON_IsString(obj_dtype) && strcmp(obj_dtype->valuestring, "NODE") == 0) {
            if (cJSON_IsString(obj_did)) {
                // Verificamos si es para este nodo
                if (strcmp(obj_did->valuestring, settings.node.mac_address) == 0) {
                    is_valid_target = true;
                }
            }
        }
    }

    // Si la validacion fallo, salimos
    if (!is_valid_target) {
        ESP_LOGW(TAG, "- JSON ignorado: No es para este dispositivo o ID incorrecto -");
        cJSON_Delete(root);
        free(json_str);
        return;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        const char *key = item->string;

        if (cJSON_IsString(item)) {
            const char *value = item->valuestring;

            if (strcmp(key, "setting_received") == 0) {
                if (strcmp(value, "true") == 0) {
                    if (task_handle.send_settings_handle != NULL) {
                        xTaskNotifyGive(task_handle.send_settings_handle);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
    free(json_str);
}


void send_settings_task(void *pvParameter) {
    const TickType_t loop_delay = pdMS_TO_TICKS(settings.node.sample_rate * 2 * MS_TO_MIN);

    while (1) {
        char *json = (char*)heap_caps_malloc(JSON_MAX, MALLOC_CAP_8BIT);
        if (json) {
            generate_json_settings(json, JSON_MAX);
            if (xQueueSend(queues.settings_buffer, &json, pdMS_TO_TICKS(100)) != pdTRUE) {
                free(json); // Si no se pudo encolar, liberamos memoria para evitar fugas
            }
        } else {
            ESP_LOGE(TAG, "- ERROR: No hay memoria para JSON -");
        }
        uint32_t flag = ulTaskNotifyTake(pdTRUE, loop_delay);
        if (flag > 0) vTaskDelete(NULL);
    }
}