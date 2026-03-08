/**
* @file message.c
 * @brief Implementación de serialización y deserialización MPack.
 */


#include "Message/message.h"
#include <esp_log.h>
#include "OTA/ota.h"
#include "Time/time.h"
#include "MQTT/mqtt.h"
#include "mpack.h"
#include "Data/data.h"
#include "DHT11/dht11.h"
#include "Fsm/fsm.h"
#include "MQ135/mq135.h"
#include "Monitor/monitor.h"
#include "Setting/settings.h"
#include "System/system.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "driver/rtc_io.h"
#include "KY037/ky037.h"


static const char *TAG = "MESSAGE";


/**
 * @brief Genera un paquete MPack con los datos de los sensores.
 * Asigna memoria dinámica para el payload que debe ser liberada por el llamador.
 *
 * @param data Estructura con los valores de sensores.
 * @param packet Puntero a la estructura donde se guardará el payload y longitud.
 * @return true si se generó correctamente, false si hubo error de memoria.
 */
bool generate_message_data(const data_sensors_t data, mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_DATA_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char network[ID_NETWORK];

    settings_get_node_mac(mac, sizeof(mac));
    settings_get_network(network, sizeof(network));

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 8);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, data.time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, network);
    mpack_write_u32(&writer, (uint32_t)data.ky037_counter);
    mpack_write_u32(&writer, (uint32_t)data.ky037_max_duration);
    mpack_write_float(&writer, (float)data.dht11_temperature);
    mpack_write_float(&writer, (float)data.dht11_humidity);
    mpack_write_float(&writer, data.co2ppm);
    mpack_write_u32(&writer, settings_get_node_sample_rate());

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE(TAG, "- ERROR: Error codificando MPack data -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de data -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera mensaje de alerta de calidad de aire (MQ135).
 */
bool generate_message_alert_air(mqtt_packet_t *packet, const mq135_alert_t alert) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_MQ135_ALERT_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char network[ID_NETWORK];
    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 4);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, network);
    mpack_write_float(&writer, alert.co2ppm_i);
    mpack_write_float(&writer, alert.co2ppm_a);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack alert_air -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de alert_air -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera mensaje de alerta de temperatura (DHT11).
 */
bool generate_message_alert_temp(mqtt_packet_t *packet, uint8_t temp_i, uint8_t temp_a) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_DHT11_ALERT_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char network[ID_NETWORK];
    settings_get_node_mac(mac, sizeof(mac));
    settings_get_network(network, sizeof(network));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 4);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, network);
    mpack_write_float(&writer, (float)temp_i);
    mpack_write_float(&writer, (float)temp_a);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("DHT11", "- ERROR: Error codificando MPack alert_temp -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de alert_temp -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera mensaje con estadísticas de monitoreo del sistema (RAM, Stack, Uptime).
 */
bool generate_message_monitor(mqtt_packet_t *packet, stats_monitor_t stats) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_MONITOR_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    uint64_t uptime = (uint64_t) esp_timer_get_time();
    uptime = uptime/1000000ULL;

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 15);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, stats.metadata.mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, stats.metadata.time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, stats.metadata.network);
    mpack_write_u32(&writer, stats.memory.mem_free);
    mpack_write_u32(&writer, stats.memory.mem_free_hm);
    mpack_write_u32(&writer, stats.memory.mem_free_block);
    mpack_write_u32(&writer, stats.memory.mem_free_internal);
    mpack_write_u32(&writer, stats.stack.collector);
    mpack_write_u32(&writer, stats.stack.publisher);
    mpack_write_u32(&writer, stats.stack.ky037);
    mpack_write_u32(&writer, stats.stack.dht11);
    mpack_write_u32(&writer, stats.stack.mq135);
    mpack_write_u32(&writer, stats.stack.monitor);
    mpack_write_str(&writer, (const char*)stats.wifi_stats.ssid, strlen((const char*)stats.wifi_stats.ssid));
    mpack_write_i8(&writer, stats.wifi_stats.rssi);
    mpack_write_u64(&writer, uptime);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Monitor", "- ERROR: Error codificando MPack monitor -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de monitor -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera respuesta de confirmación de configuración recibida.
 */
bool generate_message_setting_ok(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_SETTINGS_OK_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char network[ID_NETWORK];
    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 3);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, network);
    mpack_write_bool(&writer, true);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack setting_ok -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de setting_ok -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera reporte de estado de actualización de firmware (OTA).
 */
