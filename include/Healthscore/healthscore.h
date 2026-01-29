/**
* @file healthscore.h
 * @brief Monitor de Salud de Red (Network Health Score).
 *
 * Este módulo implementa un sistema de puntuación para evaluar la calidad de la
 * conexión MQTT. Se basa en una arquitectura orientada a eventos, desacoplada
 * de la máquina de estados principal.
 *
 * El puntaje (0-100) se ve afectado por:
 * - Latencia (RTT) en mensajes QoS 1.
 * - Pérdida de paquetes (Timeouts).
 * - Errores de socket (Buffer lleno/Error local).
 * - Desconexiones.
 */


#ifndef HEALTHSCORE_H
#define HEALTHSCORE_H

#include <stdio.h>

#define TIMEOUT_QOS_1    15      // Puntaje que se le resta al score cuando un mensaje con qos1 tardo >5 seg en enviarse
#define SOCKET_ERROR     10      // Puntaje que se le resta al score cuando se produjo un error de socket
#define HIGH_LATENCY     5       // Puntaje que se le resta al score cuando la latencia es alta
#define MEDIUM_LATENCY   2       // Puntaje que se le resta al score cuando la latencia es media
#define LOW_LATENCY      5       // Puntaje que se le suma al score cuando la latencia es baja
#define RECOVERY         1       // Puntaje que se le suma al score cuando se recupera
#define MAX_PENDING_MSGS 5       // Maxima cantidad de mensajes pendientes permitida
#define MSG_TIMEOUT_US   5000000 // Timeout maximo para un mensaje qos1 antes de penalizar al score


typedef enum {
    HEALTHY,
    DEGRADED,
    CRITICAL,
    UNAVAILABLE
} health_state_t;


typedef enum {
    HEALTH_EVT_MSG_SENT,    // Salió un QoS 1 (Guardar timestamp)
    HEALTH_EVT_PUBACK,      // Llegó confirmación (Calcular RTT)
    HEALTH_EVT_ERROR_SEND,  // Fallo de socket/buffer (Penalizar)
    HEALTH_EVT_DISCONNECT   // Caída total (Resetear)
} event_type_t;


typedef struct {
    event_type_t event;     // Tipo de evento
    int32_t msg_id;         // Importante para matchear SENT con PUBACK
    int64_t timestamp;      // Momento del evento
} health_event_t;


// Estructura de un mensaje pendiente
typedef struct {
    int32_t msg_id;
    uint64_t start_time;
    bool active;
} pending_msg_t;


// Arreglo de MAX_PENDING_MSGS mensajes pendientes en simultaneo
typedef struct {
    pending_msg_t msg[MAX_PENDING_MSGS];
} pending_t;


void health_score_task(void *pvParameters);


#endif //HEALTHSCORE_H