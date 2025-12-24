#include "Wifi/wifi.h"
#include "Setting/settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "System/system.h"


static wifi_ap_record_t ap_info;
static const char *TAG = "Wifi";
static int s_retry_num = 0;


/**
 * @brief Manejador de eventos (Event Handler) para la gestion de conexion WiFi e IP.
 *
 * Esta función es el callback principal registrado en el bucle de eventos de ESP-IDF.
 * Gestiona el ciclo de vida de la conexion en tres etapas clave:
 * - **Inicio (START):** Intenta conectar al AP inmediatamente.
 * - **Desconexion (DISCONNECTED):** Gestiona la logica de reintentos automatica. Si se
 * supera el limite `WIFI_MAX_RETRY`, marca el evento como fallido en el EventGroup.
 * - **Obtención de IP (GOT_IP):** Al obtener una IP valida, la formatea y almacena
 * de forma segura en el modulo de configuraciones, resetea contadores y notifica exito.
 *
 * @param arg Argumento de usuario opcional pasado al registrar el manejador.
 * @param event_base Identificador base del evento.
 * @param event_id ID específico del evento.
 * @param event_data Puntero a los datos asociados al evento (varia según el event_id).
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if ( s_retry_num < WIFI_MAX_RETRY ) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "- INFO: Reintentando conexion al AP -");
        }
        else {
            ESP_LOGE(TAG, "- ERROR: Fallo al conectar al AP tras %d intentos -", WIFI_MAX_RETRY);
            xEventGroupSetBits(event_group.wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_temp[32];
        snprintf(ip_temp, sizeof(ip_temp), IPSTR, IP2STR(&event->ip_info.ip));
        settings_set_wifi_ip(ip_temp);
        s_retry_num = 0;
        xEventGroupSetBits(event_group.wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


/**
 * @brief Espera de forma bloqueante el resultado del intento de conexion WiFi.
 *
 * Esta funcion suspende la ejecucion de la tarea actual utilizando `xEventGroupWaitBits`
 * hasta que ocurre una de las siguientes condiciones:
 * 1. Se establece la conexion (se levanta el bit @c WIFI_CONNECTED_BIT).
 * 2. Falla la conexion definitivamente (se levanta el bit @c WIFI_FAIL_BIT).
 * 3. Expira el tiempo maximo de espera definido en @c WIFI_TIMEOUT.
 *
 * @return
 * - @c ESP_OK: Conexion exitosa. El dispositivo tiene IP.
 * - @c ESP_FAIL: Fallo en la conexión.
 * - @c ESP_ERR_TIMEOUT: El tiempo de espera expiro antes de obtener un resultado definitivo.
 */
static esp_err_t wifi_wait_for_connection() {
    EventBits_t bits = xEventGroupWaitBits(event_group.wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "- INFO: Conexion Wi-Fi exitosa -");
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "- ERROR: Fallo al conectar Wi-Fi -");
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "- ERROR: Timeout esperando conexion Wi-Fi -");
    return ESP_ERR_TIMEOUT;
}


/**
 * @brief Inicializa el modulo WiFi.
 * @return ESP_OK si el procedimiento fue correcto, caso contrario ocurrio un error.
 */
esp_err_t wifi_init(void) {

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) return ret;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) return ret;
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) return ret;

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ret = esp_event_handler_instance_register(
                        WIFI_EVENT,
                        ESP_EVENT_ANY_ID,
                        &wifi_event_handler,
                        NULL,
                        &instance_any_id);
    if (ret != ESP_OK) return ret;

    ret = esp_event_handler_instance_register(
                        IP_EVENT,
                        IP_EVENT_STA_GOT_IP,
                        &wifi_event_handler,
                        NULL,
                        &instance_got_ip);
    if (ret != ESP_OK) return ret;

    wifi_config_t wifi_config = { 0 };
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
    uint8_t ssid[WIFI_SSID], password[WIFI_PASSWORD];
    settings_get_wifi_ssid(ssid, sizeof(ssid));
    settings_get_wifi_password(password, sizeof(password));
    memcpy(wifi_config.sta.ssid, ssid, sizeof(ssid));
    memcpy(wifi_config.sta.password, password, sizeof(password));

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) return ret;

    ret = esp_wifi_start();
    if (ret != ESP_OK) return ret;

    ret = wifi_wait_for_connection();
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}


/**
 * @brief Retorna estadisticas basicas de la conexion WiFi.
 * @param wifi_stats Estructura que guarda los datos necesarios.
 */
void get_stats_wifi(wifi_stats_t *wifi_stats) {
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err == ESP_OK) {
        wifi_stats->rssi = ap_info.rssi;
        memcpy(wifi_stats->ssid, ap_info.ssid, sizeof(wifi_stats->ssid));
    }
    else {
        wifi_stats->rssi = -127;
        memset(wifi_stats->ssid, 0, sizeof(wifi_stats->ssid));
    }
}