bool generate_message_firmware_ok(mqtt_msg_general_t *packet, const bool is_ok) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_FIRMWARE_OK_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    const uint64_t timestamp = get_time();
    settings_get_node_mac(mac, sizeof(mac));

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 3);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, timestamp);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, CURRENT_FIRMWARE_VERSION);
    mpack_write_bool(&writer, is_ok);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack firmware_ok -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de firmware_ok -");
    packet->len = used;
    packet->topic = FIRMWARE_OK;
    return true;
}


/**
 * @brief Genera handshake para sincronización en modo balanceo.
 */
bool generate_message_balance_mode_handshake(mqtt_msg_general_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_HANDSHAKE_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char id_edge[ID_EDGE];
    settings_get_network_id_edge(id_edge, sizeof(id_edge));
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 3);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, id_edge);
    mpack_write_u64(&writer, time);
    mpack_finish_array(&writer);

    const State state = atomic_load(&shared_state);
    if (state == IN_HANDSHAKE) mpack_write_cstr(&writer, "in_handshake");
    else if (state == OUT_HANDSHAKE) mpack_write_cstr(&writer, "out_handshake");
    mpack_write_u32(&writer, settings_get_balance_epoch());

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack handshake -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de handshake -");
    packet->len = used;
    packet->topic = HANDSHAKE;
    return true;
}


/**
 * @brief Genera reporte completo de la configuración actual del nodo.
 */
bool generate_message_settings(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_SETTINGS_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char id_network[ID_NETWORK];
    char mqtt_uri[MQTT_URI];
    char device_name[DEVICE_NAME];
    settings_get_network(id_network, sizeof(id_network));
    settings_get_node_mac(mac, sizeof(mac));
    settings_get_mqtt_uri(mqtt_uri, sizeof(mqtt_uri));
    settings_get_node_device_name(device_name, sizeof(device_name));
    const uint64_t timestamp = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 8);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "server0");
    mpack_write_u64(&writer, timestamp);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, id_network);
    mpack_write_str(&writer, (const char*)settings.wifi.ssid, strnlen((const char*)settings.wifi.ssid, sizeof(settings.wifi.ssid)));
    mpack_write_str(&writer, (const char*)settings.wifi.password, strnlen((const char*)settings.wifi.password, sizeof(settings.wifi.password)));
    mpack_write_cstr(&writer, mqtt_uri);
    mpack_write_cstr(&writer, device_name);
    mpack_write_u32(&writer, settings_get_node_sample_rate());
    mpack_write_u32(&writer, settings_get_node_energy_mode());

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Settings", "- ERROR: Error codificando MPack settings -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de settings -");
    packet->len = used;
    return true;
}


/**
 * @brief Genera respuesta de confirmación de configuración recibida.
 */
bool generate_message_ping(mqtt_msg_general_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_PING_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char id_network[ID_NETWORK];
    char id_edge[ID_EDGE];
    settings_get_network_id_edge(id_edge, sizeof(id_edge));
    settings_get_network(id_network, sizeof(id_network));
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_array(&writer, 3);

    mpack_start_array(&writer, 3);
    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, id_edge);
    mpack_write_u64(&writer, time);
    mpack_finish_array(&writer);

    mpack_write_cstr(&writer, id_network);
    mpack_write_bool(&writer, true);

    mpack_finish_array(&writer);

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack ping -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de ping -");
    packet->len = used;
    packet->topic = PING;
    return true;
}


/**
 * @brief Genera reporte de colas vacias (usada en Balance y en Safe).
 */
bool generate_message_empty_queue(mqtt_msg_general_t *packet, const State current_phase) {
    packet->payload = NULL;
    packet->len = 0;
    const size_t buffer_size = MPACK_EMPTY_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    char id_edge[ID_EDGE];
    settings_get_node_mac(mac, sizeof(mac));
    settings_get_network_id_edge(id_edge, sizeof(id_edge));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    if (current_phase == SAFE_MODE) {
        mpack_start_array(&writer, 3);

        mpack_start_array(&writer, 3);
        mpack_write_cstr(&writer, mac);
        mpack_write_cstr(&writer, id_edge);
        mpack_write_u64(&writer, time);
        mpack_finish_array(&writer);

        mpack_write_cstr(&writer, "balance_mode");
        mpack_write_bool(&writer, true);

        mpack_finish_array(&writer);
    }
    else {
        mpack_start_array(&writer, 4);

        mpack_start_array(&writer, 3);
        mpack_write_cstr(&writer, mac);
        mpack_write_cstr(&writer, id_edge);
        mpack_write_u64(&writer, time);
        mpack_finish_array(&writer);

        mpack_write_cstr(&writer, "balance_mode");

        if (current_phase == ALERT) {
            mpack_write_cstr(&writer, "alert");
            mpack_write_bool(&writer, true);
        }
        if (current_phase == DATA) {
            mpack_write_cstr(&writer, "data");
            mpack_write_bool(&writer, true);
        }
        if (current_phase == MONITOR) {
            mpack_write_cstr(&writer, "monitor");
            mpack_write_bool(&writer, true);
        }

        mpack_finish_array(&writer);
    }

    const size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack empty_queue -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    ESP_LOGI(TAG, "- OK: Serializacion correcta de empty_queue -");
    packet->len = used;
    packet->topic = QUEUE_EMPTY;
    return true;
}


