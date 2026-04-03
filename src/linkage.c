#include "Linkage/linkage.h"
#include <esp_log.h>
#include <esp_random.h>
#include "Fsm/fsm.h"
#include "Message/message.h"
#include "MQTT/mqtt.h"
#include "System/system.h"


static const char *TAG = "LINKAGE";


/**
 * @brief Genera número aleatorio entre 0..N
 * @param N Valor máximo inclusive
 * @return Número aleatorio 0..N
 *
 * Para N=5: devuelve 0,1,2,3,4,5 con distribución uniforme
 */
static uint32_t random_jitter(const uint32_t N) {
    const uint64_t product = (uint64_t)esp_random() * N;
    return (uint32_t)(product >> 32);
}


void linkage_task(void *pvParameter) {
    mqtt_msg_general_t packet;
    uint32_t notification = 0;

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &notification, portMAX_DELAY);

        if (notification & NOTIFY_CMD_START) {
            ESP_LOGI(TAG, "- INFO: Tarea de envio de mensajes activa -");
            bool running = true;

            while (running) {
                const TickType_t loop_delay = pdMS_TO_TICKS(10000 + random_jitter(10)*1000);

                if (generate_message_linkage_request(&packet)) {
                    if (xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                        ESP_LOGW(TAG, "- WARNING: Cola llena, descartando paquete -");
                        free(packet.payload);
                    }
                } else {
                    ESP_LOGE(TAG, "- ERROR: Problema en RAM al generar paquete -");
                }

                uint32_t signal = 0;
                const BaseType_t result = xTaskNotifyWait(0, ULONG_MAX, &signal, loop_delay);

                if (result == pdTRUE) {
                    if (signal & NOTIFY_CMD_STOP) {
                        ESP_LOGI(TAG, "- INFO: Orden de PAUSA recibida. Deteniendo envíos -");
                        running = false;
                    }
                }
            }
        }
    }
}
