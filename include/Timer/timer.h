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

#define MAX_SIMULTANEOUS_TIMERS 3

#include <esp_timer.h>


/**
 * @brief Identificadores de los tipos de temporizadores del sistema.
 *
 * Estos IDs se usan para crear el timer y también se pasan como argumento
 * (void*) al callback genérico para identificar qué timer expiró.
 */
typedef enum {
    NONE = 0,
    HEARTBEAT_TIMER,
    INIT_SYSTEM_TIMER,
    COOLING_TIMER,
    PING_TIMER,
    SAFE_MODE_TIMER,
    INIT_BALANCE_MODE_TIMER
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


void init_timer(timer_types_t timer);
void delete_timer(timer_types_t type);


#endif //TIMER_H