/* --- Funciones de Parseo (Deserialización) --- */


/**
 * @brief Parsea mensaje de cambio a estado NORMAL.
 */
bool parse_message_state_normal(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 2) {
        return false;
    }

    char id_edge[ID_EDGE];
    char buffer[32];

    // Variables de validación temporal
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "normal") == 0) state_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje state_normal -");
        return false;
    }

    if (sender_ok && dest_ok && state_ok) {
        const uint32_t flag = STATE_NORMAL;
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de state_normal -");
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
    }

    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea mensaje de cambio a estado BALANCE.
 * Extrae epoch y duración.
 */
bool parse_message_state_balance(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 5) {
        return false;
    }

    char id_edge[MAC];
    char buffer[32];
    uint32_t epoch = 0;
    uint32_t duration = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool balance_ok = false;
    bool duration_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "balance_mode") == 0) state_ok = true;

    epoch = mpack_expect_u32(&reader);
    balance_ok = true;

    duration = mpack_expect_u32(&reader);
    duration_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje state_balance -");
        return false;
    }

    if (sender_ok && dest_ok && state_ok && balance_ok && duration_ok) {
        settings_set_balance_epoch(epoch);
        setting_save_to_nvs(); // Guardamos la nueva época dictada por el Edge

        atomic_store(&balance.duration, duration);
        atomic_store(&balance.balance, epoch);

        const uint32_t flag = STATE_BALANCE_MODE;
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de state_balance -");
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
    }

    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea mensaje de cambio a estado SAFE.
 * Extrae duración, frecuencia y jitter.
 */
