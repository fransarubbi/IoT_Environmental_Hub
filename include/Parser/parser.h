/**
* @file parser.h
 * @brief Módulo de interpretación de mensajes MQTT.
 *
 * Este módulo se encarga de recibir los mensajes MQTT crudos desde la cola
 * del sistema, identificar el tópico correspondiente y despachar el payload
 * a la función de parseo específica (usando MPack o JSON según corresponda).
 * También gestiona la liberación de memoria de los mensajes procesados.
 */

#ifndef PARSER_H
#define PARSER_H

void parser_task(void *pvParameter);

#endif //PARSER_H