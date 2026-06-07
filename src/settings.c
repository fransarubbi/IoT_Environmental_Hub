#include "freertos/FreeRTOS.h"
#include "Setting/settings.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <driver/gpio.h>
#include <stdarg.h>
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
static SemaphoreHandle_t settings_mutex = NULL;
static uint16_t flags = 0;



// ---- Helpers ----
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
    const size_t src_len = strlen(src);
    const size_t max_copy = dest_size - 1;
    const size_t actual_copy_len = (src_len < max_copy) ? src_len : max_copy;
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


// Función auxiliar para imprimir filas alineadas perfectamente
static void uart_print_row(const char* color, const char* label, const char* format, ...) {
    char value_str[40];
    va_list args;
    va_start(args, format);
    vsnprintf(value_str, sizeof(value_str), format, args);
    va_end(args);

    char line_buf[150];
    // Inyectamos el color del borde (B_WHT), el color del texto (color) y el reseteo (T_RST)
    snprintf(line_buf, sizeof(line_buf), "%s│ %s%-19s %s│%s %-30s %s│\r\n",
             B_WHT, color, label, B_WHT, T_RST, value_str, B_WHT);
    uart_send_text(line_buf);
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

void settings_set_balance_epoch(const uint32_t bal) {
    atomic_store(&settings.network.balance_epoch, bal);
}

void settings_set_energy_mode(const energy_mode_t mode) {
    atomic_store(&settings.node.energy_mode, mode);
}

void settings_empty_network(void) {
    lock();
    memset(&settings.network.id_network, 0, sizeof(settings.network.id_network));
    unlock();
}

void settings_set_linkage_ok(void) {
    atomic_store(&settings.network.linkage_flag, 1);
}

void settings_set_message_id(const uint32_t bal) {
    atomic_store(&settings.network.message_id.id, bal);
}

void settings_set_message_id_sending(const bool flag) {
    atomic_store(&settings.network.message_id.sending, flag);
}

/* ---- Getters ---- */
void settings_get_node_mac(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.node.mac_address, dest_size);
    unlock();
}

void settings_get_node_device_name(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.node.device_name, dest_size);
    unlock();
}

void settings_get_network(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.network.id_network, dest_size);
    unlock();
}

void settings_get_network_id_edge(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.network.id_edge, dest_size);
    unlock();
}

uint8_t settings_get_network_linkage_flag(void) {
    const uint32_t val = atomic_load(&settings.network.linkage_flag);
    return val;
}

uint32_t settings_get_balance_epoch(void) {
    const uint32_t val = atomic_load(&settings.network.balance_epoch);
    return val;
}

uint32_t settings_get_message_id(void) {
    const uint32_t val = atomic_load(&settings.network.message_id.id);
    return val;
}

bool settings_get_message_id_sending(void) {
    const bool val = atomic_load(&settings.network.message_id.sending);
    return val;
}

void settings_get_url_https(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.network.url_https, dest_size);
    unlock();
}

uint32_t settings_get_node_sample_rate(void) {
    const uint32_t val = atomic_load(&settings.node.sample_rate);
    return val;
}

energy_mode_t settings_get_node_energy_mode(void) {
    const uint32_t val = atomic_load(&settings.node.energy_mode);
    return val;
}

uint32_t settings_get_node_timeout_heartbeat_balance_mode(void) {
    const uint32_t val = atomic_load(&settings.node.timeout_heartbeat_balance_mode);
    return val;
}

uint32_t settings_get_node_timeout_heartbeat_normal_mode(void) {
    const uint32_t val = atomic_load(&settings.node.timeout_heartbeat_normal_mode);
    return val;
}

uint32_t settings_get_node_timeout_heartbeat_safe_mode(void) {
    const uint32_t val = atomic_load(&settings.node.timeout_heartbeat_safe_mode);
    return val;
}

float settings_get_node_mq135_r0(void) {
    return settings.node.mq135_r0;
}

float settings_get_node_mq135_alpha_ema(void) {
    return settings.node.mq135_alpha_ema;
}

float settings_get_node_dht11_alpha_ema(void) {
    return settings.node.dht11_alpha_ema;
}

void settings_get_wifi_ssid(uint8_t* dest, const size_t dest_size) {
    lock();
    const size_t current_len = atomic_load(&settings.wifi.ssid_len);
    const size_t to_copy = (current_len >= dest_size) ? (dest_size - 1) : current_len;
    memcpy(dest, settings.wifi.ssid, to_copy);
    dest[to_copy] = 0;
    unlock();
}