bool parse_message_state_safe(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 4) {
        return false;
    }

    char id_edge[MAC];
    char buffer[32];
    uint32_t frequency = 0;
    uint32_t jitter = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool frequency_ok = false;
    bool jitter_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "safe_mode") == 0) state_ok = true;

    frequency = mpack_expect_u32(&reader);
    frequency_ok = true;

    jitter = mpack_expect_u32(&reader);
    jitter_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje state_safe_mode -");
        return false;
    }

    if (sender_ok && dest_ok && state_ok && frequency_ok && jitter_ok) {
        atomic_store(&safe_mode.frequency, frequency);
        atomic_store(&safe_mode.jitter, jitter);
        const uint32_t flag = STATE_SAFE_MODE;
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de state_safe_mode -");
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea configuración de fase (Alert, Data, Monitor).
 */
bool parse_message_phase(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 6) {
        return false;
    }

    char id_edge[MAC];
    char buffer[32];
    uint32_t bal = 0;
    uint32_t frequency = 0;
    uint32_t jitter = 0;
    uint8_t phase_number = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool frequency_ok = false;
    bool balance_ok = false;
    bool jitter_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "balance_mode") == 0) state_ok = true;

    bal = mpack_expect_u32(&reader);
    balance_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "alert") == 0) {
        phase_number = FLAG_PHASE_ALERT;
    }
    if (strcmp(buffer, "data") == 0) {
        phase_number = FLAG_PHASE_DATA;
    }
    if (strcmp(buffer, "monitor") == 0) {
        phase_number = FLAG_PHASE_MONITOR;
    }

    frequency = mpack_expect_u32(&reader);
    frequency_ok = true;

    jitter = mpack_expect_u32(&reader);
    jitter_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje phase -");
        return false;
    }

    if (sender_ok && dest_ok && state_ok && frequency_ok && balance_ok && jitter_ok) {
        const uint32_t epoch = atomic_load(&phase.balance);
        const uint32_t diff = epoch - bal;
        if (diff == 0) {
            atomic_store(&phase.frequency, frequency);
            atomic_store(&phase.jitter, jitter);
            if (phase_number == FLAG_PHASE_ALERT) {
                const uint32_t flag = PHASE_ALERT;
                ESP_LOGI(TAG, "- OK: Decodificacion correcta de phase_alert -");
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
            if (phase_number == FLAG_PHASE_DATA) {
                const uint32_t flag = PHASE_DATA;
                ESP_LOGI(TAG, "- OK: Decodificacion correcta de phase_data -");
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
            if (phase_number == FLAG_PHASE_MONITOR) {
                const uint32_t flag = PHASE_MONITOR;
                ESP_LOGI(TAG, "- OK: Decodificacion correcta de phase_monitor -");
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
        }
        else if (diff < 0x80000000UL) {   // 0x80000000 es 2^31
            atomic_store(&phase.balance, bal);
            atomic_store(&phase.frequency, frequency);
            atomic_store(&phase.jitter, jitter);
            const uint32_t flag = NEWER_EPOCH;
            ESP_LOGI(TAG, "- OK: Decodificacion correcta. Mensaje con epoch mas nuevo -");
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            settings_set_balance_epoch(epoch);
            setting_save_to_nvs();
        }
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea mensaje de Handshake del servidor.
 */
bool parse_message_handshake(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 3) {
        return false;
    }

    char id_edge[MAC];
    char buffer[32];
    uint32_t epoch = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool balance_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "in") == 0 || strcmp(buffer, "out") == 0) dest_ok = true;

    epoch = mpack_expect_u32(&reader);
    balance_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje handshake -");
        return false;
    }

    if (sender_ok && dest_ok && balance_ok) {
        const uint32_t bal = settings_get_balance_epoch();
        const uint32_t diff = epoch - bal;
        if (diff == 0) {
            const uint32_t flag = HANDSHAKE_REQUEST;
            ESP_LOGI(TAG, "- OK: Decodificacion correcta de handshake -");
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
        }
        else if (diff < 0x80000000UL) {   // 0x80000000 es 2^31
            atomic_store(&balance.balance, bal);
            const uint32_t flag = NEWER_EPOCH;
            ESP_LOGI(TAG, "- OK: Decodificacion correcta. Mensaje con epoch mas nuevo -");
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            settings_set_balance_epoch(epoch);
            setting_save_to_nvs();
        }
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea mensaje de Heartbeat (latido) del servidor.
 */
bool parse_message_heartbeat(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 2) {
        return false;
    }

    char id_edge[MAC];
    char buffer[32];
    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    bool sender_ok = false;
    bool dest_ok = false;
    bool beat_ok = false;

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, id_edge) == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "all") == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    const bool beat_val = mpack_expect_bool(&reader);
    if (beat_val == true) beat_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje heartbeat -");
        return false;
    }

    if (sender_ok && dest_ok && beat_ok) {
        const uint32_t flag = HEARTBEAT_INCOMING;
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de heartbeat -");
        xQueueSend(queues.heartbeat, &flag, pdMS_TO_TICKS(100));
    }

    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea comando de actualización de firmware (OTA).
 */
bool parse_message_new_firmware(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 2) {
        return false;
    }

    char network[ID_NETWORK];
    char buffer[32];
    char mac[MAC];
    bool sender_ok = false;
    bool dest_ok = false;
    bool network_ok = false;

    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "server0") == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, mac) == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, network) == 0) network_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje new_firmware -");
        return false;
    }

    if (sender_ok && dest_ok && network_ok) {
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de new_firmware -");
        esp_restart();
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea nueva configuración completa (WiFi, MQTT, Sampling).
 * Guarda los cambios en NVS si la validación es exitosa.
 */
bool parse_message_setting(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 8) {
        return false;
    }

    char buffer[65];
    char mac[MAC];
    char id_network[ID_NETWORK];
    char wifi_ssid[WIFI_SSID];
    char wifi_password[WIFI_PASSWORD];
    char mqtt_uri[MQTT_URI];
    char device[DEVICE_NAME];
    uint32_t sample = 0;
    energy_mode_t energy_mode = 0;
    bool sender_ok = false;
    bool apply = false;

    settings_get_node_mac(mac, sizeof(mac));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "server0") == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, mac) == 0) apply = true;
    if (strcmp(buffer, "all") == 0) apply = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    safe_strcpy(id_network, buffer, sizeof(id_network));

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    safe_strcpy(wifi_ssid, buffer, sizeof(wifi_ssid));

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    safe_strcpy(wifi_password, buffer, sizeof(wifi_password));

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    safe_string_copy(mqtt_uri, buffer, sizeof(mqtt_uri));

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    safe_string_copy(device, buffer, sizeof(device));

    sample = mpack_expect_u32(&reader);

    energy_mode = mpack_expect_u8(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje setting -");
        return false;
    }

    if (sender_ok && apply) {
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de setting -");
        safe_strcpy(settings.network.id_network, id_network, sizeof(settings.network.id_network));
        safe_strcpy((char *)settings.wifi.ssid, wifi_ssid, sizeof(settings.wifi.ssid));
        safe_strcpy((char *)settings.wifi.password, wifi_password, sizeof(settings.wifi.password));
        safe_string_copy(settings.mqtt.uri, mqtt_uri, sizeof(settings.mqtt.uri));
        safe_string_copy(settings.node.device_name, device, sizeof(settings.node.device_name));
        settings.node.sample_rate = sample;
        settings.node.energy_mode = energy_mode;
        const esp_err_t ret = setting_save_to_nvs();
        if (ret != ESP_OK) {
            return false;
        }
    }
    return true;
}


