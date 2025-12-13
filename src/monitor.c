#include "Data/data.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "Wifi/wifi.h"
#include "esp_log.h"
#include "System/system.h"
#include "Time/time.h"


static const char *TAG = "Monitor";


/**
 * @brief Tarea que monitoriza el uso de recursos.
 * @param pvParameter
 */
void stack_monitor_task(void *pvParameter) {
    multi_heap_info_t info;
    TickType_t last_wake_time = xTaskGetTickCount();
    wifi_stats_t wifi_stats;
    char time[50];

    while (1) {
        char *json = (char*)heap_caps_malloc(JSON_MAX, MALLOC_CAP_8BIT);
        if (!json) {
            ESP_LOGE(TAG, "- ERROR: No hay memoria para buffers JSON -");
            esp_restart();
        }
        memset(json, 0, JSON_MAX);
        UBaseType_t hwm1 = uxTaskGetStackHighWaterMark(task_handle.data_ct_handle);
        UBaseType_t hwm2 = uxTaskGetStackHighWaterMark(task_handle.data_pt_handle);
        UBaseType_t hwm3 = uxTaskGetStackHighWaterMark(task_handle.ky037_handle);
        UBaseType_t hwm4 = uxTaskGetStackHighWaterMark(task_handle.dht11_handle);
        UBaseType_t hwm5 = uxTaskGetStackHighWaterMark(task_handle.mq135_handle);
        UBaseType_t hwm_monitor = uxTaskGetStackHighWaterMark(NULL);
        uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
        uint32_t hours = uptime_ms / 3600000ULL;
        uint32_t minutes = (uptime_ms % 3600000ULL) / 60000ULL;
        uint32_t seconds = (uptime_ms % 60000ULL) / 1000ULL;
        heap_caps_get_info(&info, MALLOC_CAP_8BIT);
        get_stats_wifi(&wifi_stats);
        get_time(time);

        snprintf(json, JSON_MAX,
                "{\n"
                "  \"ID\": \"%s\",\n"
                "  \"destination_type\": SERVER,\n"
                "  \"destination_id\": SERVER0,\n"
                "  \"timestamp\": \"%s\",\n"
                "  \"device_name\": \"%s\",\n"
                "  \"mem_free\": %lu,\n"
                "  \"mem_free_hm\": %lu,\n"
                "  \"mem_free_block\": %u,\n"
                "  \"mem_free_internal\": %u,\n"
                "  \"stack_free_min_coll\": %u,\n"
                "  \"stack_free_min_pub\": %u,\n"
                "  \"stack_free_min_mic\": %u,\n"
                "  \"stack_free_min_th\": %u,\n"
                "  \"stack_free_min_air\": %u,\n"
                "  \"stack_free_min_mon\": %u,\n"
                "  \"wifi_ssid\": \"%s\",\n"
                "  \"wifi_rssi\": %d,\n"
                "  \"energy_mode\": %u,\n"
                "  \"active_time\": %02lu:%02lu:%02lu,\n"
                "}",
                settings.node.mac_address,
                time,
                settings.node.device_name,
                esp_get_free_heap_size()/4,
                esp_get_minimum_free_heap_size()/4,
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)/4,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/4,
                hwm1, hwm2, hwm3, hwm4, hwm5, hwm_monitor, (char *)wifi_stats.ssid, wifi_stats.rssi, settings.node.energy_mode, hours, minutes, seconds);
        xQueueSend(queues.monitor_buffer, &json, portMAX_DELAY);

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(settings.node.sample_rate * 2 * MS_TO_MIN));
    }
}




