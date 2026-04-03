#include "Data/data.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"
#include "esp_log.h"
#include <esp_mac.h>
#include <esp_timer.h>
#include "certs/ca_crt.h"
#include "certs/client1_crt.h"
#include "certs/client1_key.h"
#include "Healthscore/healthscore.h"
#include "System/system.h"


typedef struct {
    esp_mqtt_client_handle_t client;   // handler de ESP-IDF para el cliente MQTT
    esp_mqtt_client_config_t config;   // configuracion (URI, credenciales, etc.)
} mqtt_client_t;


static const char *TAG = "MQTT";
static char mac_addr[18];
static mqtt_client_t mqtt;
static bool subscribe = false;


/**
 * @brief Subscribe el broker a todos los topicos de recepción.
 * @param client Handler del cliente mqtt.
 */
static void subscribe_to_all_topics(esp_mqtt_client_handle_t client) {
    char topic_buff[MAX_TOPIC];

    settings_get_mqtt_topic_edge_state_normal(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 1);

    settings_get_mqtt_topic_edge_state_balance(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 1);

    settings_get_mqtt_topic_edge_state_safe(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 1);

    settings_get_mqtt_topic_edge_phase(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 1);

    settings_get_mqtt_topic_edge_handshake(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 1);

    settings_get_mqtt_topic_heartbeat(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);

    settings_get_mqtt_topic_new_firmware(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);

    settings_get_mqtt_topic_new_settings(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);

    settings_get_mqtt_topic_edge_setting_ok(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);

    settings_get_mqtt_topic_delete_hub(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);

    settings_get_mqtt_topic_active_hub(topic_buff, sizeof(topic_buff));
    esp_mqtt_client_subscribe(client, topic_buff, 0);
}


/**
 * @brief Obtener direccion MAC del microcontrolador.
 * @return Retorna ESP_OK si no hubo fallas en la configuracion, sino ESP_FAIL.
 */
static esp_err_t get_mac_address(void) {
    uint8_t mac[6];

    const esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret == ESP_OK) {
        snprintf(mac_addr, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        return ESP_FAIL;
    }
    return ESP_OK;
}


/**
 * @brief Callback interno para procesar eventos MQTT específicos.
 * @param event Handle del evento MQTT con información del mismo
 * @return ESP_OK si el evento fue procesado correctamente
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(event_group.mqtt_event_group, MQTT_CONNECTED_BIT);
            xEventGroupClearBits(event_group.mqtt_event_group, MQTT_DISCONNECTED_BIT);

            if (subscribe) {
                subscribe_to_all_topics(event->client);
            } else {
                ESP_LOGI(TAG, "Conectado a MQTT (Suscripciones en espera de INIT_SYSTEM)");
            }
            break;

        case MQTT_EVENT_DISCONNECTED: {
            xEventGroupClearBits(event_group.mqtt_event_group, MQTT_CONNECTED_BIT);
            xEventGroupSetBits(event_group.mqtt_event_group, MQTT_DISCONNECTED_BIT);
            const health_event_t health = {
                .event = HEALTH_EVT_DISCONNECT,
                .msg_id = 0,
                .timestamp = 0
            };
            xQueueSend(queues.health, &health, pdMS_TO_TICKS(0));
            ESP_LOGW(TAG, "- WARNING: Desconectado del broker -");
            break;
        }

        case MQTT_EVENT_PUBLISHED: {
            const health_event_t health = {
                .event = HEALTH_EVT_PUBACK,
                .msg_id = event->msg_id,
                .timestamp = esp_timer_get_time()
            };
            xQueueSend(queues.health, &health, pdMS_TO_TICKS(0));
            ESP_LOGI(TAG, "- INFO: Publicado msg_id = %d -", event->msg_id);
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "- ERROR: Error tipo: %d -", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "- ERROR: Error SSL/TLS -");
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "- INFO: SUBSCRIBED -");
            break;

        case MQTT_EVENT_DATA: {
            static char *rx_buffer = NULL;
            static int rx_total_len = 0;
            // NUEVO: Variables estáticas para almacenar el tópico y su longitud
            static char rx_topic[MAX_TOPIC];
            static int rx_topic_len = 0;

            // 1. Llegada del primer fragmento
            if (event->current_data_offset == 0) {
                rx_total_len = event->total_data_len;
                rx_buffer = malloc(rx_total_len + 1);

                if (rx_buffer == NULL) {
                    ESP_LOGE(TAG, "ERROR: No hay RAM para reensamblar mensaje MQTT");
                    break;
                }

                // Guardar el tópico de forma segura solo en el primer fragmento
                if (event->topic_len > 0 && event->topic_len < MAX_TOPIC) {
                    memcpy(rx_topic, event->topic, event->topic_len);
                    rx_topic[event->topic_len] = '\0';
                    rx_topic_len = event->topic_len;
                } else {
                    rx_topic[0] = '\0';
                    rx_topic_len = 0;
                }
            }

            // 2. Copiar los datos del fragmento actual al buffer
            if (rx_buffer != NULL) {
                memcpy(rx_buffer + event->current_data_offset, event->data, event->data_len);
            }

            // 3. Evaluar si ya recibimos todo el mensaje
            if (event->current_data_offset + event->data_len >= rx_total_len) {
                if (rx_buffer != NULL) {
                    rx_buffer[rx_total_len] = '\0'; // Aseguramos finalización del payload
                    mqtt_msg_to_parse_t new_msg;

                    // Asignar el tópico utilizando la caché que guardamos en el primer fragmento
                    if (rx_topic_len > 0) {
                        strcpy(new_msg.topic, rx_topic);
                    } else {
                        new_msg.topic[0] = '\0';
                    }

                    new_msg.payload = rx_buffer;
                    new_msg.len = rx_total_len;

                    // Enviar a parseo y validar que no haya fuga de memoria si la cola falla
                    if (xQueueSend(queues.parser, &new_msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "WARNING: Cola de parseo llena. Descartando mensaje ensamblado.");
                        free(rx_buffer);
                    }

                    // Reiniciar las variables estáticas para el próximo mensaje
                    rx_buffer = NULL;
                    rx_total_len = 0;
                    rx_topic_len = 0;
                }
            }
            break;
        }
        default:
            break;
    }

    return ESP_OK;
}


/**
 * @brief Callback para manejar eventos del cliente MQTT.
 * @param handler_args Argumentos del handler
 * @param base Base del evento (ESP_EVENT_BASE)
 * @param event_id ID del evento MQTT
 * @param event_data Datos del evento (cast a esp_mqtt_event_handle_t)
 * @return void
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                        int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    mqtt_event_handler_cb(event);
}


/**
 * @brief Inicializacion y configuracion del cliente MQTT.
 * @return Retorna ESP_OK si no hubo fallas en la configuracion, sino ESP_FAIL.
 */