uint8_t settings_get_wifi_ssid_len(void) {
    const uint8_t val = atomic_load(&settings.wifi.ssid_len);
    return val;
}

void settings_get_wifi_password(uint8_t* dest, const size_t dest_size) {
    lock();
    const size_t current_len = atomic_load(&settings.wifi.pass_len);
    const size_t to_copy = (current_len >= dest_size) ? (dest_size - 1) : current_len;
    memcpy(dest, settings.wifi.password, to_copy);
    dest[to_copy] = 0;
    unlock();
}

uint8_t settings_get_wifi_pass_len(void) {
    const uint8_t val = atomic_load(&settings.wifi.pass_len);
    return val;
}

void settings_get_wifi_ip(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.wifi.ip, dest_size);
    unlock();
}

void settings_get_mqtt_uri(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.uri, dest_size);
    unlock();
}

void settings_get_mqtt_topic_data(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_data, dest_size);
    unlock();
}

void settings_get_mqtt_topic_alert_air(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_alert_air, dest_size);
    unlock();
}

void settings_get_mqtt_topic_alert_temp(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_alert_temp, dest_size);
    unlock();
}

void settings_get_mqtt_topic_settings_ok(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_settings_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_hub_firmware_ok(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_hub_firmware_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_handshake_to_edge(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_handshake_to_edge, dest_size);
    unlock();
}

void settings_get_mqtt_topic_monitor(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_monitor, dest_size);
    unlock();
}

void settings_get_mqtt_topic_settings(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_settings, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_state_balance(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_balance, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_state_normal(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_normal, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_state_safe(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_state_safe, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_phase(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_phase, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_handshake(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_handshake, dest_size);
    unlock();
}

void settings_get_mqtt_topic_heartbeat(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_heartbeat, dest_size);
    unlock();
}

void settings_get_mqtt_topic_new_firmware(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_new_firmware, dest_size);
    unlock();
}

void settings_get_mqtt_topic_new_settings(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_new_settings, dest_size);
    unlock();
}

void settings_get_mqtt_topic_edge_setting_ok(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_edge_setting_ok, dest_size);
    unlock();
}

void settings_get_mqtt_topic_delete_hub(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_delete_hub, dest_size);
    unlock();
}

void settings_get_mqtt_topic_active_hub(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_active_hub, dest_size);
    unlock();
}

void settings_get_mqtt_topic_ping(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_ping, dest_size);
    unlock();
}

void settings_get_mqtt_topic_ping_ack(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_ping_ack, dest_size);
    unlock();
}

void settings_get_mqtt_topic_empty_queue(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_empty_queue, dest_size);
    unlock();
}

void settings_get_mqtt_topic_linkage_request(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_linkage_request, dest_size);
    unlock();
}

void settings_get_mqtt_topic_linkage_ack(char* dest, const size_t dest_size) {
    lock();
    safe_string_copy(dest, settings.mqtt.topic_linkage_ack, dest_size);
    unlock();
}



/**
 * @brief Configura el modo de operacion del microcontrolador
 * @param mhz Frecuencia de la CPU
 * @param flag Flag para permitir que entre en sueño ligero en idle
 * @return ESP_OK en caso de exito, otro en caso de fallo
 */
static esp_err_t set_cpu_frequency(const int mhz, const bool flag) {
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = mhz,
        .min_freq_mhz = mhz,
        .light_sleep_enable = flag     // true para que duerma en idle (ahorra más bateria)
    };
    const esp_err_t ret = esp_pm_configure(&pm_config);
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

    esp_err_t ret = uart_driver_install(
        SETTINGS_UART_PORT_NUM,
        SETTINGS_BUFFER_SIZE * 2,
        0,
        0,
        NULL,
        0
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: no se pudo instalar driver UART");
        return ret;
    }

    ret = uart_param_config(
        SETTINGS_UART_PORT_NUM,
        &uart_conf
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: no se pudo configurar UART");
        return ret;
    }

    ret = uart_set_pin(
        SETTINGS_UART_PORT_NUM,
        SETTINGS_UART_TX_PIN,
        SETTINGS_UART_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error: no se pudo configurar pines UART");
        return ret;
    }

    gpio_set_pull_mode(
        SETTINGS_UART_RX_PIN,
        GPIO_PULLUP_ONLY
    );

    return ESP_OK;
}


