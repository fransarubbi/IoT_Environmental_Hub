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


/**
 * @brief Genera un paquete MPack con los datos de los sensores.
 * Asigna memoria dinámica para el payload que debe ser liberada por el llamador.
 *
 * @param data Estructura con los valores de sensores.
 * @param packet Puntero a la estructura donde se guardará el payload y longitud.
 * @return true si se generó correctamente, false si hubo error de memoria.
 */
bool generate_message_data(data_sensors_t data, mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_DATA_SIZE;
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

    mpack_start_map(&writer, 10);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, data.time);
    mpack_write_cstr(&writer, "network");           mpack_write_cstr(&writer, network);
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
 * @brief Genera mensaje de alerta de calidad de aire (MQ135).
 */
bool generate_message_alert_air(mqtt_packet_t *packet, mq135_alert_t alert) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_MQ135_ALERT_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 6);
    mpack_write_cstr(&writer, "ID");                mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_type");  mpack_write_cstr(&writer, "SERVER");
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "SERVER0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, time);
    mpack_write_cstr(&writer, "co2_ppm_initial");   mpack_write_float(&writer, alert.co2ppm_i);
    mpack_write_cstr(&writer, "co2_ppm_rightnow");  mpack_write_float(&writer, alert.co2ppm_a);
    mpack_finish_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }

    packet->len = mpack_writer_buffer_used(&writer);
    return true;
}


/**
 * @brief Genera mensaje de alerta de temperatura (DHT11).
 */
bool generate_message_alert_temp(mqtt_packet_t *packet, uint8_t temp_i, uint8_t temp_a) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_DHT11_ALERT_SIZE;
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

    mpack_start_map(&writer, 6);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, time);
    mpack_write_cstr(&writer, "network");           mpack_write_cstr(&writer, network);
    mpack_write_cstr(&writer, "temp_initial");      mpack_write_u8(&writer, temp_i);
    mpack_write_cstr(&writer, "temp_rightnow");     mpack_write_u8(&writer, temp_a);
    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("DHT11", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}


/**
 * @brief Genera mensaje con estadísticas de monitoreo del sistema (RAM, Stack, Uptime).
 */
bool generate_message_monitor(mqtt_packet_t *packet, stats_monitor_t stats) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_MONITOR_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char time_str[30];
    snprintf(time_str, sizeof(time_str), "%lu:%02lu:%02lu",
             stats.uptime.hours, stats.uptime.minutes, stats.uptime.seconds);

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 18);
    mpack_write_cstr(&writer, "sender_user_id");       mpack_write_cstr(&writer, stats.metadata.mac);
    mpack_write_cstr(&writer, "destination_id");       mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");            mpack_write_u64(&writer, stats.metadata.time);
    mpack_write_cstr(&writer, "network");              mpack_write_cstr(&writer, stats.metadata.network);
    mpack_write_cstr(&writer, "mem_free");             mpack_write_u32(&writer, stats.memory.mem_free);
    mpack_write_cstr(&writer, "mem_free_hm");          mpack_write_u32(&writer, stats.memory.mem_free_hm);
    mpack_write_cstr(&writer, "mem_free_block");       mpack_write_u32(&writer, stats.memory.mem_free_block);
    mpack_write_cstr(&writer, "mem_free_internal");    mpack_write_u32(&writer, stats.memory.mem_free_internal);
    mpack_write_cstr(&writer, "stack_free_min_coll");  mpack_write_u32(&writer, stats.stack.collector);
    mpack_write_cstr(&writer, "stack_free_min_pub");   mpack_write_u32(&writer, stats.stack.publisher);
    mpack_write_cstr(&writer, "stack_free_min_mic");   mpack_write_u32(&writer, stats.stack.ky037);
    mpack_write_cstr(&writer, "stack_free_min_th");    mpack_write_u32(&writer, stats.stack.dht11);
    mpack_write_cstr(&writer, "stack_free_min_air");   mpack_write_u32(&writer, stats.stack.mq135);
    mpack_write_cstr(&writer, "stack_free_min_mon");   mpack_write_u32(&writer, stats.stack.monitor);
    mpack_write_cstr(&writer, "wifi_ssid");            mpack_write_str(&writer, (const char*)stats.wifi_stats.ssid, strlen((const char*)stats.wifi_stats.ssid));
    mpack_write_cstr(&writer, "wifi_rssi");            mpack_write_i8(&writer, stats.wifi_stats.rssi);
    mpack_write_cstr(&writer, "energy_mode");          mpack_write_u8(&writer, stats.energy_mode);
    mpack_write_cstr(&writer, "active_time");          mpack_write_cstr(&writer, time_str);

    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Monitor", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}


