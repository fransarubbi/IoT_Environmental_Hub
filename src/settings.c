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
#include "components/mpack/include/mpack.h"
#include "MQTT/mqtt.h"


static struct {
    struct {
        char mac_address[MAC];
        char device_name[DEVICE_NAME];
        uint32_t sample_rate;
        uint8_t energy_mode;
    } node;
    struct {
        uint8_t ssid[WIFI_SSID];
        uint8_t ssid_len;
        uint8_t password[WIFI_PASSWORD];
        uint8_t pass_len;
        char ip[WIFI_IP];
    } wifi;
    struct {
        char uri[MQTT_URI];
        char user[MQTT_USER];
        char password[MQTT_PASS];
        char topic_data[MAX_TOPIC];
        char topic_alert[MAX_TOPIC];
        char topic_monitor[MAX_TOPIC];
        char topic_settings[MAX_TOPIC];
        char topic_handshake[MAX_TOPIC];
    } mqtt;
} settings;


static const char *TAG = "Settings";
static char uart_buffer[SETTINGS_BUFFER_SIZE];
static TimerHandle_t one_shot_timer = NULL;
static SemaphoreHandle_t settings_mutex = NULL;


/* ---- Helpers ---- */
static void lock(void) {
    if (settings_mutex == NULL) {
        settings_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(settings_mutex, portMAX_DELAY);
}

static void unlock(void) {
    xSemaphoreGive(settings_mutex);
}

static void safe_string_copy(char* dest, const char* src, size_t size) {
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

static inline void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t src_len = strlen(src);
    size_t max_copy = dest_size - 1;
    size_t actual_copy_len = (src_len < max_copy) ? src_len : max_copy;
    memcpy(dest, src, actual_copy_len);
    dest[actual_copy_len] = '\0';
}

static inline void uart_send_text(const char *text) {
    uart_write_bytes(SETTINGS_UART_PORT_NUM, text, strlen(text));
}

void settings_init(void) {
    if (settings_mutex == NULL) {
        settings_mutex = xSemaphoreCreateMutex();
    }
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


/* ---- Setters ---- */
void settings_set_node_mac(const char* mac) {
    lock();
    safe_string_copy(settings.node.mac_address, mac, sizeof(settings.node.mac_address));
    unlock();
}

void settings_set_wifi_ip(const char* ip) {
    lock();
    safe_string_copy(settings.wifi.ip, ip, sizeof(settings.wifi.ip));
    unlock();
}


/* ---- Getters ---- */
void settings_get_node_mac(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.node.mac_address, dest_size);
    unlock();
}

void settings_get_node_device_name(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.node.device_name, dest_size);
    unlock();
}

uint32_t settings_get_node_sample_rate(void) {
    lock();
    uint32_t val = settings.node.sample_rate;
    unlock();
    return val;
}

uint8_t settings_get_node_energy_mode(void) {
    lock();
    uint8_t val = settings.node.energy_mode;
    unlock();
    return val;
}

void settings_get_wifi_ssid(uint8_t* dest, size_t dest_size) {
    lock();
    size_t current_len = settings.wifi.ssid_len;
    size_t to_copy = (current_len >= dest_size) ? (dest_size - 1) : current_len;
    memcpy(dest, settings.wifi.ssid, to_copy);
    dest[to_copy] = 0;
    unlock();
}

uint8_t settings_get_wifi_ssid_len(void) {
    lock();
    uint8_t val = settings.wifi.ssid_len;
    unlock();
    return val;
}

void settings_get_wifi_password(uint8_t* dest, size_t dest_size) {
    lock();
    size_t current_len = settings.wifi.pass_len;
    size_t to_copy = (current_len >= dest_size) ? (dest_size - 1) : current_len;
    memcpy(dest, settings.wifi.password, to_copy);
    dest[to_copy] = 0;
    unlock();
}

uint8_t settings_get_wifi_pass_len(void) {
    lock();
    uint8_t val = settings.wifi.pass_len;
    unlock();
    return val;
}

void settings_get_wifi_ip(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.wifi.ip, dest_size);
    unlock();
}

void settings_get_mqtt_uri(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.uri, dest_size);
    unlock();
}