/**
 * @brief Muestra la ayuda de comandos.
 */
static void show_help(void) {
    /* El compilador unirá todas estas partes en una sola cadena estática hiper rápida */
    uart_send_text(
        "\r\n" B_WHT
        "┌──────────────────────────────────────────────────────────────────────────────────────────────┐\r\n"
        "│" C_MAG "                                   COMANDOS DISPONIBLES                                       " B_WHT "│\r\n"
        "├──────────────────────────────────────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_CYN "❖ CONFIGURACIÓN BÁSICA       " B_WHT "│                                                               │\r\n"
        "├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_CYN "WIFI-SSID <ssid>             " B_WHT "│" T_RST " Configura el SSID del WiFi                                    " B_WHT "│\r\n"
        "│ " C_CYN "WIFI-PASS <password>         " B_WHT "│" T_RST " Configura la contraseña del WiFi                              " B_WHT "│\r\n"
        "│ " C_CYN "MQTT-URI <uri>               " B_WHT "│" T_RST " Configura la URI de MQTT (mqtts)                              " B_WHT "│\r\n"
        "│ " C_CYN "NETWORK <id_red>             " B_WHT "│" T_RST " Configura el ID de la red a la que se conectará               " B_WHT "│\r\n"
        "│ " C_CYN "EDGE-ID <id_edge>            " B_WHT "│" T_RST " Configura el ID del Edge al que se conectará                  " B_WHT "│\r\n"
        "│ " C_CYN "URL-BYPASS <url>             " B_WHT "│" T_RST " Configura la URL para conexión Bypass (https)                 " B_WHT "│\r\n"
        "│ " C_CYN "NAME-DEVICE <name>           " B_WHT "│" T_RST " Configura el nombre del dispositivo                           " B_WHT "│\r\n"
        "│ " C_CYN "SAMPLE-TIME <time>           " B_WHT "│" T_RST " Configura la frecuencia de envío de datos (min)               " B_WHT "│\r\n"
        "│ " C_CYN "ENERGY-MODE <energy>         " B_WHT "│" T_RST " Modo de energía [0 = Ahorro | 1 = Medio | 2 = Max]            " B_WHT "│\r\n"
        "│ " C_CYN "DELETE-LINKAGE               " B_WHT "│" T_RST " Elimina el flag de linkage para una nueva conexión            " B_WHT "│\r\n"
        "│ " C_CYN "HEARTBEAT-BALANCE <time>     " B_WHT "│" T_RST " Latidos recibidos en estado Balance (seg)                     " B_WHT "│\r\n"
        "│ " C_CYN "HEARTBEAT-NORMAL <time>      " B_WHT "│" T_RST " Latidos recibidos en estado Normal (seg)                      " B_WHT "│\r\n"
        "│ " C_CYN "HEARTBEAT-SAFE <time>        " B_WHT "│" T_RST " Latidos recibidos en estado Safe (seg)                        " B_WHT "│\r\n"
        "├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_YEL "❖ CALIBRACIÓN DE SENSORES    " B_WHT "│                                                               │\r\n"
        "├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_YEL "MQ135-R0 <resistance>        " B_WHT "│" T_RST " Configura la resistencia (kΩ) del sensor MQ135                " B_WHT "│\r\n"
        "│ " C_YEL "EMA-ALPHA-MQ135 <alpha>      " B_WHT "│" T_RST " Configura el parámetro Alpha del filtro EMA del MQ135         " B_WHT "│\r\n"
        "│ " C_YEL "EMA-ALPHA-DHT11 <alpha>      " B_WHT "│" T_RST " Configura el parámetro Alpha del filtro EMA del DHT11         " B_WHT "│\r\n"
        "├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_GRN "❖ INTERFAZ                   " B_WHT "│                                                               │\r\n"
        "├──────────────────────────────┼───────────────────────────────────────────────────────────────┤\r\n"
        "│ " C_GRN "SHOW                         " B_WHT "│" T_RST " Muestra la configuración actual                               " B_WHT "│\r\n"
        "│ " C_GRN "HELP                         " B_WHT "│" T_RST " Muestra este mensaje de ayuda                                 " B_WHT "│\r\n"
        "│ " C_GRN "EXIT                         " B_WHT "│" T_RST " Salir del modo configuración                                  " B_WHT "│\r\n"
        "└──────────────────────────────┴───────────────────────────────────────────────────────────────┘\r\n"
        T_RST "\r\n" // Es crítico el reseteo final para no manchar de blanco los siguientes prints del ESP32
    );
}