/**
 * @brief Parsea confirmación de recepción de configuración (Setting OK).
 */
bool parse_message_setting_ok(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 3) {
        return false;
    }

    char buffer[32];
    char network[ID_NETWORK];
    char mac[MAC];
    bool sender_ok = false;
    bool dest_ok = false;
    bool hand_ok = false;
    bool net_ok = false;

    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "server0") == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, mac) == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, network) == 0) net_ok = true;

    hand_ok = mpack_expect_bool(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje setting_ok -");
        return false;
    }

    if (sender_ok && dest_ok && hand_ok && net_ok) {
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de setting_ok -");
        if (task_handle.send_settings_handle != NULL) {
            xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_DESTROY, eSetBits);
        }
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea comando de borrado lógico (de la red) del Hub.
 * Además, setea el Hub en modo deep sleep. Por lo que solo se activará
 * nuevamente con un reset físico.
 */
bool parse_message_delete(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t heartbeat_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || heartbeat_array_size != 2) {
        return false;
    }

    char network[ID_NETWORK];
    char mac[MAC];
    char buffer[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool net_ok = false;

    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "server0") == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, mac) == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, network) == 0) net_ok = true;

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje delete -");
        return false;
    }

    if (sender_ok && dest_ok && net_ok) {
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de delete -");
        settings_empty_network();
        setting_save_to_nvs();
        esp_wifi_stop();
        rtc_gpio_isolate(DHT11_PIN);
        rtc_gpio_isolate(KY037_PIN);
        esp_deep_sleep_start();
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}


/**
 * @brief Parsea comando de activación/desactivación del Hub.
 */
bool parse_message_active(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t active_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || active_array_size != 3) {
        return false;
    }

    char network[ID_NETWORK];
    char buffer[32];
    char mac[MAC];
    bool sender_ok = false;
    bool dest_ok = false;
    bool net_ok = false;
    bool is_active = false;

    settings_get_network(network, sizeof(network));
    settings_get_node_mac(mac, sizeof(mac));

    const uint32_t meta_array_size = mpack_expect_array(&reader);
    if (mpack_reader_error(&reader) != mpack_ok || meta_array_size != 3) {
        return false;
    }

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, "server0") == 0) sender_ok = true;

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, mac) == 0) dest_ok = true;

    mpack_expect_i64(&reader);

    mpack_expect_cstr(&reader, buffer, sizeof(buffer));
    if (strcmp(buffer, network) == 0) net_ok = true;

    is_active = mpack_expect_bool(&reader);

    if (mpack_reader_error(&reader) != mpack_ok) {
        mpack_reader_destroy(&reader);
        ESP_LOGE(TAG, "- ERROR: No se pudo decodificar mensaje active -");
        return false;
    }

    if (sender_ok && dest_ok && net_ok) {
        ESP_LOGI(TAG, "- OK: Decodificacion correcta de active -");
        if (!is_active) {
            xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
            xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
            xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
            xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
            if (task_handle.send_settings_handle != NULL) {
                xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
            }
        }
        if (is_active) {
            xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
            xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
            xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
            xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
            if (task_handle.send_settings_handle != NULL) {
                xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
            }
        }
    }
    return (mpack_reader_destroy(&reader) == mpack_ok);
}