void settings_get_mqtt_user(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.user, dest_size);
    unlock();
}

void settings_get_mqtt_password(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.password, dest_size);
    unlock();
}

void settings_get_mqtt_topic_data(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_data, dest_size);
    unlock();
}

void settings_get_mqtt_topic_alert(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_alert, dest_size);
    unlock();
}

void settings_get_mqtt_topic_monitor(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_monitor, dest_size);
    unlock();
}

void settings_get_mqtt_topic_settings(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_settings, dest_size);
    unlock();
}

void settings_get_mqtt_topic_handshake(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_handshake, dest_size);
    unlock();
}



/* ---- Timer ---- */
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
    uart_send_text("| ====================================================================================== |\r\n");
    uart_send_text("| ----------------- Puede usar mayusculas o minusculas, es indistinto! ----------------- |\r\n");
    uart_send_text("| ================================ COMANDOS DISPONIBLES ================================ |\r\n");
    uart_send_text("| W_SSID <ssid>                - Configura SSID WiFi                                     |\r\n");
    uart_send_text("| W_PASS <password>            - Configura password WiFi                                 |\r\n");
    uart_send_text("| M_URI <uri>                  - Configura uri MQTT                                      |\r\n");
    uart_send_text("| M_USER <user>                - Configura usuario MQTT                                  |\r\n");
    uart_send_text("| M_PASS <pass>                - Configura password MQTT                                 |\r\n");
    uart_send_text("| M_T_DATA <topic>             - Configura el topico de publicacion de datos             |\r\n");
    uart_send_text("| M_T_ALERT <topic>            - Configura el topico de publicacion de alertas           |\r\n");
    uart_send_text("| M_T_MONITOR <topic>          - Configura el topico de publicacion de monitoreo         |\r\n");
    uart_send_text("| M_T_SETTINGS <topic>         - Configura el topico de pub/esc de la configuracion      |\r\n");
    uart_send_text("| M_T_HANDSHAKE <topic>        - Configura el topico de escucha de confirmaciones        |\r\n");
    uart_send_text("| NAME <name>                  - Configura nombre del dispositivo                        |\r\n");
    uart_send_text("| SAMPLE <rate>                - Configura frecuencia de envio de datos                  |\r\n");
    uart_send_text("| E_MODE <energy>              - Configura modo de energia                               |\r\n");
    uart_send_text("| SHOW                         - Muestra configuracion actual                            |\r\n");
    uart_send_text("| EXIT                         - Salir                                                   |\r\n");
    uart_send_text("| HELP                         - Muestra mensaje de ayuda                                |\r\n");
    uart_send_text("| ====================================================================================== |\r\n");
    uart_send_text("| Info: SAMPLE setea cada cuantos minutos se envian los datos                            |\r\n");
    uart_send_text("| Info: E_MODE [0 = Bajo consumo, 1 = Alto consumo]                                      |\r\n");
    uart_send_text("| Info: M_T_SETTINGS sirve como topico de cliente y servidor en simultaneo               |\r\n");
    uart_send_text("| Info: Debe ingresar mqtts:// obligatoriamente en la uri de MQTT                        |\r\n");
    uart_send_text("| ====================================================================================== |\r\n\r\n");
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

    ret = nvs_set_blob(h, "config", &settings, sizeof(settings));

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

    if (ret != ESP_OK || size != sizeof(settings)) {  // Si no existe la configuración o el tamaño de la estructura cambió, no leer nada
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
        safe_strcpy((char*)settings.wifi.ssid, param, sizeof(settings.wifi.ssid));
        settings.wifi.ssid_len = strlen((char *)settings.wifi.ssid);
        uart_send_text("- INFO: SSID configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_WIFI_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        safe_strcpy((char*)settings.wifi.password, param, sizeof(settings.wifi.password));
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
        safe_strcpy(settings.mqtt.uri, param, sizeof(settings.mqtt.uri));
        uart_send_text("- INFO: MQTT uri configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_USER) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro usuario -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.user, param, sizeof(settings.mqtt.user));
        uart_send_text("- INFO: Usuario MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <password> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.password, param, sizeof(settings.mqtt.password));
        uart_send_text("- INFO: Password MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_DATA) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.topic_data, param, sizeof(settings.mqtt.topic_data));
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_ALERT) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.topic_alert, param, sizeof(settings.mqtt.topic_alert));
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_MONITOR) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.topic_monitor, param, sizeof(settings.mqtt.topic_monitor));
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_SETTINGS) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.topic_settings, param, sizeof(settings.mqtt.topic_settings));
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_TOPIC_HANDSHAKE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <topic> -\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.topic_handshake, param, sizeof(settings.mqtt.topic_handshake));
        uart_send_text("- INFO: Topico MQTT configurado correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_DEVICE_NAME) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <name> -\r\n");
            return false;
        }
        safe_strcpy(settings.node.device_name, param, sizeof(settings.node.device_name));
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


