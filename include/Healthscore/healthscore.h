#ifndef HEALTHSCORE_H
#define HEALTHSCORE_H

#include <stdio.h>

#define TIMEOUT_QOS_1   15
#define SOCKET_ERROR    10
#define HIGH_LATENCY    5
#define MEDIUM_LATENCY  2
#define LOW_LATENCY     5
#define RECOVERY        1


typedef enum {
    HEALTHY,
    DEGRADED,
    CRITICAL,
    UNAVAILABLE
} health_state_t;


typedef struct {
    health_state_t state;          // Estado de salud de la red
    uint8_t score;                 // Valor actual (0-100)
    uint64_t ping_start_time;      // Timestamp inicio medición RTT
    int32_t pending_msg_id;        // ID del mensaje QoS 1 en vuelo
    bool waiting_ack;              // Flag de espera
} health_monitor_t;





#endif //HEALTHSCORE_H