/**
* @file https_bypass.h
 * @brief Módulo de comunicación de respaldo vía HTTPS (Bypass).
 *
 * Este componente se encarga de establecer un canal de comunicación directo
 * entre el dispositivo (ESP32) y el servidor en la nube cuando el intermediario
 * local (Edge) no está disponible o confiable.
 *
 * Su propósito es garantizar que las alertas lleguen a destino incluso en condiciones
 * de fallo de infraestructura local.
 */


#ifndef HTTPS_BYPASS_H
#define HTTPS_BYPASS_H

void https_bypass_task(void *pvParam);

#endif //HTTPS_BYPASS_H