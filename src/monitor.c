#include "Data/data.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "Wifi/wifi.h"
#include "esp_log.h"
#include "System/system.h"
#include "Time/time.h"
#include "Monitor/monitor.h"
#include "MQTT/mqtt.h"
#include "components/mpack/include/mpack.h"



typedef struct {
    struct {
        char mac[MAC];
        char time[TIME_MAX_LEN];
        char device[DEVICE_NAME];
    } metadata;
    struct {
        uint32_t mem_free;
        uint32_t mem_free_hm;
        uint32_t mem_free_block;
        uint32_t mem_free_internal;
    } memory;
    struct {
        UBaseType_t collector;
        UBaseType_t publisher;
        UBaseType_t ky037;
        UBaseType_t dht11;
        UBaseType_t mq135;
        UBaseType_t monitor;
    } stack;
    wifi_stats_t wifi_stats;
    uint8_t energy_mode;
    struct {
        uint32_t hours;
        uint32_t minutes;
        uint32_t seconds;
    } uptime;
} stats_monitor_t;




static bool generate_mpack_settings(mqtt_packet_t *packet, stats_monitor_t stats) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_MONITOR_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char time_str[30];
    snprintf(time_str, sizeof(time_str), "%lu:%02lu:%02lu",
             stats.uptime.hours, stats.uptime.minutes, stats.uptime.seconds);

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 19);
    mpack_write_cstr(&writer, "ID");                   mpack_write_cstr(&writer, stats.metadata.mac);
    mpack_write_cstr(&writer, "destination_type");     mpack_write_cstr(&writer, "SERVER");
    mpack_write_cstr(&writer, "destination_id");       mpack_write_cstr(&writer, "SERVER0");
    mpack_write_cstr(&writer, "timestamp");            mpack_write_cstr(&writer, stats.metadata.time);
    mpack_write_cstr(&writer, "device_name");          mpack_write_cstr(&writer, stats.metadata.device);
    mpack_write_cstr(&writer, "mem_free");             mpack_write_u32(&writer, stats.memory.mem_free);
    mpack_write_cstr(&writer, "mem_free_hm");          mpack_write_u32(&writer, stats.memory.mem_free_hm);
    mpack_write_cstr(&writer, "mem_free_block");       mpack_write_u32(&writer, stats.memory.mem_free_block);
    mpack_write_cstr(&writer, "mem_free_internal");    mpack_write_u32(&writer, stats.memory.mem_free_internal);
    mpack_write_cstr(&writer, "stack_free_min_coll");  mpack_write_u32(&writer, stats.stack.collector);
    mpack_write_cstr(&writer, "stack_free_min_pub");   mpack_write_u32(&writer, stats.stack.publisher);
    mpack_write_cstr(&writer, "stack_free_min_mic");   mpack_write_u32(&writer, stats.stack.ky037);
    mpack_write_cstr(&writer, "stack_free_min_th");    mpack_write_u32(&writer, stats.stack.dht11);
    mpack_write_cstr(&writer, "stack_free_min_air");   mpack_write_u32(&writer, stats.stack.mq135);
    mpack_write_cstr(&writer, "stack_free_min_mon");   mpack_write_u32(&writer, stats.stack.monitor);
    mpack_write_cstr(&writer, "wifi_ssid");            mpack_write_str(&writer, (const char*)stats.wifi_stats.ssid, strlen((const char*)stats.wifi_stats.ssid));
    mpack_write_cstr(&writer, "wifi_rssi");            mpack_write_i8(&writer, stats.wifi_stats.rssi);
    mpack_write_cstr(&writer, "energy_mode");          mpack_write_u8(&writer, stats.energy_mode);
    mpack_write_cstr(&writer, "active_time");          mpack_write_cstr(&writer, time_str);

    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Monitor", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}



static void get_formated_data(stats_monitor_t *stats) {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    settings_get_node_mac(stats->metadata.mac, sizeof(stats->metadata.mac));
    get_time(stats->metadata.time);
    settings_get_node_device_name(stats->metadata.device, sizeof(stats->metadata.device));
    stats->memory.mem_free = esp_get_free_heap_size()/4;
    stats->memory.mem_free_hm = esp_get_minimum_free_heap_size()/4;
    stats->memory.mem_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)/4;
    stats->memory.mem_free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/4;
    stats->stack.collector = uxTaskGetStackHighWaterMark(task_handle.data_ct_handle);
    stats->stack.publisher = uxTaskGetStackHighWaterMark(task_handle.data_pt_handle);
    stats->stack.ky037 = uxTaskGetStackHighWaterMark(task_handle.ky037_handle);
    stats->stack.dht11 = uxTaskGetStackHighWaterMark(task_handle.dht11_handle);
    stats->stack.mq135 = uxTaskGetStackHighWaterMark(task_handle.mq135_handle);
    stats->stack.monitor = uxTaskGetStackHighWaterMark(task_handle.monitor_handle);
    get_stats_wifi(&(stats->wifi_stats));
    stats->energy_mode = settings_get_node_energy_mode();
    uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    stats->uptime.hours = uptime_ms / 3600000ULL;
    stats->uptime.minutes = (uptime_ms % 3600000ULL) / 60000ULL;
    stats->uptime.seconds = (uptime_ms % 60000ULL) / 1000ULL;
}



/**
 * @brief Tarea que monitoriza el uso de recursos.
 * @param pvParameter
 */
void stack_monitor_task(void *pvParameter) {
    TickType_t last_wake_time = xTaskGetTickCount();
    mqtt_packet_t packet;
    stats_monitor_t stats;

    while (1) {
        get_formated_data(&stats);
        if (generate_mpack_settings(&packet, stats)) {
            if (xQueueSend(queues.monitor_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                free(packet.payload);
            }
        }
        else {
            ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
        }
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(settings_get_node_sample_rate() * 2 * MS_TO_MIN));
    }
}




