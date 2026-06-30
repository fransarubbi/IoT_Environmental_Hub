/**
* @file https_bypass.c
 * @brief Implementación del cliente HTTPS para modo de emergencia.
 */

#include "Https_bypass/https_bypass.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "Fsm/fsm.h"
#include "MQTT/mqtt.h"
#include "System/system.h"


static const char *TAG = "Bypass";


/**
 * @brief Crea una conexión HTTPS efímera, envía el paquete y libera recursos.
 *
 * Esta función encapsula todo el ciclo de vida de una petición HTTP segura:
 * 1. Inicializa el cliente con el certificado CA del servidor.
 * 2. Configura los headers (Content-Type: application/x-msgpack).
 * 3. Realiza la petición POST (Bloqueante hasta timeout).
 * 4. Limpia el cliente HTTP.
 * 5. Libera la memoria dinámica del payload del paquete.
 *
 * @note Esta función es bloqueante. El tiempo de ejecución depende de la red
 * y del timeout configurado (5 seg).
 *
 * @warning Es responsabilidad de esta función liberar `packet->payload` mediante `free()`,
 * ya que la memoria fue reservada en la etapa de generación del mensaje (message.c).
 *
 * @param packet Puntero a la estructura `mqtt_packet_t` con el payload y longitud.
 */
static void create_https_send_and_delete(const mqtt_packet_t *packet) {

    char url[URL_HTTPS];
    settings_get_url_https(url, sizeof(url));

    const esp_http_client_config_t config = {
        .url = url,   
        .cert_pem = NULL,  
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client) {
        esp_http_client_set_header(client, "Content-Type", "application/x-msgpack");
        esp_http_client_set_post_field(client, packet->payload, packet->len);

        const esp_err_t err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Info: alerta enviada. Status: %d",
                     esp_http_client_get_status_code(client));
        } else {
            ESP_LOGE(TAG, "Error: fallo envío %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
    }

    if (packet->payload != NULL) {
        free(packet->payload);
    }
}


/**
 * @brief Tarea Worker para el modo Bypass.
 *
 * Ciclo de vida:
 * 1. Espera pasiva (Blocked) hasta recibir `NOTIFY_CMD_START`.
 * 2. Entra en bucle de procesamiento (`running = true`).
 * 3. Consume alertas de las colas de Temperatura y Aire.
 * 4. Delega el envío a `create_https_send_and_delete`.
 * 5. Verifica señales de `NOTIFY_CMD_STOP` para salir del modo Bypass y volver a dormir.
 *
 * @note Se recomienda usar recepción no bloqueante (timeout 0) en las colas internas
 * para evitar deadlocks si se recibe la orden de STOP mientras las colas están vacías.
 *
 * @param pvParam Parámetros de tarea.
 */
void https_bypass_task(void *pvParam) {
    mqtt_packet_t packet;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            bool running = true;

            while (running) {
                bool worked = false;

                if (xQueueReceive(queues.alert_temp_buffer, &packet, 0) == pdTRUE) {
                    create_https_send_and_delete(&packet);
                    worked = true;
                }

                if (xQueueReceive(queues.alert_air_buffer, &packet, 0) == pdTRUE) {
                    create_https_send_and_delete(&packet);
                    worked = true;
                }

                uint32_t stop_signal = 0;
                const TickType_t wait_time = worked ? 0 : pdMS_TO_TICKS(100);
                const BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &stop_signal, wait_time);

                if (result == pdTRUE) {
                    if (stop_signal & NOTIFY_CMD_STOP) {
                        running = false;
                    }
                }
            }
        }
    }
}