#include "MQTT/mqtt.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MQTT";


/* ----- Inicializacion ----- */
void mqtt_client_init(mqtt_client_t *mqtt, const char *uri,
                      const char *user, const char *pass) {
    memset(mqtt, 0, sizeof(mqtt_client_t));   // inicializa la estructura mqtt con ceros
    mqtt->config.broker.address.uri = uri;  // define la dirección del broker MQTT

    if (user && strlen(user) > 0) {     // configurar usuario si se proporciona
        mqtt->config.credentials.username = user;
    }

    if (pass && strlen(pass) > 0) {   // configurar contrasena si se proporciona
        mqtt->config.credentials.authentication.password = pass;
    }

    mqtt->client = NULL;   // inicializa el puntero al cliente MQTT como NULL
    mqtt->reconnecting = pdFALSE;   // controla si se esta en modo reconexion
}


/* ----- Callback principal -----
 * Es la funcion que se ejecuta cuando ocurren eventos en el cliente MQTT */
esp_err_t mqtt_event_handler_cb(mqtt_client_t *mqtt,
                                esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al broker");
            mqtt->reconnecting = pdFALSE;
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Desconectado del broker");
            if (mqtt->reconnecting == pdFALSE) {
                mqtt->reconnecting = pdTRUE;
                xTaskCreate(mqtt_reconnect_task, "mqtt_reconnect_task",
                            4096, mqtt, 5, NULL);
            }
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Publicado msg_id=%d", event->msg_id);
            break;

        default:   // ignorar otros eventos
            break;
    }

    return ESP_OK;
}


/* ----- Callback puente -----
 * Esta funcion hace de “puente”: convierte los parametros (void*) en el tipo correcto y llama al callback real mqtt_event_handler_cb */
void mqtt_event_handler(void *handler_args,
                        esp_event_base_t base,
                        int32_t event_id,
                        void *event_data) {
    mqtt_client_t *mqtt = (mqtt_client_t *)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    mqtt_event_handler_cb(mqtt, event);
}


/* ----- Start ----- */
esp_err_t mqtt_client_start(mqtt_client_t *mqtt) {
    mqtt->client = esp_mqtt_client_init(&mqtt->config);
    if (!mqtt->client) return ESP_FAIL;

    // registramos el callback y pasamos mqtt como handler_args
    esp_mqtt_client_register_event(mqtt->client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   mqtt);

    return esp_mqtt_client_start(mqtt->client);
}


/* ----- Publish ----- */
esp_err_t mqtt_client_publish(mqtt_client_t *mqtt,
                              const char *topic,
                              const char *payload,
                              int qos, int retain) {
    if (!mqtt->client) return ESP_FAIL;

    int msg_id = esp_mqtt_client_publish(mqtt->client,
                                         topic,
                                         payload,
                                         0, qos, retain);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}


/* ----- Tarea de reconexión ----- */
void mqtt_reconnect_task(void *arg) {
    mqtt_client_t *mqtt = (mqtt_client_t *)arg;

    while (mqtt->reconnecting == pdTRUE) {
        ESP_LOGI(TAG, "Intentando reconectar al broker...");
        esp_err_t err = esp_mqtt_client_reconnect(mqtt->client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Reconexión exitosa");
            mqtt->reconnecting = pdFALSE;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));   // espera 5 segundos
    }

    vTaskDelete(NULL);
}


/* ----- Tarea principal ----- */
/*void mqtt_task(void *pvParam) {
    mqtt_client_t *mqtt = (mqtt_client_t *)pvParam;
    char buffer[MAX_BUFFER_SIZE];

    while (1) {
        // Espera datos de sensores
        if (xQueueReceive(data_formated, &buffer, portMAX_DELAY)) {
            if (mqtt_client_publish(mqtt, "sensor/data", buffer, 1, 0) != ESP_OK) {
                ESP_LOGW(TAG, "Error publicando mensaje MQTT");
            } else {
                ESP_LOGI(TAG, "Mensaje publicado: %s", buffer);
            }
        }
    }
}*/