/**
 * @brief Muestra la configuracion actual.
 */
void show_config(void) {
    const uint32_t tbm = atomic_load(&settings.node.timeout_heartbeat_balance_mode);
    const uint32_t tnm = atomic_load(&settings.node.timeout_heartbeat_normal_mode);
    const uint32_t tsm = atomic_load(&settings.node.timeout_heartbeat_safe_mode);
    const uint32_t sample = atomic_load(&settings.node.sample_rate);
    const energy_mode_t energy = atomic_load(&settings.node.energy_mode);

    const char* energy_str = (energy == 0) ? "Bajo Consumo" :
                             (energy == 1) ? "Balanceado" : "Rendimiento";

    uart_send_text("\r\n" B_WHT "┌─────────────────────┬────────────────────────────────┐\r\n");
    uart_send_text("│" C_MAG " PARAMETRO           " B_WHT "│" C_MAG " VALOR ACTUAL                   " B_WHT "│\r\n");
    uart_send_text("├─────────────────────┼────────────────────────────────┤\r\n");

    uart_print_row(C_CYN, "WiFi SSID", "%s", (const char*)settings.wifi.ssid);
    uart_print_row(C_CYN, "WiFi Password", "%s", (const char*)settings.wifi.password);
    uart_print_row(C_CYN, "MQTT URI", "%s", settings.mqtt.uri);

    uart_send_text(B_WHT "├─────────────────────┼────────────────────────────────┤\r\n");

    uart_print_row(C_CYN, "Red ID", "%s", settings.network.id_network);
    uart_print_row(C_CYN, "Edge ID", "%s", settings.network.id_edge);
    uart_print_row(C_CYN, "Bypass URL", "%s", settings.network.url_https);
    uart_print_row(C_CYN, "Nombre Dispositivo", "%s", settings.node.device_name);

    uart_send_text(B_WHT "├─────────────────────┼────────────────────────────────┤\r\n");

    uart_print_row(C_YEL, "Heartbeat Balance", "%lu s", tbm/1000000);
    uart_print_row(C_YEL, "Heartbeat Normal", "%lu s", tnm/1000000);
    uart_print_row(C_YEL, "Heartbeat Safe", "%lu s", tsm/1000000);

    uart_send_text(B_WHT "├─────────────────────┼────────────────────────────────┤\r\n");

    uart_print_row(C_GRN, "Sample Rate", "%lu min", sample);
    uart_print_row(C_GRN, "Modo de Energia", "%s", energy_str);
    uart_print_row(C_GRN, "MQ135 R0", "%.2f kOhm", settings.node.mq135_r0);
    uart_print_row(C_GRN, "MQ135 Alpha EMA", "%.2f", settings.node.mq135_alpha_ema);
    uart_print_row(C_GRN, "DHT11 Alpha EMA", "%.2f", settings.node.dht11_alpha_ema);

    uart_send_text(B_WHT "└─────────────────────┴────────────────────────────────┘\r\n" T_RST "\r\n");
}


/**
 * @brief Muestra el menu principal de configuracion.
 */
static void show_menu(void) {
    uart_send_text(
        "\r\n"
        "┌──────────────────────────────────────────────────────────────┐\r\n"
        "│\033[1;36m                     MODO DE CONFIGURACIÓN                    \033[0m│\r\n"
        "├──────────────────────────────────────────────────────────────┤\r\n"
        "│ Use 'HELP' para ver comandos disponibles                     │\r\n"
        "│ Use 'SHOW' para ver la configuración actual                  │\r\n"
        "│ Use 'EXIT' para salir                                        │\r\n"
        "├──────────────────────────────────────────────────────────────┤\r\n"
        "│\033[1;33m Info: Los cambios se guardan automáticamente. Para salir del \033[0m│\r\n"
        "│\033[1;33m modo configuración deben estar todos los campos completos.   \033[0m│\r\n"
        "└──────────────────────────────────────────────────────────────┘\r\n\r\n"
    );
}


/**
 * @brief Muestra el menu de consulta para cambiar o no una configuracion existente.
 */
