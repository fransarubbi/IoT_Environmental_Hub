#include "freertos/FreeRTOS.h"
#include "Fsm/fsm.h"
#include "esp_timer.h"
#include <esp_log.h>
#include "OTA/ota.h"
#include "System/system.h"
#include "MQTT/mqtt.h"
#include "components/mpack/include/mpack.h"
#include "Message/message.h"
#include "Timer/timer.h"
#include <stdatomic.h>


message_variable_t msg_data;
AtomicState shared_state = CHECK_FIRMWARE;


const StateTable table[] = {
    {CHECK_FIRMWARE, eUpdate, UPDATE, action_entry_update},
    {CHECK_FIRMWARE, eNotUpdate, INIT_SYSTEM, action_entry_init_system},
    {UPDATE, eUpdateOk, NOTIFY_OK, action_entry_notify_ok},
    {UPDATE, eUpdateError, INIT_SYSTEM, action_entry_init_system},
    {INIT_SYSTEM, eFromInitToStore, STORE, action_entry_store},
    {INIT_SYSTEM, eFromInitToNormal, NORMAL, action_entry_normal},
    {INIT_SYSTEM, eFromInitToBalance, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {INIT_SYSTEM, eFromInitToSafe, SAFE_MODE, action_entry_safe},
    {STORE, eFromStoreToBypass, BYPASS, action_entry_bypass},
    {STORE, eFromStoreToBalance, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {NORMAL, eFromNormalToCooling, COOLING_TIME, action_entry_cooling},
    {NORMAL, eFromNormalToBalance, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {INIT_BALANCE_MODE, eFromInitBalanceToStore, STORE, action_entry_store},
    {INIT_BALANCE_MODE, eFromInitBalanceToInHandshake, IN_HANDSHAKE, action_entry_in_handshake},
    {INIT_BALANCE_MODE, eFromInitBalanceToAlert, ALERT, action_entry_alert},
    {INIT_BALANCE_MODE, eFromInitBalanceToData, DATA, action_entry_data},
    {INIT_BALANCE_MODE, eFromInitBalanceToMonitor, MONITOR, action_entry_monitor},
    {IN_HANDSHAKE, eFromInHandshakeToStore, STORE, action_entry_store},
    {IN_HANDSHAKE, eFromInHandshakeToAlert, ALERT, action_entry_alert},
    {IN_HANDSHAKE, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {ALERT, eFromAlertToStore, STORE, action_entry_store},
    {ALERT, eFromAlertToData, DATA, action_entry_data},
    {ALERT, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {DATA, eFromDataToStore, STORE, action_entry_store},
    {DATA, eFromDataToMonitor, MONITOR, action_entry_monitor},
    {DATA, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {MONITOR, eFromMonitorToStore, STORE, action_entry_store},
    {MONITOR, eFromMonitorToOutHandshake, OUT_HANDSHAKE, action_entry_out_handshake},
    {MONITOR, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {OUT_HANDSHAKE, eFromOutHandshakeToNormal, NORMAL, action_entry_normal},
    {OUT_HANDSHAKE, eFromOutHandshakeToStore, STORE, action_entry_store},
    {OUT_HANDSHAKE, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {COOLING_TIME, eFromCoolingToUpdateScore, UPDATE_SCORE, action_entry_update_score},
    {COOLING_TIME, eToBypass, BYPASS, action_entry_bypass},
    {UPDATE_SCORE, eFromUpdateScoreToCooling, COOLING_TIME, action_entry_cooling},
    {UPDATE_SCORE, eFromUpdateScoreToNormal, NORMAL, action_entry_normal},
    {UPDATE_SCORE, eToBypass, BYPASS, action_entry_bypass},
    {BYPASS, eFromBypassToNormal, NORMAL, action_entry_normal},
    {BYPASS, eFromBypassToBalance, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {SAFE_MODE, eFromSafeToNormal, NORMAL, action_entry_normal},
    {SAFE_MODE, eFromSafeToStore, STORE, action_entry_store}
};
const uint8_t SIZE_TABLE = sizeof(table) / sizeof(StateTable);


static void event_processor(Fsm *fsm, const Event event) {
    for (uint8_t i = 0; i < SIZE_TABLE; i++) {
        if (table[i].current == fsm->state && table[i].event == event) {
            if (table[i].action) table[i].action(fsm);
            fsm->state = table[i].next;
            atomic_store(&shared_state, fsm->state);
            return;
        }
    }
}


void fsm_task(void *pvParameter) {
    Fsm fsm = {CHECK_FIRMWARE, 0};
    msg_data.duration = ATOMIC_VAR_INIT(0);
    msg_data.balance = ATOMIC_VAR_INIT(settings_get_balance_epoch());
    msg_data.jitter = ATOMIC_VAR_INIT(0);
    msg_data.frequency = ATOMIC_VAR_INIT(0);
    Event event;

    action_entry_check_firmware(&fsm);

    while (1) {
        if (xQueueReceive(queues.event, &event, portMAX_DELAY) == pdTRUE) {
            event_processor(&fsm, event);
        }
    }
}


void action_entry_check_firmware(Fsm *fsm) {
    check_update();
}


void action_entry_update(Fsm *fsm) {
    if (ota_from_github() == ESP_OK) {
        const uint32_t flag = UPDATE_OK;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    } else {
        mqtt_packet_t packet;
        generate_message_firmware_ok(&packet, false);
        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
        const uint32_t flag = 0;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
}


void action_entry_init_system(Fsm *fsm) {
    init_timer(INIT_SYSTEM_TIMER);
    mqtt_enable_subscribe_topics();
}


void action_entry_notify_ok(Fsm *fsm) {
    mqtt_packet_t packet;
    generate_message_firmware_ok(&packet, true);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGW("FSM", "WARNING: Reiniciando sistema...");
    esp_restart();
}


void action_entry_init_balance_mode(Fsm *fsm) {
    delete_timer(HEARTBEAT_NORMAL_TIMER);
    delete_timer(INIT_SYSTEM_TIMER);
    delete_timer(BYPASS_TIMER);
    init_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    init_timer(ALL_BALANCE_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
}


void action_entry_in_handshake(Fsm *fsm) {
    mqtt_packet_t packet;
    generate_message_balance_mode_handshake(&packet);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
}


void action_entry_alert(Fsm *fsm) {
    // CHECK EPOCH ?
    xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
}


void action_entry_data(Fsm *fsm) {
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
}


void action_entry_monitor(Fsm *fsm) {
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
}


void action_entry_out_handshake(Fsm *fsm) {
    mqtt_packet_t packet;
    generate_message_balance_mode_handshake(&packet);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
}


void action_entry_normal(Fsm *fsm) {
    delete_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    delete_timer(ALL_BALANCE_TIMER);
    delete_timer(BYPASS_TIMER);
    delete_timer(SAFE_MODE_TIMER);
    delete_timer(INIT_SYSTEM_TIMER);
    init_timer(HEARTBEAT_NORMAL_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
    }
}


void action_entry_store(Fsm *fsm) {
    // MODIFICAR SAMPLE RATE AL DOBLE
    delete_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    delete_timer(HEARTBEAT_SAFE_MODE_TIMER);
    delete_timer(SAFE_MODE_TIMER);
    delete_timer(ALL_BALANCE_TIMER);
}


void action_entry_cooling(Fsm *fsm) {
    delete_timer(HEARTBEAT_NORMAL_TIMER);
    init_timer(COOLING_TIMER);
}


void action_entry_update_score(Fsm *fsm) {
    delete_timer(COOLING_TIMER);
    // INCREMENTAR HEALTH SCORE
    // SI CUMPLE LA CONDICION, GENERAR FLAG PARA LUEGO OBTENER EVENTO Y CAMBIAR DE ESTADO
}


void action_entry_bypass(Fsm *fsm) {
    delete_timer(COOLING_TIMER);
    // CREAR CONEXION HTTPS CON EL SERVIDOR
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
    }
}


void action_entry_safe(Fsm *fsm) {
    delete_timer(INIT_SYSTEM_TIMER);
    init_timer(HEARTBEAT_SAFE_MODE_TIMER);
    // CAMBIAR FRECUENCIA DE SAMPLEO
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
    }
}