/**
 * @brief Genera respuesta de confirmación de configuración recibida.
 */
bool generate_message_setting_ok(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_SETTINGS_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 4);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, time);
    mpack_write_cstr(&writer, "handshake");         mpack_write_bool(&writer, true);
    mpack_finish_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }

    packet->len = mpack_writer_buffer_used(&writer);
    return true;
}


/**
 * @brief Genera reporte de estado de actualización de firmware (OTA).
 */
bool generate_message_firmware_ok(mqtt_packet_t *packet, const bool is_ok) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_DATA_SIZE;
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

    mpack_start_map(&writer, 5);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, timestamp);
    mpack_write_cstr(&writer, "version");           mpack_write_cstr(&writer, CURRENT_FIRMWARE_VERSION);
    mpack_write_cstr(&writer, "is_ok");             mpack_write_bool(&writer, is_ok);

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
 * @brief Genera handshake para sincronización en modo balanceo.
 */
bool generate_message_balance_mode_handshake(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_SETTINGS_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    char mac[MAC];
    settings_get_node_mac(mac, sizeof(mac));
    const uint64_t time = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 4);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, mac);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, time);
    mpack_write_cstr(&writer, "balance_epoch");     mpack_write_u32(&writer, 1);
    mpack_finish_map(&writer);

    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Data", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }

    packet->len = mpack_writer_buffer_used(&writer);
    return true;
}


/**
 * @brief Genera reporte completo de la configuración actual del nodo.
 */
bool generate_message_settings(mqtt_packet_t *packet) {
    packet->payload = NULL;
    packet->len = 0;
    size_t buffer_size = MPACK_SETTINGS_SIZE;
    packet->payload = malloc(buffer_size);

    if (packet->payload == NULL) {
        ESP_LOGE("Data", "- ERROR: No hay RAM para MPack -");
        return false;
    }

    uint64_t timestamp = get_time();

    mpack_writer_t writer;
    mpack_writer_init(&writer, packet->payload, buffer_size);

    mpack_start_map(&writer, 10);
    mpack_write_cstr(&writer, "sender_user_id");    mpack_write_cstr(&writer, settings.node.mac_address);
    mpack_write_cstr(&writer, "destination_id");    mpack_write_cstr(&writer, "Server0");
    mpack_write_cstr(&writer, "timestamp");         mpack_write_u64(&writer, timestamp);
    mpack_write_cstr(&writer, "network");           mpack_write_cstr(&writer, settings.network.id_network);
    mpack_write_cstr(&writer, "wifi_ssid");         mpack_write_str(&writer, (const char*)settings.wifi.ssid, strnlen((const char*)settings.wifi.ssid, sizeof(settings.wifi.ssid)));
    mpack_write_cstr(&writer, "wifi_password");     mpack_write_str(&writer, (const char*)settings.wifi.password, strnlen((const char*)settings.wifi.password, sizeof(settings.wifi.password)));
    mpack_write_cstr(&writer, "mqtt_uri");          mpack_write_cstr(&writer, settings.mqtt.uri);
    mpack_write_cstr(&writer, "device_name");       mpack_write_cstr(&writer, settings.node.device_name);
    mpack_write_cstr(&writer, "sample");            mpack_write_u32(&writer, settings.node.sample_rate);
    mpack_write_cstr(&writer, "energy_mode");       mpack_write_u8(&writer, settings.node.energy_mode);

    mpack_finish_map(&writer);

    size_t used = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) != mpack_ok) {
        ESP_LOGE("Settings", "- ERROR: Error codificando MPack -");
        free(packet->payload);
        packet->payload = NULL;
        return false;
    }
    packet->len = used;
    return true;
}


/* --- Funciones de Parseo (Deserialización) --- */


/**
 * @brief Parsea mensaje de cambio a estado NORMAL.
 */