static void show_menu_change_settings(void) {
    uart_send_text(
        "\r\n" B_WHT
        "┌────────────────────────────────────────────────────────────┐\r\n"
        "│" C_GRN "      Se ha detectado una configuracion guardada en NVS     " B_WHT "│\r\n"
        "├────────────────────────────────────────────────────────────┤\r\n"
        "│" T_RST " ¿Desea cambiar algun atributo de la configuracion?         " B_WHT "│\r\n"
        "│" C_MAG " Tiene 20 seg para responder. Por omision se asume 'n'.     " B_WHT "│\r\n"
        "├────────────────────────────────────────────────────────────┤\r\n"
        "│" T_RST "  > Ingrese '" C_YEL "y" T_RST "' para cambiar la configuracion               " B_WHT "│\r\n"
        "│" T_RST "  > Ingrese '" C_YEL "n" T_RST "' para usar la configuracion actual           " B_WHT "│\r\n"
        "└────────────────────────────────────────────────────────────┘\r\n"
        T_RST " > "
    );
}


/**
 * @brief Verifica que todos los campos esten completos.
 * @return bool  Devuelve true cuando la configuracion esta completa. Sino retorna false.
 */
static bool setting_is_device_configured(void) {
    if (flags == FLAG_OK) return true;
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
static bool process_command(const char *command, bool flag_process) {
    char cmd[32];
    char param[100];
    char *endptr;

    // Parsear comando y parámetro
    int parsed = sscanf(command, "%31s %99[^\n]", cmd, param);

    if (parsed < 1) {
        uart_send_text("Error: comando inválido. Use HELP para ver los comandos disponibles\r\n");
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
            uart_send_text("Error: falta parametro <SSID>\r\n");
            return false;
        }
        safe_strcpy((char*)settings.wifi.ssid, param, sizeof(settings.wifi.ssid));
        const uint8_t len = strlen((char *)settings.wifi.ssid);
        atomic_store(&settings.wifi.ssid_len, len);
        flags |= FLAG_WIFI_SSID_OK;
        uart_send_text("Info: SSID configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_WIFI_PASS) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <password>\r\n");
            return false;
        }
        safe_strcpy((char*)settings.wifi.password, param, sizeof(settings.wifi.password));
        const uint8_t len = strlen((char *)settings.wifi.password);
        atomic_store(&settings.wifi.pass_len, len);
        flags |= FLAG_WIFI_PASS_OK;
        uart_send_text("Info: password WiFi configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQTT_URI) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <uri>\r\n");
            return false;
        }
        // Verificar el prefijo de seguridad MQTTS
        if (strncmp(param, MQTTS_PREFIX, MQTTS_PREFIX_LEN) != 0) {
            uart_send_text("Error: MQTT uri erroneo. Falta mqtts:// como primer parametro\r\n");
            return false;
        }
        safe_strcpy(settings.mqtt.uri, param, sizeof(settings.mqtt.uri));
        flags |= FLAG_MQTT_URI_OK;
        uart_send_text("Info: MQTT uri configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_NETWORK) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <id_network>\r\n");
            return false;
        }
        safe_strcpy(settings.network.id_network, param, sizeof(settings.network.id_network));
        flags |= FLAG_NETWORK_OK;
        uart_send_text("Info: red configurada correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_EDGE) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <id_edge>\r\n");
            return false;
        }
        safe_strcpy(settings.network.id_edge, param, sizeof(settings.network.id_edge));
        flags |= FLAG_EDGE_OK;
        uart_send_text("Info: edge configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_URL_HTTPS) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <url>\r\n");
            return false;
        }
        safe_strcpy(settings.network.url_https, param, sizeof(settings.network.url_https));
        flags |= FLAG_URL_HTTPS_OK;
        uart_send_text("Info: bypass url configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_DEVICE_NAME) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <name>\r\n");
            return false;
        }
        safe_strcpy(settings.node.device_name, param, sizeof(settings.node.device_name));
        flags |= FLAG_DEVICE_NAME_OK;
        uart_send_text("Info: nombre del dispositivo configurado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_SAMPLE) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <rate>\r\n");
            return false;
        }
        errno = 0;
        const unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
            uart_send_text("Error: ingrese un numero de muestreo valido\r\n");
        }
        if (val > 0) {
            atomic_store(&settings.node.sample_rate, val);
            flags |= FLAG_SAMPLE_OK;
            uart_send_text("Info: muestreo configurado correctamente\r\n");
        } else {
            uart_send_text("Error: ingrese un numero de muestreo valido\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_ENERGY_MODE) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <energy>\r\n");
            return false;
        }
        errno = 0;
        const unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE)) {
            uart_send_text("Error: ingrese un modo de energia valido\r\n");
        }
        switch (val) {
            case 0: atomic_store(&settings.node.energy_mode, val);
                    set_cpu_frequency(MIN_FREQ, true);
                    flags |= FLAG_ENERGY_OK;
                    uart_send_text("Info: modo de energia configurado correctamente. LOW_CONSUMPTION\r\n");
                    break;
            case 1: atomic_store(&settings.node.energy_mode, val);
                    set_cpu_frequency(MID_FREQ, false);
                    flags |= FLAG_ENERGY_OK;
                    uart_send_text("Info: modo de energia configurado correctamente. BALANCED\r\n");
                    break;
            case 2: atomic_store(&settings.node.energy_mode, val);
                    set_cpu_frequency(MAX_FREQ, false);
                    flags |= FLAG_ENERGY_OK;
                    uart_send_text("Info: modo de energia configurado correctamente. PERFORMANCE\r\n");
                    break;
            default: uart_send_text("Error: ingrese un modo de energia valido\r\n");
                     break;
        }
        return false;
    }

    if (strcmp(cmd, CMD_DELETE_LINKAGE_FLAG) == 0) {
        settings.network.linkage_flag = 0;
        uart_send_text("Info: linkage flag reseteado correctamente\r\n");
        return false;
    }

    if (strcmp(cmd, CMD_SET_HEARTBEAT_BM) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <time>\r\n");
            return false;
        }
        errno = 0;
        const unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
            uart_send_text("Error: ingrese un numero de tiempo valido\r\n");
        }
        if (val > 0) {
            atomic_store(&settings.node.timeout_heartbeat_balance_mode, val*1000000 + 5000000);
            flags |= FLAG_TBM_OK;
            uart_send_text("Info: tiempo de latidos en estado balance configurado correctamente\r\n");
        } else {
            uart_send_text("Error: ingrese un numero de tiempo de latido en estado balance valido\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_HEARTBEAT_N) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <time>\r\n");
            return false;
        }
        errno = 0;
        const unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
            uart_send_text("Error: ingrese un numero de tiempo valido\r\n");
        }
        if (val > 0) {
            atomic_store(&settings.node.timeout_heartbeat_normal_mode, val*1000000 + 5000000);
            flags |= FLAG_TN_OK;
            uart_send_text("Info: tiempo de latidos en estado normal configurado correctamente\r\n");
        } else {
            uart_send_text("Error: ingrese un numero de tiempo de latido en estado normal valido\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_HEARTBEAT_SM) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <time>\r\n");
            return false;
        }
        errno = 0;
        const unsigned long val = strtoul(param, &endptr, 10);
        if (endptr == param || (errno == ERANGE) || (val > UINT16_MAX)) {
            uart_send_text("Error: ingrese un numero de tiempo valido\r\n");
        }
        if (val > 0) {
            atomic_store(&settings.node.timeout_heartbeat_safe_mode, val*1000000 + 5000000);
            flags |= FLAG_TSM_OK;
            uart_send_text("Info: tiempo de latidos en estado safe configurado correctamente\r\n");
        } else {
            uart_send_text("Error: ingrese un numero de tiempo de latido en estado safe valido\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_MQ135_RZERO) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <resistance>\r\n");
            return false;
        }
        errno = 0;
        char *endptr;
        const float val = strtof(param, &endptr);
        if (endptr == param || errno == ERANGE) {
            uart_send_text("Error: ingrese un formato de resistencia valido (ej: 70)\r\n");
            return false;
        }
        if (val > 0.0f) {
            settings.node.mq135_r0 = val;
            flags |= FLAG_MQ135_R0_OK;
            uart_send_text("Info: resistencia R0 del MQ135 configurada correctamente\r\n");
        } else {
            uart_send_text("Error: la resistencia R0 debe ser mayor a 0\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_EMA_ALPHA_MQ135) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <alpha>\r\n");
            return false;
        }
        errno = 0;
        char *endptr;
        const float val = strtof(param, &endptr);
        if (endptr == param || errno == ERANGE) {
            uart_send_text("Error: ingrese un formato de alpha valido (ej: 0.5)\r\n");
            return false;
        }
        if (val > 0.0f && val < 1.0f) {
            settings.node.mq135_alpha_ema = val;
            flags |= FLAG_MQ135_ALPHA_EMA_OK;
            uart_send_text("Info: parámetro alpha del MQ135 configurado correctamente\r\n");
        } else {
            uart_send_text("Error: el parámetro alpha debe ser mayor a 0.0 y menor a 1.0\r\n");
        }
        return false;
    }

    if (strcmp(cmd, CMD_SET_EMA_ALPHA_DHT11) == 0) {
        if (parsed < 2) {
            uart_send_text("Error: falta parametro <alpha>\r\n");
            return false;
        }
        errno = 0;
        char *endptr;
        const float val = strtof(param, &endptr);
        if (endptr == param || errno == ERANGE) {
            uart_send_text("Error: ingrese un formato de alpha valido (ej: 0.5)\r\n");
            return false;
        }
        if (val > 0.0f && val < 1.0f) {
            settings.node.dht11_alpha_ema = val;
            flags |= FLAG_DHT11_ALPHA_EMA_OK;
            uart_send_text("Info: parámetro alpha del DHT11 configurado correctamente\r\n");
        } else {
            uart_send_text("Error: el parámetro alpha debe ser mayor a 0.0 y menor a 1.0\r\n");
        }
        return false;
    }

    if (flag_process) {  // Cambiando datos de una config existente
        if (strcmp(cmd, CMD_EXIT) == 0) {
            const esp_err_t ret = setting_save_to_nvs();
            if (ret == ESP_OK) {
                uart_send_text("\nInfo: configuración guardada correctamente. Saliendo del modo configuración\r\n");
                return true;
            }
            uart_send_text("Error: no se pudo guardar la configuracion\r\n");
            return false;
        }
    } else {   // Cargando una config
        if (strcmp(cmd, CMD_EXIT) == 0 && setting_is_device_configured()) {
            const esp_err_t ret = setting_save_to_nvs();
            if (ret == ESP_OK) {
                uart_send_text("\nInfo: configuración guardada correctamente. Saliendo del modo configuración\r\n");
                return true;
            }
            uart_send_text("Error: no se pudo guardar la configuracion\r\n");
            return false;
        }

        if (strcmp(cmd, CMD_EXIT) == 0 && !setting_is_device_configured()) {
            uart_send_text("Info: para salir del modo configuración, deben estar todos los campos configurados\r\n");
        }
        else {
            uart_send_text("Error: comando desconocido. Use HELP para ver comandos disponibles\r\n");
        }
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
        "iot/%s/new_setting", settings.network.id_network);

    snprintf(settings.mqtt.topic_edge_setting_ok, sizeof(settings.mqtt.topic_edge_setting_ok),
        "iot/%s/new_setting_ok", settings.network.id_network);

    snprintf(settings.mqtt.topic_delete_hub, sizeof(settings.mqtt.topic_delete_hub),
        "iot/%s/delete_hub", settings.network.id_network);

    snprintf(settings.mqtt.topic_active_hub, sizeof(settings.mqtt.topic_active_hub),
        "iot/%s/active", settings.network.id_network);

    snprintf(settings.mqtt.topic_ping, sizeof(settings.mqtt.topic_ping),
        "iot/%s/hub/%s/ping", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_ping_ack, sizeof(settings.mqtt.topic_ping_ack),
    "iot/%s/ping", settings.network.id_network);

    snprintf(settings.mqtt.topic_empty_queue, sizeof(settings.mqtt.topic_empty_queue),
        "iot/%s/hub/%s/empty_queue", settings.network.id_network, settings.node.mac_address);

    snprintf(settings.mqtt.topic_linkage_request, sizeof(settings.mqtt.topic_linkage_request),
        "iot/%s/linkage_request", settings.network.id_edge);

    snprintf(settings.mqtt.topic_linkage_ack, sizeof(settings.mqtt.topic_linkage_ack),
        "iot/%s/linkage_ack", settings.network.id_edge);
}