/**
 * @brief Genera un json con los datos de la configuracion del dispositivo
 * @param packet
 */
static bool serialize_mpack_settings(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_SETTINGS_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char time[TIME_MAX_LEN];
    get_time(time);

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    // El mapa tiene 17 elementos
    mpack_start_map(&writer, 17);

    mpack_write_cstr(&writer, "ID");                mpack_write_cstr(&writer, settings.node.mac_address);
    mpack_write_cstr(&writer, "destination_type");  mpack_write_cstr(&writer, "SERVER");
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "SERVER0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_cstr(&writer, time);
    mpack_write_cstr(&writer, "wifi_ssid");         mpack_write_str(&writer, (const char*)settings.wifi.ssid, strnlen((const char*)settings.wifi.ssid, sizeof(settings.wifi.ssid)));
    mpack_write_cstr(&writer, "wifi_password");     mpack_write_str(&writer, (const char*)settings.wifi.password, strnlen((const char*)settings.wifi.password, sizeof(settings.wifi.password)));
    mpack_write_cstr(&writer, "mqtt_uri");          mpack_write_cstr(&writer, settings.mqtt.uri);
    mpack_write_cstr(&writer, "mqtt_user");         mpack_write_cstr(&writer, settings.mqtt.user);
    mpack_write_cstr(&writer, "mqtt_pass");         mpack_write_cstr(&writer, settings.mqtt.password);
    mpack_write_cstr(&writer, "topic_data");        mpack_write_cstr(&writer, settings.mqtt.topic_data);
    mpack_write_cstr(&writer, "topic_alert");       mpack_write_cstr(&writer, settings.mqtt.topic_alert);
    mpack_write_cstr(&writer, "topic_monitor");     mpack_write_cstr(&writer, settings.mqtt.topic_monitor);
    mpack_write_cstr(&writer, "topic_settings");    mpack_write_cstr(&writer, settings.mqtt.topic_settings);
    mpack_write_cstr(&writer, "topic_handshake");   mpack_write_cstr(&writer, settings.mqtt.topic_handshake);
    mpack_write_cstr(&writer, "device_name");       mpack_write_cstr(&writer, settings.node.device_name);
    mpack_write_cstr(&writer, "sample");            mpack_write_u32(&writer, settings.node.sample_rate);
    mpack_write_cstr(&writer, "energy_mode");       mpack_write_u8(&writer, settings.node.energy_mode);

    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE(TAG, "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}


/**
 * @brief Procesar comando recibido por MQTT para modificar la configuracion del sistema.
 * @param data Mensaje mpack.
 * @param len Longitud del mensaje mpack.
 */
bool parse_mpack_settings(const char* data, size_t len) {
    uint8_t flags = 0x0;
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char val_buf[55];
    for (uint32_t i = 0; i < map_size; i++) {
        char key[55];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "ID") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (strcmp(val_buf, "SERVER0") == 0) flags |= FLAG_SERVER_VALID;
        }
        else if (strcmp(key, "destination_type") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x1 && strcmp(val_buf, "NODE") == 0) {
                flags |= FLAG_CLIENT_VALID;
            }
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x3) {
                if (strcmp(val_buf, settings.node.mac_address) == 0) flags |= FLAG_ITS_ME;
                if (strcmp(val_buf, "all") == 0) flags |= FLAG_ITS_ALL;
            }
        }
        else if (strcmp(key, "wifi_ssid") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_strcpy((char *)settings.wifi.ssid, val_buf, sizeof(settings.wifi.ssid));
            }
        }
        else if (strcmp(key, "wifi_password") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_strcpy((char *)settings.wifi.password, val_buf, sizeof(settings.wifi.password));
            }
        }
        else if (strcmp(key, "mqtt_uri") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.uri, val_buf, sizeof(settings.mqtt.uri));
            }
        }
        else if (strcmp(key, "mqtt_user") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.user, val_buf, sizeof(settings.mqtt.user));
            }
        }
        else if (strcmp(key, "mqtt_pass") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.password, val_buf, sizeof(settings.mqtt.password));
            }
        }
        else if (strcmp(key, "topic_data") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.topic_data, val_buf, sizeof(settings.mqtt.topic_data));
            }
        }
        else if (strcmp(key, "topic_alert") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.topic_alert, val_buf, sizeof(settings.mqtt.topic_alert));
            }
        }
        else if (strcmp(key, "topic_monitor") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.topic_monitor, val_buf, sizeof(settings.mqtt.topic_monitor));
            }
        }
        else if (strcmp(key, "topic_settings") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.topic_settings, val_buf, sizeof(settings.mqtt.topic_settings));
            }
        }
        else if (strcmp(key, "topic_handshake") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.topic_handshake, val_buf, sizeof(settings.mqtt.topic_handshake));
            }
        }
        else if (strcmp(key, "device_name") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7) {
                safe_string_copy(settings.node.device_name, val_buf, sizeof(settings.node.device_name));
            }
        }
        else if (strcmp(key, "sample") == 0) {
            uint32_t val = mpack_expect_u32(&reader);
            if (flags == 0x7 || flags == 0xB) {
                settings.node.sample_rate = val;
            }
        }
        else if (strcmp(key, "energy_mode") == 0) {
            uint8_t val = mpack_expect_u8(&reader);
            if (flags == 0x7 || flags == 0xB) {
                settings.node.energy_mode = val;
            }
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (mpack_reader_destroy(&reader) != mpack_ok) {
        return false;
    }

    esp_err_t ret = setting_save_to_nvs();
    if (ret != ESP_OK) {
        return false;
    }
    return true;
}


