/**
* @file converter.h
 * @brief Módulo de Traducción de Señales (Flags) a Eventos de FSM.
 *
 * Este componente actúa como un intermediario inteligente entre las fuentes de
 * interrupción/notificación (Timers, Parser MQTT, HealthScore) y la Máquina de
 * Estados Finitos (FSM).
 *
 * Su responsabilidad es:
 * 1. Recibir flags genéricos (ej. TIMEOUT, DATA_RECEIVED).
 * 2. Consultar el estado actual del sistema.
 * 3. Determinar si ese flag es relevante para el estado actual.
 * 4. Generar el Evento específico que disparará la transición en la FSM.
 */


#ifndef CONVERTER_H
#define CONVERTER_H

void flag_converter_task(void *pvParameters);

#endif //CONVERTER_H