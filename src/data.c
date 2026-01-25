#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "KY037/ky037.h"
#include "Setting/settings.h"
#include "MQTT/mqtt.h"
#include "Time/time.h"
#include "esp_log.h"
#include "System/system.h"
#include "components/mpack/include/mpack.h"



typedef struct {
    uint32_t ky037_counter;         // Contador de detecciones del microfono
    uint32_t ky037_max_duration;    // Maxima duracion de pulso de microfono
    uint8_t dht11_temperature;      // Parte entera de temperatura
    uint8_t dht11_humidity;         // Parte entera de humedad
    float co2ppm;
    char time[TIME_MAX_LEN];
} data_sensors_t;


/**
 * @brief Genera una cadena JSON con los datos del dispositivo y sensores.
 *
 */
static bool generate_mpack_data(data_sensors_t data, mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_DATA_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char device[DEVICE_NAME];
    char ip[WIFI_IP];
    uint8_t ssid[WIFI_SSID];

    settings_get_node_mac(mac, sizeof(mac));
    settings_get_node_device_name(device, sizeof(device));
    settings_get_wifi_ip(ip, sizeof(ip));
    settings_get_wifi_ssid(ssid, sizeof(ssid));

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    // El mapa tiene 13 elementos
    mpack_start_map(&writer, 13);

    mpack_write_cstr(&writer, "ID");                mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_type");  mpack_write_cstr(&writer, "SERVER");
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "SERVER0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_cstr(&writer, data.time);
    mpack_write_cstr(&writer, "device_name");       mpack_write_cstr(&writer, device);
    mpack_write_cstr(&writer, "ipv4");              mpack_write_cstr(&writer, ip);
    mpack_write_cstr(&writer, "wifi_ssid");         mpack_write_cstr(&writer, (const char*)ssid);
    mpack_write_cstr(&writer, "pulse_counter");     mpack_write_u32(&writer, (uint32_t)data.ky037_counter);
    mpack_write_cstr(&writer, "pulse_max_duration");mpack_write_u32(&writer, (uint32_t)data.ky037_max_duration);
    mpack_write_cstr(&writer, "temperature");       mpack_write_u8(&writer, data.dht11_temperature);
    mpack_write_cstr(&writer, "humidity");          mpack_write_u8(&writer, data.dht11_humidity);
    mpack_write_cstr(&writer, "co2_ppm");           mpack_write_float(&writer, data.co2ppm);
    mpack_write_cstr(&writer, "sample");            mpack_write_u32(&writer, settings_get_node_sample_rate());

    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}


/**
 * @brief  Recolecta la informacion en el parametro data
 *
 * @param data Puntero de tipo data_sensors_t que recibe la informacion. Para no generar una
 * copia de este campo, se usa un puntero y de esa forma buscar mas eficiencia.
 */
static void get_formated_data(dht11_data_t *dht11, ky037_t *ky037, mq135_data_t *mq135, data_sensors_t *data) {
    memset(data, 0, sizeof(*data));
    get_time(data->time);

    if (xQueueReceive(queues.dht11_buffer, dht11, 0)) {
        data->dht11_temperature = dht11_get_temperature(dht11);
        data->dht11_humidity = dht11_get_humidity(dht11);
    }

    if (xQueueReceive(queues.ky037_buffer, ky037, 0)) {
        data->ky037_counter = ky037_get_counter(*ky037);
        data->ky037_max_duration = ky037_get_duration(*ky037);
    }

    if (xQueueReceive(queues.mq135_buffer, mq135, 0)) {
        data->co2ppm = mq135->co2ppm;
    }
}


/**
 * @brief  Tarea que lee los sensores, formatea los datos y los encola mientras haya espacio en la misma.
 *
 * @param pvParameter
 */
void data_collection_task(void *pvParameter) {
    static data_sensors_t data;
    static dht11_data_t dht11;
    static ky037_t ky037;
    static mq135_data_t mq135;
    mqtt_packet_t packet;

    while (1) {
        xEventGroupWaitBits(
            event_group.collector_events,
            ALL_DATA_READY,
            pdTRUE,  // Limpiar bits despues de leer
            pdTRUE,  // Esperar todos los bits
            pdMS_TO_TICKS(portMAX_DELAY)
        );
        get_formated_data(&dht11, &ky037, &mq135, &data);
        if (generate_mpack_data(data, &packet)) {
            if (xQueueSend(queues.data_buffer, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW("Data", "- INFO: Cola llena, descartando paquete -");
                free(packet.payload);
            }
        } else {
            ESP_LOGE("Data", "- ERROR: Fallo al generar paquete (RAM) -");
        }
    }
}


/**
 * @brief  Tarea que lee los datos de la cola y los publica al broker mientras la conexion
 * este activa.
 *
 * @param pvParameter
 */
void data_publish_task(void *pvParameter) {
    mqtt_packet_t packet_data;
    mqtt_packet_t packet_monitor;
    mqtt_packet_t packet_settings;
    mqtt_packet_t packet_alert;

    char topic_data[MAX_TOPIC];
    char topic_monitor[MAX_TOPIC];
    char topic_settings[MAX_TOPIC];
    char topic_alert[MAX_TOPIC];

    settings_get_mqtt_topic_data(topic_data, sizeof(topic_data));
    settings_get_mqtt_topic_monitor(topic_monitor, sizeof(topic_monitor));
    settings_get_mqtt_topic_settings(topic_settings, sizeof(topic_settings));
    //settings_get_mqtt_topic_alert(topic_alert, sizeof(topic_alert));

    while (1) {
        bool did_work = false;
        EventBits_t bits = xEventGroupGetBits(event_group.mqtt_event_group);
        if ((bits & MQTT_CONNECTED_BIT) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (xQueueReceive(queues.data_buffer, &packet_data, 0)) {
            mqtt_publish(topic_data, packet_data.payload, (int)packet_data.len, 2, 0);
            free(packet_data.payload);
            did_work = true;
        }
        if (xQueueReceive(queues.monitor_buffer, &packet_monitor, 0)) {
            mqtt_publish(topic_monitor, packet_monitor.payload, (int)packet_monitor.len, 2, 0);
            free(packet_monitor.payload);
            did_work = true;
        }
        if (xQueueReceive(queues.settings_buffer, &packet_settings, 0)) {
            mqtt_publish(topic_settings, packet_settings.payload, (int)packet_settings.len, 2, 0);
            free(packet_settings.payload);
            did_work = true;
        }
        if (xQueueReceive(queues.alert_buffer, &packet_alert, 0)) {
            mqtt_publish(topic_alert, packet_alert.payload, (int)packet_alert.len, 2, 0);
            free(packet_alert.payload);
            did_work = true;
        }
        if (!did_work) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}