bool parse_message_state_normal(const char* data, const size_t len) {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) return false;

    char id_edge[MAC];
    char key[32];
    char value[32];

    // Variables de validación temporal
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "state") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "normal") == 0) state_ok = true;
        }
        else {
            mpack_discard(&reader); // Ignorar claves desconocidas
        }

        if (mpack_reader_error(&reader) != mpack_ok) return false;
    }

    // Validación final conjunta
    if (sender_ok && dest_ok && state_ok) {
        uint32_t flag = STATE_NORMAL;
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char id_edge[MAC];
    char key[32];
    char value[32];
    uint32_t epoch = 0;
    uint32_t duration = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool balance_ok = false;
    bool duration_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "state") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "balance_mode") == 0) state_ok = true;
        }
        else if (strcmp(key, "balance_epoch") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            balance_ok = true;
            epoch = val;
        }
        else if (strcmp(key, "duration") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            duration = val;
            duration_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && state_ok && balance_ok && duration_ok) {
        const uint32_t balance = settings_get_balance_epoch();
        if (balance < epoch) {
            settings_set_balance_epoch(epoch);
            const uint32_t flag = STATE_BALANCE_MODE;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
            atomic_store(&msg_data.duration, duration);
            atomic_store(&msg_data.balance, epoch);
            setting_save_to_nvs();
        } else if (balance == epoch) {
            const uint32_t flag = STATE_BALANCE_MODE;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(10));
            atomic_store(&msg_data.duration, duration);
        }

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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char id_edge[MAC];
    char key[32];
    char value[32];
    uint32_t duration = 0;
    uint32_t frequency = 0;
    uint32_t jitter = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool frequency_ok = false;
    bool duration_ok = false;
    bool jitter_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "state") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "safe_mode") == 0) state_ok = true;
        }
        else if (strcmp(key, "duration") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            duration = val;
            duration_ok = true;
        }
        else if (strcmp(key, "frequency") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            frequency = val;
            frequency_ok = true;
        }
        else if (strcmp(key, "jitter") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            jitter = val;
            jitter_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && state_ok && frequency_ok && duration_ok && jitter_ok) {
        atomic_store(&msg_data.duration, duration);
        atomic_store(&msg_data.frequency, frequency);
        atomic_store(&msg_data.jitter, jitter);
        uint32_t flag = STATE_BALANCE_MODE;
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char id_edge[MAC];
    char key[32];
    char value[32];
    uint32_t balance = 0;
    uint32_t frequency = 0;
    uint32_t jitter = 0;
    uint8_t phase = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool state_ok = false;
    bool frequency_ok = false;
    bool balance_ok = false;
    bool jitter_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "state") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "balance_mode") == 0) state_ok = true;
        }
        else if (strcmp(key, "epoch") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            balance = val;
            balance_ok = true;
        }
        else if (strcmp(key, "phase") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "alert") == 0) {
                phase = FLAG_PHASE_ALERT;
            }
            if (strcmp(value, "data") == 0) {
                phase = FLAG_PHASE_DATA;
            }
            if (strcmp(value, "monitor") == 0) {
                phase = FLAG_PHASE_MONITOR;
            }
        }
        else if (strcmp(key, "frequency") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            frequency = val;
            frequency_ok = true;
        }
        else if (strcmp(key, "jitter") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            jitter = val;
            jitter_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && state_ok && frequency_ok && balance_ok && jitter_ok) {
        const uint32_t epoch = atomic_load(&msg_data.balance);
        if (epoch < balance) {
            atomic_store(&msg_data.balance, balance);
            const uint32_t flag = NEWER_EPOCH;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            settings_set_balance_epoch(epoch);
            setting_save_to_nvs();
        }
        else if (epoch == balance){
            atomic_store(&msg_data.frequency, frequency);
            atomic_store(&msg_data.jitter, jitter);
            if (phase == FLAG_PHASE_ALERT) {
                const uint32_t flag = PHASE_ALERT;
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
            if (phase == FLAG_PHASE_DATA) {
                const uint32_t flag = PHASE_DATA;
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
            if (phase == FLAG_PHASE_MONITOR) {
                const uint32_t flag = PHASE_MONITOR;
                xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            }
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

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char id_edge[MAC];
    char key[32];
    char value[32];
    uint32_t epoch = 0;
    uint32_t duration = 0;
    bool sender_ok = false;
    bool dest_ok = false;
    bool duration_ok = false;
    bool balance_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "balance_epoch") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            epoch = val;
            balance_ok = true;
        }
        else if (strcmp(key, "duration") == 0) {
            const uint32_t val = mpack_expect_u32(&reader);
            duration = val;
            duration_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && duration_ok && balance_ok) {
        const uint32_t balance = settings_get_balance_epoch();
        if (balance < epoch) {
            atomic_store(&msg_data.balance, balance);
            const uint32_t flag = NEWER_EPOCH;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            settings_set_balance_epoch(epoch);
            setting_save_to_nvs();
        }
        else if (epoch == balance) {
            uint32_t flag = HANDSHAKE_REQUEST;
            xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
            atomic_store(&msg_data.duration, duration);
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char id_edge[MAC];
    char key[32];
    char value[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool beat_ok = false;

    settings_get_network_id_edge(id_edge, sizeof(id_edge));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, id_edge) == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "all") == 0) dest_ok = true;
        }
        else if (strcmp(key, "beat") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "true") == 0) beat_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && beat_ok) {
        uint32_t flag = HEARTBEAT_INCOMING;
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char network[ID_NETWORK];
    char key[32];
    char value[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool network_ok = false;

    settings_get_network(network, sizeof(network));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "Server0") == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, settings.node.mac_address) == 0) dest_ok = true;
        }
        else if (strcmp(key, "network") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, network) == 0) network_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && network_ok) {
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

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char val_buf[55];
    char id_network[ID_NETWORK];
    char wifi_ssid[WIFI_SSID];
    char wifi_password[WIFI_PASSWORD];
    char mqtt_uri[MQTT_URI];
    char device[DEVICE_NAME];
    uint32_t sample = 0;
    energy_mode_t energy_mode = 0;
    bool sender_ok = false;
    bool apply = false;

    for (uint32_t i = 0; i < map_size; i++) {
        char key[55];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (strcmp(val_buf, "Server0") == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (strcmp(val_buf, settings.node.mac_address) == 0) apply = true;
            if (strcmp(val_buf, "all") == 0) apply = true;
        }
        else if (strcmp(key, "network") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            safe_strcpy(id_network, val_buf, sizeof(id_network));
        }
        else if (strcmp(key, "wifi_ssid") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            safe_strcpy(wifi_ssid, val_buf, sizeof(wifi_ssid));
        }
        else if (strcmp(key, "wifi_password") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            safe_strcpy(wifi_password, val_buf, sizeof(wifi_password));
        }
        else if (strcmp(key, "mqtt_uri") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            safe_string_copy(mqtt_uri, val_buf, sizeof(mqtt_uri));
        }
        else if (strcmp(key, "device_name") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            safe_string_copy(device, val_buf, sizeof(device));
        }
        else if (strcmp(key, "sample") == 0) {
            uint32_t val = mpack_expect_u32(&reader);
            sample = val;
        }
        else if (strcmp(key, "energy_mode") == 0) {
            uint8_t val = mpack_expect_u8(&reader);
            energy_mode = val;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (mpack_reader_destroy(&reader) != mpack_ok) {
        return false;
    }

    if (sender_ok && apply) {
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char key[32];
    char value[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool hand_ok = false;

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "Server0") == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, settings.node.mac_address) == 0) dest_ok = true;
        }
        else if (strcmp(key, "handshake") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "true") == 0) hand_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && hand_ok) {
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char network[ID_NETWORK];
    char key[32];
    char value[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool net_ok = false;

    settings_get_network(network, sizeof(network));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "Server0") == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, settings.node.mac_address) == 0) dest_ok = true;
        }
        else if (strcmp(key, "network") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, network) == 0) net_ok = true;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && net_ok) {
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

    const uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char network[ID_NETWORK];
    char key[32];
    char value[32];
    bool sender_ok = false;
    bool dest_ok = false;
    bool net_ok = false;
    bool is_active = false;

    settings_get_network(network, sizeof(network));

    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "Server0") == 0) sender_ok = true;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, settings.node.mac_address) == 0) dest_ok = true;
        }
        else if (strcmp(key, "network") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, network) == 0) net_ok = true;
        }
        else if (strcmp(key, "active") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "true") == 0) is_active = true;
            if (strcmp(value, "false") == 0) is_active = false;
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    if (sender_ok && dest_ok && net_ok) {
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