esp_err_t mqtt_init(void) {
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_err_t ret = get_mac_address();
    memset(&mqtt.config, 0, sizeof(esp_mqtt_client_config_t));

    static char mqtt_uri[MQTT_URI];

    settings_get_mqtt_uri(mqtt_uri, sizeof(mqtt_uri));

    if (ret == ESP_OK) {
        settings_set_node_mac(mac_addr);
        mqtt.config.broker.address.uri = mqtt_uri;   // Establecer la URI del broker
        mqtt.config.broker.verification.certificate = (const char *)ca_crt;  // Certificado CA
        mqtt.config.buffer.size = 1024;           // Tamaño del buffer de salida
        mqtt.config.buffer.out_size = 1024;       // Tamaño del buffer de envío
        mqtt.config.credentials.authentication.certificate = (const char *)client1_crt;  // Certificado del cliente
        mqtt.config.credentials.authentication.key = (const char *)client1_key;   // Clave para mTLS
        mqtt.config.credentials.client_id = mac_addr;    // ID (la MAC de la ESP32)
        mqtt.config.network.disable_auto_reconnect = false;   // Reconectar automaticamente si se pierde conexion
        mqtt.config.session.keepalive = 60;     // Mantener activa la conexion cada 60 seg cuando hay inactividad
        mqtt.config.session.protocol_ver = MQTT_PROTOCOL_V_5;   // MQTT Version 5
        mqtt.config.network.timeout_ms = 30000;   // Timeout de 30 seg
        mqtt.config.session.disable_clean_session = false;  //  No guarda sesion entre desconexiones
        mqtt.config.session.last_will.topic = "/devices/esp32/status";
        mqtt.config.session.last_will.msg = "offline";
        mqtt.config.session.last_will.qos = 1;
        mqtt.config.session.last_will.retain = true;
        mqtt.config.network.reconnect_timeout_ms = 5000;  // Esperar 5 seg entre intentos de reconexion
        mqtt.client = esp_mqtt_client_init(&mqtt.config);
        if (!mqtt.client) {
            ESP_LOGE(TAG, "- ERROR: No se pudo crear el cliente MQTT -");
            return ESP_FAIL;
        }
        esp_mqtt_client_register_event(mqtt.client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, mqtt.client);
        esp_mqtt_client_start(mqtt.client);
    }
    else {
        return ESP_FAIL;
    }
    return ESP_OK;
}


/**
 * @brief Publica el mensaje al broker.
 * @param topic Tópico de publicación.
 * @param payload Contenido del mensaje.
 * @param len Longitud del mensaje.
 * @param qos Quality Of Service del mensaje.
 * @param retain Mensaje retenido o no retenido.
 * @return Retorna ESP_OK si no hubo fallas en la configuracion, sino ESP_FAIL.
 */
int mqtt_publish(const char *topic, const char *payload, const int len, const int qos, const int retain) {
    if (!mqtt.client) return -1;
    const int msg_id = esp_mqtt_client_publish(mqtt.client,
                                               topic,payload,
                                               len,
                                               qos,
                                               retain);
    return msg_id;
}


/**
 * @brief Permite habilitar la suscripción a topicos para no recibir mensajes antes de tiempo.
 */
void mqtt_enable_subscribe_topics(void) {
    if (!subscribe) {
        subscribe = true;
        if (xEventGroupGetBits(event_group.mqtt_event_group) & MQTT_CONNECTED_BIT) {
            subscribe_to_all_topics(mqtt.client);
        }
    }
}


/**
 * @brief Permite habilitar la suscripción al topico de linkage unicamente.
 */
void mqtt_enable_subscribe_topic_linkage(void) {
    char topic_buff[MAX_TOPIC];

    if (xEventGroupGetBits(event_group.mqtt_event_group) & MQTT_CONNECTED_BIT) {
        settings_get_mqtt_topic_linkage_ack(topic_buff, sizeof(topic_buff));
        esp_mqtt_client_subscribe(mqtt.client, topic_buff, 1);
    }
}