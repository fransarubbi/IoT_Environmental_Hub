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
#include "mpack.h"
#include "Fsm/fsm.h"
#include "Message/message.h"


static void get_formated_data(stats_monitor_t *stats) {
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    settings_get_node_mac(stats->metadata.mac, sizeof(stats->metadata.mac));
    stats->metadata.time = get_time();
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
}



/**
 * @brief Tarea que monitoriza el uso de recursos.
 * @param pvParameter
 */
void stack_monitor_task(void *pvParameter) {
    mqtt_packet_t packet;
    stats_monitor_t stats;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;

            while (running) {
                uint32_t rate_min = settings_get_node_sample_rate();
                if (rate_min == 0) rate_min = 1;

                TickType_t dynamic_delay = pdMS_TO_TICKS(rate_min * 2 * 60000);

                get_formated_data(&stats);
                if (generate_message_monitor(&packet, stats)) {
                    if (xQueueSend(queues.monitor_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        free(packet.payload);
                    }
                }

                uint32_t stop_signal = 0;
                BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &stop_signal, dynamic_delay);

                if (result == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        running = false;
                    }
                }
            }
        }
    }
}




