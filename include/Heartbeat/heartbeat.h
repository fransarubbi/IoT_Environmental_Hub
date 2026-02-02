/**
* @file heartbeat.h
 * @brief Monitor de Vitalidad (Application Watchdog).
 *
 * Este módulo implementa un mecanismo de seguridad para verificar que el sistema
 * mantiene conectividad y operatividad. Funciona mediante un sistema de "créditos"
 * o vidas que se recargan con mensajes MQTT y se consumen por tiempo.
 */


#ifndef HEARTBEAT_H
#define HEARTBEAT_H


/** * @brief Cantidad máxima de vidas acumulables.
 * Evita que el contador se desborde (overflow) si la conexión es muy estable.
 */
#define MAX_LIVES 3

void heartbeat_task(void *pvParameter);

#endif //HEARTBEAT_H