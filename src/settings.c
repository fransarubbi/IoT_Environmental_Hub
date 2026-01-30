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
#include "esp_pm.h"
#include "System/system.h"
#include "mpack.h"
#include "Fsm/fsm.h"
#include "Message/message.h"
#include "MQTT/mqtt.h"


settings_t settings;
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

void safe_string_copy(char* dest, const char* src, size_t size) {
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

void safe_strcpy(char *dest, const char *src, size_t dest_size) {
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

static void uart_send_text(const char *text) {
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

void settings_set_balance_epoch(const uint32_t balance) {
    lock();
    settings.network.balance_epoch = balance;
    unlock();
}

void settings_set_energy_mode(energy_mode_t mode) {
    lock();
    settings.node.energy_mode = mode;
    unlock();
}

void settings_empty_network(void) {
    lock();
    memset(&settings.network.id_network, 0, sizeof(settings.network.id_network));
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

void settings_get_network(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.network.id_network, dest_size);
    unlock();
}

void settings_get_network_id_edge(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.network.id_edge, dest_size);
    unlock();
}

uint32_t settings_get_balance_epoch(void) {
    lock();
    uint32_t val = settings.network.balance_epoch;
    unlock();
    return val;
}

uint32_t settings_get_node_sample_rate(void) {
    lock();
    uint32_t val = settings.node.sample_rate;
    unlock();
    return val;
}

energy_mode_t settings_get_node_energy_mode(void) {
    lock();
    energy_mode_t val = settings.node.energy_mode;
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

void settings_get_mqtt_topic_data(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_data, dest_size);
    unlock();
}

void settings_get_mqtt_topic_alert_air(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_alert_air, dest_size);
    unlock();
}

void settings_get_mqtt_topic_alert_temp(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_alert_temp, dest_size);
    unlock();
}