/**
 * @brief Tarea de envio de mensaje de configuracion. Se autoelimina cuando recibe confirmacion.
 * @param pvParameter
 */
void send_settings_task(void *pvParameter) {
    mqtt_packet_t packet;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            ESP_LOGI(TAG, "Info: tarea de envio de mensajes activa");
            bool running = true;

            while (running) {
                uint32_t rate = settings_get_node_sample_rate();
                if (rate == 0) rate = 1;
                const TickType_t loop_delay = pdMS_TO_TICKS(rate * 2 * 60000);

                if (generate_message_settings(&packet)) {
                    if (xQueueSend(queues.settings_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "Warning: cola llena, descartando paquete");
                        free(packet.payload);
                    }
                } else {
                    ESP_LOGE(TAG, "Error: problema en RAM al generar paquete");
                }

                uint32_t signal = 0;
                const BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &signal, loop_delay);

                if (result == pdTRUE) {
                    if (signal & NOTIFY_CMD_DESTROY) {
                        ESP_LOGW(TAG, "Warning: orden de destrucción recibida");
                        goto delete_task;
                    }

                    if (signal & NOTIFY_CMD_STOP) {
                        ESP_LOGI(TAG, "Info: orden de pausa recibida. Deteniendo envíos");
                        running = false;
                    }
                }
            }
        }
    }

