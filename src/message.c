#include "Message/message.h"
#include <esp_log.h>
#include "OTA/ota.h"
#include "Time/time.h"
#include "MQTT/mqtt.h"
#include "mpack.h"
#include "Data/data.h"
#include "DHT11/dht11.h"
#include "MQ135/mq135.h"
#include "Monitor/monitor.h"
#include "Setting/settings.h"
#include "System/system.h"


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


// --------------------------------------------------------------------------------------

// todo parsear mas mensajes

bool parse_message_setting(const char* data, size_t len) {
    uint8_t flags = 0x0;
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char val_buf[55];
    for (uint32_t i = 0; i < map_size; i++) {
        char key[55];
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (strcmp(val_buf, "Server0") == 0) flags |= FLAG_SERVER_VALID;
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x3) {
                if (strcmp(val_buf, settings.node.mac_address) == 0) flags |= FLAG_ITS_ME;
                if (strcmp(val_buf, "all") == 0) flags |= FLAG_ITS_ALL;
            }
        }
        else if (strcmp(key, "network") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_strcpy((char *)settings.network.id_network, val_buf, sizeof(settings.network.id_network));
            }
        }
        else if (strcmp(key, "wifi_ssid") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_strcpy((char *)settings.wifi.ssid, val_buf, sizeof(settings.wifi.ssid));
            }
        }
        else if (strcmp(key, "wifi_password") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_strcpy((char *)settings.wifi.password, val_buf, sizeof(settings.wifi.password));
            }
        }
        else if (strcmp(key, "mqtt_uri") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7 || flags == 0xB) {
                safe_string_copy(settings.mqtt.uri, val_buf, sizeof(settings.mqtt.uri));
            }
        }
        else if (strcmp(key, "device_name") == 0) {
            mpack_expect_cstr(&reader, val_buf, sizeof(val_buf));
            if (flags == 0x7) {
                safe_string_copy(settings.node.device_name, val_buf, sizeof(settings.node.device_name));
            }
        }
        else if (strcmp(key, "sample") == 0) {
            uint32_t val = mpack_expect_u32(&reader);
            if (flags == 0x7 || flags == 0xB) {
                settings.node.sample_rate = val;
            }
        }
        else if (strcmp(key, "energy_mode") == 0) {
            uint8_t val = mpack_expect_u8(&reader);
            if (flags == 0x7 || flags == 0xB) {
                settings.node.energy_mode = val;
            }
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

    esp_err_t ret = setting_save_to_nvs();
    if (ret != ESP_OK) {
        return false;
    }
    return true;
}


bool parse_message_setting_ok(const char* data, size_t len) {
    uint8_t flags = 0x0;
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, data, len);

    uint32_t map_size = mpack_expect_map(&reader);
    if (mpack_reader_error(&reader) != mpack_ok) {
        return false;
    }

    char key[32];
    char value[32];
    for (uint32_t i = 0; i < map_size; i++) {
        mpack_expect_cstr(&reader, key, sizeof(key));

        if (strcmp(key, "sender_user_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if (strcmp(value, "SERVER0") == 0) {
                flags |= FLAG_SERVER_VALID;
            }
        }
        else if (strcmp(key, "destination_id") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if ((flags == 0x03) && (strcmp(value, settings.node.mac_address) == 0)) {
                flags |= FLAG_ITS_ME;
            }
        }
        else if (strcmp(key, "handshake") == 0) {
            mpack_expect_cstr(&reader, value, sizeof(value));
            if ((flags == 0x07) && (strcmp(value, "true") == 0)) {
                if (task_handle.send_settings_handle != NULL) {
                    xTaskNotifyGive(task_handle.send_settings_handle);
                }
            }
        }
        else {
            mpack_discard(&reader);
        }

        if (mpack_reader_error(&reader) != mpack_ok) {
            return false;
        }
    }

    return (mpack_reader_destroy(&reader) == mpack_ok);
}
