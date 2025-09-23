#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include "esp_err.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"


typedef struct {
    esp_mqtt_client_handle_t client;   // handler de ESP-IDF para el cliente MQTT
    esp_mqtt_client_config_t config;   // configuracion (URI, credenciales, etc.)
    BaseType_t reconnecting;           // flag para saber si la tarea de reconexión esta activa
} mqtt_client_t;


/* ----- Inicializa la estructura y configura el cliente ----- */
void mqtt_client_init(mqtt_client_t *mqtt, const char *uri,
                      const char *user, const char *pass);

/* ----- Inicia la conexion MQTT ----- */
esp_err_t mqtt_client_start(mqtt_client_t *mqtt);

/* ----- Publica un mensaje ----- */
esp_err_t mqtt_client_publish(mqtt_client_t *mqtt,
                                  const char *topic,
                                  const char *payload,
                                  int qos, int retain);

/* ----- Callback de eventos MQTT ----- */
esp_err_t mqtt_event_handler_cb(mqtt_client_t *mqtt,
                                esp_mqtt_event_handle_t event);

/* ----- Handler de eventos (puente requerido por ESP-IDF) ----- */
void mqtt_event_handler(void *handler_args,
                            esp_event_base_t base,
                            int32_t event_id,
                            void *event_data);

/* ----- Tarea de reconexion ----- */
void mqtt_reconnect_task(void *arg);

/* ----- Tarea principal ----- */
void mqtt_task(void *);


#endif // MQTT_CLIENT_H