delete_task:
    ESP_LOGI(TAG, "Info: tarea send_settings_task eliminada");
    vTaskDelete(NULL);
}


/**
 * @brief Configuracion manual del sistema a traves de UART.
 * @return bool Devuelve true cuando el proceso termina con exito, sino retorna false.
 */
static bool setting_mode_start(const bool flag_process) {
    char buffer_aux[100];
    char c;
    bool flag = false;

    uart_flush_input(UART_NUM_2);

    show_menu();
    strcpy(buffer_aux, "config>  ");
    uart_send_text(buffer_aux);

    while (1) {
        const int bytes = uart_read_bytes(SETTINGS_UART_PORT_NUM, (uint8_t*)&c, 1, portMAX_DELAY);

        if (bytes > 0) {
            if (c == '\n' || c == '\r') {
                if (strlen(uart_buffer) == 0) continue;
                uart_send_text("\r\n");
                flag = process_command(uart_buffer, flag_process);
                if (flag) break;
                memset(uart_buffer, 0, sizeof(uart_buffer));
                uart_send_text("\r\n");  // Nueva linea
                show_menu();
                uart_send_text(" >  ");
            }
            else {
                // Solo concatenar si hay espacio disponible en el buffer
                if (strlen(uart_buffer) < SETTINGS_BUFFER_SIZE - 1) {
                    char tmp[2] = {c, '\0'};
                    strcat(uart_buffer, tmp);
                    uart_send_text(tmp);
                }
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

    uart_flush_input(UART_NUM_2);

    show_menu_change_settings();
    uart_send_text(buffer_aux);

    TickType_t start = xTaskGetTickCount();

    while (1) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(20000)) {
            return false;
        }

        const int bytes = uart_read_bytes(SETTINGS_UART_PORT_NUM, (uint8_t*)&c, 1, pdMS_TO_TICKS(100));

        if (bytes > 0) {
            if (c == '\n' || c == '\r') {
                if (strlen(uart_buffer) == 0) continue;
                uart_send_text("\r\n");
                const int parsed = sscanf(uart_buffer, "%c", cmd);
                if (parsed < 1) {
                    uart_send_text("Error: comando invalido. Ingrese y o n -\r\n");
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
                    uart_send_text("Error: comando invalido. Ingrese y o n -\r\n");
                }
                memset(uart_buffer, 0, sizeof(uart_buffer));
                uart_send_text("\r\n");
                show_menu_change_settings();
            }
            else {
                // Solo concatenar si hay espacio disponible en el buffer
                if (strlen(uart_buffer) < SETTINGS_BUFFER_SIZE - 1) {
                    char tmp[2] = {c, '\0'};
                    strcat(uart_buffer, tmp);
                    uart_send_text(tmp);
                }
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
        ESP_LOGW(TAG, "Warning: borrando NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Info: NVS inicializado correctamente");

    ret = uart_config();
    if (ret != ESP_OK) return ESP_FAIL;

    settings_init();

    // Cargar configuracion si existe
    if (!setting_load_from_nvs()) {   // No existe
        memset(&settings, 0, sizeof(settings));
        bool flag = false;
        while (!flag) {
            flag = setting_mode_start(false);
        }
    }
    else {  // Si existe
        if (setting_mode_change()) {  // Cambiar parametros de la configuracion
            bool flag = false;
            while (!flag) {
                flag = setting_mode_start(true);
            }
        }
    }
    create_mqtt_topics();
    show_config();
    return ESP_OK;
}