/**
 * @brief Procesar comando recibido por MQTT para confirmar recepcion de datos.
 * @param data Mensaje mpack recibido.
 * @param len Longitud del mensaje.
 */
bool parse_mpack_handshake(const char* data, size_t len) {
    uint8_t flags = 0x0;
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char key[32];
    char value[32];
    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "ID") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "SERVER0") == 0) {
                flags |= FLAG_SERVER_VALID;
            }
        }
        else if (strcmp(key, "destination_type") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if ((flags == FLAG_SERVER_VALID) && (strcmp(value, "NODE") == 0)) {
                flags |= FLAG_CLIENT_VALID;
            }
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if ((flags == 0x03) && (strcmp(value, settings.node.mac_address) == 0)) {
                flags |= FLAG_ITS_ME;
            }
        }
        else if (strcmp(key, "setting_received") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if ((flags == 0x07) && (strcmp(value, "true") == 0)) {
                if (task_handle.send_settings_handle != NULL) {
                    xTaskNotifyGive(task_handle.send_settings_handle);
                }
            }
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Tarea de envio de mensaje de configuracion. Se autoelimina cuando recibe confirmacion.
 * @param pvParameter
 */
void send_settings_task(void *pvParameter) {
    const TickType_t loop_delay = pdMS_TO_TICKS(settings.node.sample_rate * 2 * MS_TO_MIN);
    mqtt_packet_t packet;

    while (1) {
        if (serialize_mpack_settings(&packet)) {
            if (xQueueSend(queues.settings_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                free(packet.payload);
            }
        }
        else {
            ESP_LOGE(TAG, "- ERROR: Fallo al generar paquete (RAM) -");
        }
        uint32_t flag = ulTaskNotifyTake(pdTRUE, loop_delay);
        if (flag > 0) vTaskDelete(NULL);
    }
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

    settings_init();

    // Cargar configuracion si existe
    if (!setting_load_from_nvs()) {   // No existe
        memset(&settings, 0, sizeof(settings));
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