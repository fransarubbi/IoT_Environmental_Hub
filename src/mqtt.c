#include "Data/data.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"
#include "esp_log.h"
#include <esp_mac.h>
#include "certs/ca_crt.h"
#include "certs/client1_crt.h"
#include "certs/client1_key.h"
#include "Message/message.h"
#include "System/system.h"


typedef struct {
    esp_mqtt_client_handle_t client;   // handler de ESP-IDF para el cliente MQTT
    esp_mqtt_client_config_t config;   // configuracion (URI, credenciales, etc.)
} mqtt_client_t;


static const char *TAG = "MQTT";
static char mac_addr[18];
static mqtt_client_t mqtt;



/**
 * @brief Obtener direccion MAC del microcontrolador.
 * @return Retorna ESP_OK si no hubo fallas en la configuracion, sino ESP_FAIL.
 */
static esp_err_t get_mac_address(void) {
    uint8_t mac[6];

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
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
    char topic_settings[MAX_TOPIC];
    char topic_handshake[MAX_TOPIC];

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(event_group.mqtt_event_group, MQTT_CONNECTED_BIT);
            xEventGroupClearBits(event_group.mqtt_event_group, MQTT_DISCONNECTED_BIT);
            settings_get_mqtt_topic_settings(topic_settings, sizeof(topic_settings));
            settings_get_mqtt_topic_settings(topic_handshake, sizeof(topic_handshake));
            esp_mqtt_client_subscribe(event->client, topic_settings, 1);
            esp_mqtt_client_subscribe(event->client, topic_handshake, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(event_group.mqtt_event_group, MQTT_CONNECTED_BIT);
            xEventGroupSetBits(event_group.mqtt_event_group, MQTT_DISCONNECTED_BIT);
            ESP_LOGW(TAG, "- WARNING: Desconectado del broker -");
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "- INFO: Publicado msg_id = %d -", event->msg_id);
            break;

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
            char msg[event->data_len + 1];
            memcpy(msg, event->data, event->data_len);
            msg[event->data_len] = '\0';

            settings_get_mqtt_topic_settings(topic_settings, sizeof(topic_settings));
            //settings_get_mqtt_topic_handshake(topic_handshake, sizeof(topic_handshake));

            if (event->topic_len == strlen(topic_settings) &&
                strncmp(event->topic, topic_settings, event->topic_len) == 0) {
                if (!parse_message_setting(msg, event->data_len)) ESP_LOGE(TAG, "- ERROR: Fallo el parseo settings -");
                }
            else if (event->topic_len == strlen(topic_handshake) &&
                     strncmp(event->topic, topic_handshake, event->topic_len) == 0) {
                if (!parse_message_setting_ok(msg, event->data_len)) ESP_LOGE(TAG, "- ERROR: Fallo el parseo handshake -");
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

        /* ---- Last Will Testament ----
         * Si el ESP32 se desconecta inesperadamente, el broker publicará "offline" en el tópico status con QoS 1 y
         * retain (permanece hasta nueva publicación)
         */
        mqtt.config.session.last_will.topic = "/devices/esp32/status";
        mqtt.config.session.last_will.msg = "offline";
        mqtt.config.session.last_will.qos = 1;
        mqtt.config.session.last_will.retain = true;
        mqtt.config.network.reconnect_timeout_ms = 5000;  // Esperar 5 seg entre intentos de reconexion
        mqtt.client = esp_mqtt_client_init(&mqtt.config);
        if (!mqtt.client) {
            ESP_LOGI(TAG, "- ERROR -");
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
 * @param topic
 * @param payload
 * @param qos
 * @param retain
 * @return Retorna ESP_OK si no hubo fallas en la configuracion, sino ESP_FAIL.
 */
esp_err_t mqtt_publish(const char *topic, const char *payload, int len, int qos, int retain) {
    if (!mqtt.client) return ESP_FAIL;

    int msg_id = esp_mqtt_client_publish(mqtt.client, topic,payload,
                                         len, qos, retain);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

