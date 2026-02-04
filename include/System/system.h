#ifndef SYSTEM_H
#define SYSTEM_H

// --- PRIORIDADES ---
#define PRIO_FSM        10
#define PRIO_CONVERTER  10
#define PRIO_HEARTBEAT  10
#define PRIO_DHT11      9
#define PRIO_COLLECTOR  8
#define PRIO_PARSER     6
#define PRIO_PUBLISHER  6
#define PRIO_HTTPS      6
#define PRIO_SENSOR     4
#define PRIO_HEALTH     3
#define PRIO_MONITOR    1
#define PRIO_SETTINGS   1

// --- NÚCLEOS ---
#define CORE_PRO        0  // Protocol CPU (WiFi, BT, Network)
#define CORE_APP        1  // Application CPU (Logic, Sensors)

// --- STACKS ---
#define STACK_FSM       4096
#define STACK_HTTPS     6144
#define STACK_HEALTH    3072
#define STACK_PARSER    4096
#define STACK_CONVERTER 4096
#define STACK_HEARTBEAT 3072
#define STACK_DHT11     3072
#define STACK_MIC       3072
#define STACK_MQ135     3072
#define STACK_COLLECTOR 4096
#define STACK_PUBLISHER 6144 // Necesita espacio para JSONs grandes
#define STACK_MONITOR   3072
#define STACK_SEND_SETT 4096

// --- CAPACIDAD MAXIMA DE COLAS ---
#define QUEUE_HEART     5
#define QUEUE_HEALTH    5
#define QUEUE_PARSER    5
#define QUEUE_GENERAL   10
#define QUEUE_FLAG      5
#define QUEUE_EVENT     5
#define QUEUE_LENGTH    100
#define QUEUE           5

#define TIME_SETUP 240000

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"


/* Estructura que contiene todas las colas del sistema */
typedef struct {
    QueueHandle_t heartbeat;
    QueueHandle_t parser;
    QueueHandle_t health;
    QueueHandle_t general;
    QueueHandle_t flag;
    QueueHandle_t event;
    QueueHandle_t data_buffer;
    QueueHandle_t alert_temp_buffer;
    QueueHandle_t alert_air_buffer;
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
    TaskHandle_t fsm_handle;
    TaskHandle_t https_handle;
    TaskHandle_t health_handle;
    TaskHandle_t parser_handle;
    TaskHandle_t converter_handle;
    TaskHandle_t heartbeat_handle;
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
        StackType_t stack[STACK_FSM];
        StaticTask_t tcb;
    } fsm;
    struct {
        StackType_t stack[STACK_HTTPS];
        StaticTask_t tcb;
    } https;
    struct {
        StackType_t stack[STACK_HEALTH];
        StaticTask_t tcb;
    } health;
    struct {
        StackType_t stack[STACK_PARSER];
        StaticTask_t tcb;
    } parser;
    struct {
        StackType_t stack[STACK_FSM];
        StaticTask_t tcb;
    } converter;
    struct {
        StackType_t stack[STACK_HEARTBEAT];
        StaticTask_t tcb;
    } heartbeat;
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