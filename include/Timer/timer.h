/**
* @file timer.h
 * @brief Gestor de Temporizadores de Software (Wrapper para esp_timer).
 *
 * Este módulo administra un conjunto limitado de temporizadores de alta resolución
 * basados en la API esp_timer del ESP32. Permite iniciar, detener y eliminar
 * timers asociados a eventos específicos del sistema mediante un array estático.
 *
 * @note Este módulo no es thread-safe para llamadas concurrentes desde múltiples tareas,
 * aunque los callbacks se ejecutan en el contexto de alta prioridad del sistema.
 */

#ifndef TIMER_H
#define TIMER_H

#define MAX_SIMULTANEOUS_TIMERS         3
#define TIMEOUT_HEARTBEAT_NORMAL        60000000  // 60 seg
#define TIMEOUT_HEARTBEAT_BALANCE_MODE  30000000  // 30 seg
#define TIMEOUT_HEARTBEAT_SAFE_MODE     70000000  // 70 seg
#define TIMEOUT_INIT_SYSTEM             60000000  // 60 seg
#define TIMEOUT_COOLING_TIMER           30000000  // 30 seg
#define TIMEOUT_BYPASS_TIMER            40000000  // 40 seg

#include <esp_timer.h>


/**
 * @brief Identificadores de los tipos de temporizadores del sistema.
 *
 * Estos IDs se usan para crear el timer y también se pasan como argumento
 * (void*) al callback genérico para identificar qué timer expiró.
 */
typedef enum {
    NONE = 0,
    HEARTBEAT_NORMAL_TIMER,
    HEARTBEAT_BALANCE_MODE_TIMER,
    HEARTBEAT_SAFE_MODE_TIMER,
    INIT_SYSTEM_TIMER,
    COOLING_TIMER,
    BYPASS_TIMER,
    INIT_BALANCE_TIMER,
    HANDSHAKE_TIMER,
} timer_types_t;


/**
 * @brief Estructura interna para almacenar la información de un timer activo.
 */
typedef struct {
    timer_types_t type;
    esp_timer_handle_t handle;
} timer_info_t;


/**
 * @brief Contenedor principal del pool de timers.
 */
typedef struct {
    timer_info_t timers[MAX_SIMULTANEOUS_TIMERS];
} timers_t;


void init_timer(timer_types_t type);
void delete_timer(timer_types_t type);


#endif //TIMER_H