void settings_get_mqtt_topic_settings_ok(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_settings_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_hub_firmware_ok(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_hub_firmware_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_handshake_to_edge(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_handshake_to_edge, dest_size);
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

void settings_get_mqtt_topic_edge_state_balance(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_balance, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_state_normal(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_normal, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_state_safe(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_safe, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_phase(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_phase, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_handshake(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_handshake, dest_size);
    unlock();
}

void settings_get_mqtt_topic_heartbeat(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_heartbeat, dest_size);
    unlock();
}

void settings_get_mqtt_topic_new_firmware(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_new_firmware, dest_size);
    unlock();
}

void settings_get_mqtt_topic_new_settings(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_new_settings, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_setting_ok(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_setting_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_delete_hub(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_delete_hub, dest_size);
    unlock();
}

void settings_get_mqtt_topic_active_hub(char* dest, size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_active_hub, dest_size);
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
    uart_send_text("| NET <id_red>                 - Configura el id de la red a la que se conectara         |\r\n");
    uart_send_text("| EDGE <id_edge>               - Configura el id del edge al que se conectara            |\r\n");
    uart_send_text("| NAME <name>                  - Configura nombre del dispositivo                        |\r\n");
    uart_send_text("| SAMPLE <rate>                - Configura frecuencia de envio de datos                  |\r\n");
    uart_send_text("| ENERGY <energy>              - Configura modo de energia                               |\r\n");
    uart_send_text("| SHOW                         - Muestra configuracion actual                            |\r\n");
    uart_send_text("| EXIT                         - Salir                                                   |\r\n");
    uart_send_text("| HELP                         - Muestra mensaje de ayuda                                |\r\n");
    uart_send_text("| ====================================================================================== |\r\n");
    uart_send_text("| Info: SAMPLE setea cada cuantos minutos se envian los datos                            |\r\n");
    uart_send_text("| Info: ENERGY [0 = Bajo consumo, 1 = Balanceado, 2 = Performance]                       |\r\n");
    uart_send_text("| Info: Debe ingresar el prefijo mqtts:// obligatoriamente en la uri de MQTT             |\r\n");
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
    sprintf(temp_buffer, "| WiFi Contraseña:  %s\r\n", (const char*)settings.wifi.password);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| MQTT Uri:         %s\r\n", settings.mqtt.uri);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Red:              %s\r\n", settings.network.id_network);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Edge:             %s\r\n", settings.network.id_edge);
    uart_send_text(temp_buffer);
    sprintf(temp_buffer, "| Nombre Disp:      %s\r\n", settings.node.device_name);
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
        && strlen(settings.mqtt.uri) > 0 && strlen(settings.node.device_name) > 0
        && strlen(settings.network.id_network) > 0 && strlen(settings.network.id_edge) > 0
        && settings.node.sample_rate > 0 &&
        (settings.node.energy_mode == 0 || settings.node.energy_mode == 1 || settings.node.energy_mode == 2)) {
        return true;
        }
    return false;
}


/**
 * @brief Guardar en memoria no volatil (NVS) la configuracion.
 * @return esp_err_t  Devuelve ESP_OK cuando el almacenamiento fue correcto.
 */
esp_err_t setting_save_to_nvs(void) {
    nvs_handle_t h;
    esp_err_t ret = nvs_open("device_setting", NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_blob(h, "cfg_node", &settings.node, sizeof(settings.node));
    if (ret != ESP_OK) goto close_and_fail;

    ret = nvs_set_blob(h, "cfg_net", &settings.network, sizeof(settings.network));
    if (ret != ESP_OK) goto close_and_fail;

    ret = nvs_set_blob(h, "cfg_wifi", &settings.wifi, sizeof(settings.wifi));
    if (ret != ESP_OK) goto close_and_fail;

    ret = nvs_set_str(h, "cfg_mqtt_uri", settings.mqtt.uri);
    if (ret != ESP_OK) goto close_and_fail;

    ret = nvs_commit(h);
    nvs_close(h);
    return ret;

    close_and_fail:
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

    size = sizeof(settings.node);
    if (nvs_get_blob(h, "cfg_node", &settings.node, &size) != ESP_OK) {
        return false;
    }

    size = sizeof(settings.network);
    if (nvs_get_blob(h, "cfg_net", &settings.network, &size) != ESP_OK) {
        return false;
    }

    size = sizeof(settings.wifi);
    if (nvs_get_blob(h, "cfg_wifi", &settings.wifi, &size) != ESP_OK) {
        return false;
    }

    size = sizeof(settings.mqtt.uri);
    if (nvs_get_str(h, "cfg_mqtt_uri", settings.mqtt.uri, &size) != ESP_OK) {
        return false;
    }

    nvs_close(h);

    // Configurar CPU según el modo cargado
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

    if (strcmp(cmd, CMD_SET_NETWORK) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <id_network> -\r\n");
            return false;
        }
        safe_strcpy(settings.network.id_network, param, sizeof(settings.network.id_network));
        uart_send_text("- INFO: Red configurada correctamente -\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_EDGE) == 0) {
        if (parsed < 2) {
            uart_send_text("- ERROR: Falta parametro <id_edge> -\r\n");
            return false;
        }
        safe_strcpy(settings.network.id_edge, param, sizeof(settings.network.id_edge));
        uart_send_text("- INFO: Edge configurado correctamente -\r\n");
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
        switch (val) {
            case 0: settings.node.energy_mode = val;
                    set_cpu_frequency(MIN_FREQ, true);
                    uart_send_text("- INFO: Modo de energia configurado correctamente. LOW_CONSUMPTION -\r\n");
                    break;
            case 1: settings.node.energy_mode = val;
                    set_cpu_frequency(MID_FREQ, false);
                    uart_send_text("- INFO: Modo de energia configurado correctamente. BALANCED -\r\n");
                    break;
            case 2: settings.node.energy_mode = val;
                    set_cpu_frequency(MAX_FREQ, false);
                    uart_send_text("- INFO: Modo de energia configurado correctamente. PERFORMANCE -\r\n");
                    break;
            default: uart_send_text("- ERROR: Ingrese un modo de energia valido -\r\n");
                     break;
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
 * @brief Genera los tópicos MQTT dinámicamente según los parametros ya configurados.
 */
void create_mqtt_topics() {
    snprintf(settings.mqtt.topic_data, sizeof(settings.mqtt.topic_data),
        "iot/%s/hub/%s/data", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_alert_air, sizeof(settings.mqtt.topic_alert_air),
        "iot/%s/hub/%s/alert_air", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_alert_temp, sizeof(settings.mqtt.topic_alert_temp),
        "iot/%s/hub/%s/alert_temp", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_monitor, sizeof(settings.mqtt.topic_monitor),
        "iot/%s/hub/%s/monitor", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_settings_ok, sizeof(settings.mqtt.topic_settings_ok),
        "iot/%s/hub/%s/hub_setting_ok", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_hub_firmware_ok, sizeof(settings.mqtt.topic_hub_firmware_ok),
        "iot/%s/hub/%s/hub_firmware_ok", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_handshake_to_edge, sizeof(settings.mqtt.topic_handshake_to_edge),
        "iot/%s/hub/%s/balance_mode_handshake", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_settings, sizeof(settings.mqtt.topic_settings),
        "iot/%s/hub/%s/setting", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_edge_state_balance, sizeof(settings.mqtt.topic_edge_state_balance),
        "iot/%s/state/balance", settings.network.id_edge);

    snprintf(settings.mqtt.topic_edge_state_normal, sizeof(settings.mqtt.topic_edge_state_normal),
        "iot/%s/state/normal", settings.network.id_edge);

    snprintf(settings.mqtt.topic_edge_state_safe, sizeof(settings.mqtt.topic_edge_state_safe),
        "iot/%s/state/safe", settings.network.id_edge);

    snprintf(settings.mqtt.topic_edge_phase, sizeof(settings.mqtt.topic_edge_phase),
        "iot/%s/state/phase", settings.network.id_edge);

    snprintf(settings.mqtt.topic_edge_handshake, sizeof(settings.mqtt.topic_edge_handshake),
        "iot/%s/handshake", settings.network.id_edge);

    snprintf(settings.mqtt.topic_heartbeat, sizeof(settings.mqtt.topic_heartbeat),
        "iot/%s/heartbeat", settings.network.id_edge);

    snprintf(settings.mqtt.topic_new_firmware, sizeof(settings.mqtt.topic_new_firmware),
        "iot/%s/new_firmware", settings.network.id_network);

    snprintf(settings.mqtt.topic_new_settings, sizeof(settings.mqtt.topic_new_settings),
        "iot/%s/new_settings_to_hub", settings.network.id_network);

    snprintf(settings.mqtt.topic_edge_setting_ok, sizeof(settings.mqtt.topic_edge_setting_ok),
        "iot/%s/setting_ok", settings.network.id_network);

    snprintf(settings.mqtt.topic_delete_hub, sizeof(settings.mqtt.topic_delete_hub),
        "iot/%s/delete_hub", settings.network.id_network);

    snprintf(settings.mqtt.topic_active_hub, sizeof(settings.mqtt.topic_active_hub),
        "iot/%s/active_hub", settings.network.id_network);
}


/**
 * @brief Tarea de envio de mensaje de configuracion. Se autoelimina cuando recibe confirmacion.
 * @param pvParameter
 */
void send_settings_task(void *pvParameter) {
    mqtt_packet_t packet;
    uint32_t notification = 0;

    xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

    if (notification & NOTIFY_CMD_START) {
        while (1) {
            uint32_t rate = settings.node.sample_rate;
            if (rate == 0) rate = 1;
            const TickType_t loop_delay = pdMS_TO_TICKS(rate * 2 * 60000);

            if (generate_message_settings(&packet)) {
                if (xQueueSend(queues.settings_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                    ESP_LOGW("Settings", "Cola llena, descartando paquete");
                    free(packet.payload);
                }
            } else {
                ESP_LOGE("Settings", "Error RAM al generar paquete");
            }

            uint32_t kill_signal = 0;
            BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &kill_signal, loop_delay);

            if (result == pdTRUE) {
                if (kill_signal & NOTIFY_CMD_DESTROY) {
                    ESP_LOGW("Settings", "Orden de destrucción recibida. Eliminando tarea...");
                    break;
                }
            }
        }
    }
    vTaskDelete(NULL);
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
    create_mqtt_topics();
    show_config();
    return ESP_OK;
}