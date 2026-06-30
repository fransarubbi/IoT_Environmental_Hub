#ifndef MONITOR_H
#define MONITOR_H


#include "Wifi/wifi.h"
#include "Setting/settings.h"

typedef struct {
    struct {
        uint32_t mem_free;
        uint32_t mem_free_hm;
        uint32_t mem_free_block;
        uint32_t mem_free_internal;
    } memory;
    struct {
        UBaseType_t https_handle;
        UBaseType_t health_handle;
        UBaseType_t parser_handle;
        UBaseType_t converter_handle;
        UBaseType_t heartbeat_handle;
        UBaseType_t collector;
        UBaseType_t publisher;
        UBaseType_t ky037;
        UBaseType_t dht11;
        UBaseType_t mq135;
        UBaseType_t monitor;
        UBaseType_t fsm;
    } stack;
    wifi_stats_t wifi_stats;
    uint8_t energy_mode;
} stats_monitor_t;


void stack_monitor_task(void *pvParameter);

#endif //MONITOR_H