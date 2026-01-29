#include "System/system.h"
#include "DHT11/dht11.h"
#include "KY037/ky037.h"
#include "MQ135/mq135.h"
#include "MQTT/mqtt.h"
#include "Setting/settings.h"
#include "Time/time.h"
#include "Wifi/wifi.h"
#include "Data/data.h"
#include "Monitor/monitor.h"
#include "Fsm/fsm.h"
#include <esp_log.h>

#include "Converter/converter.h"


static const char *TAG = "System";


/**
 * @brief Inicializa todas las queues del sistema.
 * @return true si el proceso fue exitoso, false en caso contrario. Si el
 * retorno fue false, el sistema se reiniciara.
 */
bool init_queues(void) {
    queues.general           = xQueueCreate(QUEUE_GENERAL, sizeof(mqtt_packet_t));
    queues.flag              = xQueueCreate(QUEUE_FLAG, sizeof(uint32_t));
    queues.event             = xQueueCreate(QUEUE_EVENT, sizeof(int));
    queues.data_buffer       = xQueueCreate(QUEUE_LENGTH, sizeof(mqtt_packet_t));
    queues.alert_air_buffer  = xQueueCreate(QUEUE_LENGTH, sizeof(mqtt_packet_t));
    queues.alert_temp_buffer = xQueueCreate(QUEUE_LENGTH, sizeof(mqtt_packet_t));
    queues.monitor_buffer    = xQueueCreate(QUEUE_LENGTH, sizeof(mqtt_packet_t));
    queues.settings_buffer   = xQueueCreate(QUEUE_LENGTH, sizeof(mqtt_packet_t));
    queues.dht11_buffer      = xQueueCreate(QUEUE, dht11_struct_get_size());
    queues.ky037_buffer      = xQueueCreate(QUEUE, ky037_get_size());
    queues.mq135_buffer      = xQueueCreate(QUEUE, sizeof(mq135_data_t));
    queues.dht11_to_mq135    = xQueueCreate(QUEUE, dht11_struct_get_size());

    if (!queues.general || !queues.flag || !queues.event || !queues.data_buffer ||
        !queues.monitor_buffer || !queues.dht11_buffer || !queues.ky037_buffer ||
        !queues.mq135_buffer || !queues.dht11_to_mq135 || !queues.alert_air_buffer ||
        !queues.alert_temp_buffer || !queues.settings_buffer) {
        ESP_LOGE(TAG, "- ERROR: Error creando queues -");
        return false;
    }
    return true;
}


/**
 * @brief Inicia los Event Group del sistema
 * @return true si el proceso fue exitoso, false si ocurrio algun problema.
 * Si el retorno fue false, el sistema se reiniciara.
 */
bool init_event_group(void) {
    event_group.collector_events = xEventGroupCreate();
    event_group.mqtt_event_group = xEventGroupCreate();
    event_group.wifi_event_group = xEventGroupCreate();
    if (!event_group.collector_events || !event_group.mqtt_event_group || !event_group.wifi_event_group) {
        ESP_LOGE(TAG, "- ERROR: Error creando event_group -");
        return false;
    }
    return true;
}


/**
 * @brief Inicializacion de los drivers basicos para el funcionamiento del sistema.
 * @return true en caso de exito o false en caso de que falle alguno de los drivers.
 * Es importante este retorno porque es critico que todos los componentes se inicien
 * correctamente. Caso contrario el sistema se reiniciara.
 */
bool init_base_drivers(void) {
    if (uart_init() != ESP_OK) { ESP_LOGE(TAG, "- ERROR: UART fallo -"); return false; }
    if (wifi_init() != ESP_OK) { ESP_LOGE(TAG, "- ERROR: WiFi fallo -"); return false; }
    if (time_init() != ESP_OK) { ESP_LOGE(TAG, "- ERROR: Time fallo -"); return false; }
    if (mqtt_init() != ESP_OK) { ESP_LOGE(TAG, "- ERROR: MQTT fallo -"); return false; }
    return true;
}


/**
 * @brief Espera que todos los sensores del sistema esten listos para su uso.
 */
void wait_for_sensors(void) {
    esp_err_t retMQ135 = ESP_FAIL, retDHT11 = ESP_FAIL, retKY037 = ESP_FAIL;

    while (1) {
        if (retMQ135 != ESP_OK) retMQ135 = mq135_init();
        if (retDHT11 != ESP_OK) retDHT11 = dht11_init();
        if (retKY037 != ESP_OK) retKY037 = ky037_init();

        if (retMQ135 == ESP_OK && retDHT11 == ESP_OK && retKY037 == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // vTaskDelay(pdMS_TO_TICKS(TIME_SETUP));   para mq135
}


/**
 * @brief Creacion de todas las tareas del sistema.
 */
void start_application_tasks(void) {
    task_handle.fsm_handle = xTaskCreateStaticPinnedToCore(fsm_task, "FSM", STACK_FSM, NULL, PRIO_SENSORS, mem.fsm.stack, &mem.fsm.tcb, CORE_1);
    task_handle.converter_handle = xTaskCreateStaticPinnedToCore(flag_converter_task, "Converter", STACK_CONVERTER, NULL, PRIO_SENSORS, mem.converter.stack, &mem.converter.tcb, CORE_1);
    task_handle.dht11_handle = xTaskCreateStaticPinnedToCore(dht11_task, "DHT11", STACK_DHT11, NULL, PRIO_SENSORS, mem.dht11.stack, &mem.dht11.tcb, CORE_1);
    task_handle.ky037_handle = xTaskCreateStaticPinnedToCore(ky037_task, "KY037", STACK_MIC,   NULL, PRIO_SENSORS, mem.ky037.stack, &mem.ky037.tcb, CORE_1);
    task_handle.mq135_handle = xTaskCreateStaticPinnedToCore(mq135_task, "MQ135", STACK_MQ135, NULL, PRIO_SENSORS, mem.mq135.stack, &mem.mq135.tcb, CORE_0);
    task_handle.data_ct_handle = xTaskCreateStaticPinnedToCore(data_collection_task, "Collector", STACK_COLLECTOR, NULL, PRIO_COMMS, mem.collector.stack, &mem.collector.tcb, CORE_1);
    task_handle.data_pt_handle = xTaskCreateStaticPinnedToCore(data_publish_task, "Publisher", STACK_PUBLISHER, NULL, PRIO_COMMS, mem.publisher.stack, &mem.publisher.tcb, CORE_0);
    task_handle.monitor_handle = xTaskCreateStaticPinnedToCore(stack_monitor_task, "Monitor", STACK_MONITOR, NULL, PRIO_SENSORS, mem.monitor.stack, &mem.monitor.tcb, CORE_0);
    task_handle.send_settings_handle = xTaskCreateStaticPinnedToCore(send_settings_task, "Send_sett", STACK_SEND_SETT, NULL, PRIO_COMMS, mem.send_settings.stack, &mem.send_settings.tcb, CORE_0);
}
