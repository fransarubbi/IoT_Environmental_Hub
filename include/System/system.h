#ifndef SYSTEM_H
#define SYSTEM_H

#define PRIO_SENSORS    3
#define PRIO_COMMS      4
#define CORE_0          0
#define CORE_1          1

#define STACK_DHT11     8000
#define STACK_MIC       8000
#define STACK_COLLECTOR 8000
#define STACK_PUBLISHER 8000
#define STACK_MONITOR   8000
#define STACK_MQ135     8000
#define STACK_SEND_SETT 8000

#define QUEUE_LENGTH 100
#define QUEUE 2
#define TIME_SETUP 240000

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


/* Estructura que contiene todas las colas del sistema */
typedef struct {
    QueueHandle_t data_buffer;
    QueueHandle_t alert_buffer;
    QueueHandle_t monitor_buffer;
    QueueHandle_t dht11_buffer;
    QueueHandle_t ky037_buffer;
    QueueHandle_t mq135_buffer;
    QueueHandle_t dht11_to_mq135;
    QueueHandle_t settings_buffer;
} app_queues_t;


/* Estructura que contiene todos los event group */
typedef struct {
    EventGroupHandle_t collector_events;
    EventGroupHandle_t mqtt_event_group;
    EventGroupHandle_t wifi_event_group;
} app_event_group_t;


/* Estructura que contiene todos los task handle */
typedef struct {
    TaskHandle_t dht11_handle;
    TaskHandle_t ky037_handle;
    TaskHandle_t mq135_handle;
    TaskHandle_t data_pt_handle;
    TaskHandle_t data_ct_handle;
    TaskHandle_t monitor_handle;
    TaskHandle_t send_settings_handle;
} app_task_handle_t;


/* Estructura que contiene todos los buffers para crear task estaticas */
typedef struct {
    struct {
        StackType_t stack[STACK_DHT11];
        StaticTask_t tcb;
    } dht11;
    struct {
        StackType_t stack[STACK_MIC];
        StaticTask_t tcb;
    } ky037;
    struct {
        StackType_t stack[STACK_MQ135];
        StaticTask_t tcb;
    } mq135;
    struct {
        StackType_t stack[STACK_COLLECTOR];
        StaticTask_t tcb;
    } collector;
    struct {
        StackType_t stack[STACK_PUBLISHER];
        StaticTask_t tcb;
    } publisher;
    struct {
        StackType_t stack[STACK_MONITOR];
        StaticTask_t tcb;
    } monitor;
    struct {
        StackType_t stack[STACK_SEND_SETT];
        StaticTask_t tcb;
    } send_settings;
} app_static_mem_t;


extern app_queues_t queues;
extern app_event_group_t event_group;
extern app_static_mem_t mem;
extern app_task_handle_t task_handle;


/* ===== API ===== */
bool init_queues(void);
bool init_event_group(void);
bool init_base_drivers(void);
void wait_for_sensors(void);
void start_application_tasks(void);


#endif //SYSTEM_H