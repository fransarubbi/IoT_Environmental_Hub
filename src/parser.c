/**
* @file parser.c
 * @brief Implementación de la tarea de parseo y enrutamiento de mensajes.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "Parser/parser.h"
#include "Message/message.h"
#include <esp_log.h>
#include "MQTT/mqtt.h"
#include "System/system.h"


/**
 * @brief Estructura interna para almacenar en caché los tópicos del sistema.
 */
typedef struct {
    char topic_state_normal[MAX_TOPIC];
    char topic_state_balance[MAX_TOPIC];
    char topic_state_safe[MAX_TOPIC];
    char topic_state_phase[MAX_TOPIC];
    char topic_handshake[MAX_TOPIC];
    char topic_heartbeat[MAX_TOPIC];
    char topic_new_firmware[MAX_TOPIC];
    char topic_new_settings_to_hub[MAX_TOPIC];
    char topic_setting_ok[MAX_TOPIC];
    char topic_delete_hub[MAX_TOPIC];
    char topic_active_hub[MAX_TOPIC];
} topics;


/**
 * @brief Carga todos los tópicos desde la configuración a la estructura local.
 * @param topics Puntero a la estructura de tópicos a rellenar.
 */
static void get_all_topics(topics *topics) {
    settings_get_mqtt_topic_edge_state_normal(topics->topic_state_normal, sizeof(topics->topic_state_normal));
    settings_get_mqtt_topic_edge_state_balance(topics->topic_state_balance, sizeof(topics->topic_state_balance));
    settings_get_mqtt_topic_edge_state_safe(topics->topic_state_safe, sizeof(topics->topic_state_safe));
    settings_get_mqtt_topic_edge_phase(topics->topic_state_phase, sizeof(topics->topic_state_phase));
    settings_get_mqtt_topic_edge_handshake(topics->topic_handshake, sizeof(topics->topic_handshake));
    settings_get_mqtt_topic_heartbeat(topics->topic_heartbeat, sizeof(topics->topic_heartbeat));
    settings_get_mqtt_topic_new_firmware(topics->topic_new_firmware, sizeof(topics->topic_new_firmware));
    settings_get_mqtt_topic_new_settings(topics->topic_new_settings_to_hub, sizeof(topics->topic_new_settings_to_hub));
    settings_get_mqtt_topic_settings_ok(topics->topic_setting_ok, sizeof(topics->topic_setting_ok));
    settings_get_mqtt_topic_delete_hub(topics->topic_delete_hub, sizeof(topics->topic_delete_hub));
    settings_get_mqtt_topic_active_hub(topics->topic_active_hub, sizeof(topics->topic_active_hub));
}


/**
 * @brief Tarea principal de parseo (Consumer).
 *
 * Ciclo infinito que espera mensajes en la cola `queues.parser`.
 * Al recibir un mensaje:
 * 1. Compara el tópico con los configurados en el sistema.
 * 2. Llama al parser específico.
 * 3. Libera la memoria del payload para evitar fugas.
 *
 * @param pvParameter Parámetro de FreeRTOS (no utilizado).
 */
void parser_task(void *pvParameter) {
    mqtt_msg_to_parse_t to_parse;
    static topics topics;
    get_all_topics(&topics);

    while (1) {
        if (xQueueReceive(queues.parser, &to_parse, portMAX_DELAY) == pdTRUE) {
            if (strcmp(to_parse.topic, topics.topic_state_normal) == 0) {
                if (!parse_message_state_normal(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de state_normal");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_state_balance) == 0) {
                if (!parse_message_state_balance(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de state_balance");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_state_safe) == 0) {
                if (!parse_message_state_safe(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de state_safe");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_state_phase) == 0) {
                if (!parse_message_phase(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de phase");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_handshake) == 0) {
                if (!parse_message_handshake(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de handshake");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_heartbeat) == 0) {
                if (!parse_message_heartbeat(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de heartbeat");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_new_firmware) == 0) {
                if (!parse_message_new_firmware(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de new_firmware");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_new_settings_to_hub) == 0) {
                if (!parse_message_setting(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de new_settings");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_setting_ok) == 0) {
                if (!parse_message_setting_ok(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de settings_ok");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_delete_hub) == 0) {
                if (!parse_message_delete(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de delete");
                }
            }
            else if (strcmp(to_parse.topic, topics.topic_active_hub) == 0) {
                if (!parse_message_active(to_parse.payload, to_parse.len)) {
                    ESP_LOGE("Parser", "ERROR: No se pudo parsear mensaje de active");
                }
            }
            if (to_parse.payload != NULL) {
                free(to_parse.payload);
                to_parse.payload = NULL;
            }
        }
    }
}
