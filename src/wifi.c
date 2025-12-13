#include "Wifi/wifi.h"
#include "Setting/settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "System/system.h"

static wifi_ap_record_t ap_info;

static const char *TAG = "wifi";
static int s_retry_num = 0;



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
            ESP_LOGE(TAG, "Fallo al conectar al AP tras %d intentos", WIFI_MAX_RETRY);
            xEventGroupSetBits(event_group.wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "- INFO: IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(settings.wifi.ip, 30, IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(event_group.wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


static esp_err_t wifi_wait_for_connection() {
    EventBits_t bits = xEventGroupWaitBits(event_group.wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conexión Wi-Fi exitosa");
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Fallo al conectar Wi-Fi");
        return ESP_FAIL;
    }
    ESP_LOGE(TAG, "Timeout esperando conexión Wi-Fi");
    return ESP_ERR_TIMEOUT;
}


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
    memcpy(wifi_config.sta.ssid, settings.wifi.ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, settings.wifi.password, sizeof(wifi_config.sta.password));

    ESP_LOGI("WIFI", "SSID: %d", strlen((char *)wifi_config.sta.ssid));
    ESP_LOGI("WIFI", "Password length: %d", strlen((char *)wifi_config.sta.password));

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


