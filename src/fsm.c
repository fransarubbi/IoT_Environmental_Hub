/**
* @file fsm.c
 * @brief Implementación de la lógica de la Máquina de Estados.
 */


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


static const char *TAG = "FSM";
balance_mode_parameters_t balance;
phase_parameters_t phase;
safe_mode_parameters_t safe_mode;

/** @brief Estado actual expuesto de forma segura para otras tareas. */
AtomicState shared_state = CHECK_FIRMWARE;


/**
 * @brief Tabla de Transiciones de la FSM.
 * Define la lógica: Si estoy en [current] y pasa [event] -> Ejecuto [action] y voy a [next].
 */
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
    {INIT_BALANCE_MODE, eFromInitBalanceToSafe, SAFE_MODE, action_entry_safe},
    {IN_HANDSHAKE, eFromInHandshakeToStore, STORE, action_entry_store},
    {IN_HANDSHAKE, eFromInHandshakeToAlert, ALERT, action_entry_alert},
    {IN_HANDSHAKE, eNewerEpoch, INIT_BALANCE_MODE, action_entry_init_balance_mode},
    {IN_HANDSHAKE, eFromInHandshakeToSafe, SAFE_MODE, action_entry_safe},
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
    {OUT_HANDSHAKE, eFromOutHandshakeToSafe, SAFE_MODE, action_entry_safe},
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


/**
 * @brief Procesa el evento recibido y ejecuta la transición correspondiente.
 * Busca en la tabla una coincidencia para (Estado Actual + Evento).
 * * @param fsm Puntero a la estructura de la FSM.
 * @param event Evento recibido.
 */
static void event_processor(Fsm *fsm, const Event event) {
    for (uint8_t i = 0; i < SIZE_TABLE; i++) {
        if (table[i].current == fsm->state && table[i].event == event) {
            if (table[i].action) table[i].action(fsm);
            fsm->state = table[i].next;
            atomic_store(&shared_state, fsm->state);
            xQueueReset(queues.flag);
            return;
        }
    }
}


/**
 * @brief Funcion auxiliar para facilitar informacion al momento de debuggear, ya que
 * retorna el nombre del estado actual, en vez de su respectivo indice.
 * @param state Estado actual.
 * @return String con el nombre del estado.
 */
static char* get_state_name(const State state) {
    switch (state) {
        case CHECK_FIRMWARE: return "CHECK_FIRMWARE";
        case INIT_SYSTEM: return "INIT_SYSTEM";
        case UPDATE: return "UPDATE";
        case NOTIFY_OK: return "NOTIFY_OK";
        case INIT_BALANCE_MODE: return "INIT_BALANCE_MODE";
        case IN_HANDSHAKE: return "IN_HANDSHAKE";
        case ALERT: return "ALERT";
        case DATA: return "DATA";
        case MONITOR: return "MONITOR";
        case OUT_HANDSHAKE: return "OUT_HANDSHAKE";
        case NORMAL: return "NORMAL";
        case STORE: return "STORE";
        case COOLING_TIME: return "COOLING_TIME";
        case UPDATE_SCORE: return "UPDATE_SCORE";
        case BYPASS: return "BYPASS";
        case SAFE_MODE: return "SAFE_MODE";
        default: return "error";
    }
}


/**
 * @brief Tarea principal de la FSM.
 * Inicializa variables atómicas y entra en un bucle infinito esperando eventos en la cola.
 */
void fsm_task(void *pvParameter) {
    Fsm fsm = {CHECK_FIRMWARE};
    Event event;

    action_entry_check_firmware(&fsm);

    while (1) {
        if (xQueueReceive(queues.event, &event, portMAX_DELAY) == pdTRUE) {
            event_processor(&fsm, event);
            ESP_LOGI(TAG, " - Estado actual: %s -", get_state_name(fsm.state));
        }
    }
}


/* --- Implementación de Acciones --- */


void action_entry_check_firmware(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry CHECK_FIRMWARE -");
    xTaskNotify(task_handle.health_handle, NOTIFY_CMD_START, eSetBits);
    check_update();
}


void action_entry_update(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry UPDATE -");
    if (ota_from_github() == ESP_OK) {
        const uint32_t flag = UPDATE_OK;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    } else {
        mqtt_msg_general_t packet;
        generate_message_firmware_ok(&packet, false);
        xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
        const uint32_t flag = 0;
        xQueueSend(queues.flag, &flag, pdMS_TO_TICKS(100));
    }
}


void action_entry_init_system(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry INIT_SYSTEM -");
    xTaskNotify(task_handle.parser_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.converter_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.data_pt_handle, NOTIFY_CMD_START, eSetBits);
    init_timer(INIT_SYSTEM_TIMER);
    mqtt_enable_subscribe_topics();
}


void action_entry_notify_ok(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry NOTIFY_OK -");
    mqtt_msg_general_t packet;
    generate_message_firmware_ok(&packet, true);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(5000));
    ESP_LOGW(TAG, "- WARNING: Reiniciando sistema... -");
    esp_restart();
}


void action_entry_init_balance_mode(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry INIT_BALANCE_MODE -");
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_STOP, eSetBits);
    delete_timer(HEARTBEAT_NORMAL_TIMER);
    delete_timer(INIT_SYSTEM_TIMER);
    delete_timer(BYPASS_TIMER);
    xTaskNotify(task_handle.health_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_START, eSetBits);
    init_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    //init_timer(INIT_BALANCE_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
    }
}


void action_entry_in_handshake(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry IN_HANDSHAKE -");
    delete_timer(INIT_BALANCE_TIMER);
    init_timer(HANDSHAKE_TIMER);
    mqtt_msg_general_t packet;
    generate_message_balance_mode_handshake(&packet);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
}


void action_entry_alert(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry ALERT -");
    delete_timer(HANDSHAKE_TIMER);
}


void action_entry_data(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry DATA -");
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
}


void action_entry_monitor(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry MONITOR -");
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
}


void action_entry_out_handshake(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry OUT_HANDSHAKE -");
    init_timer(HANDSHAKE_TIMER);
    mqtt_msg_general_t packet;
    generate_message_balance_mode_handshake(&packet);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
}


void action_entry_normal(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry NORMAL -");
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_STOP, eSetBits);
    delete_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    delete_timer(BYPASS_TIMER);
    delete_timer(INIT_SYSTEM_TIMER);
    delete_timer(HANDSHAKE_TIMER);
    init_timer(HEARTBEAT_NORMAL_TIMER);
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.data_ct_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.health_handle, NOTIFY_CMD_START, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
    }
}


void action_entry_store(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry STORE -");
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.health_handle, NOTIFY_CMD_STOP, eSetBits);
    delete_timer(HEARTBEAT_BALANCE_MODE_TIMER);
    delete_timer(HEARTBEAT_SAFE_MODE_TIMER);
    delete_timer(HANDSHAKE_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.data_ct_handle, NOTIFY_CMD_START, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
    }
}


void action_entry_cooling(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry COOLING -");
    xTaskNotify(task_handle.heartbeat_handle, NOTIFY_CMD_STOP, eSetBits);
    delete_timer(HEARTBEAT_NORMAL_TIMER);
    init_timer(COOLING_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.data_ct_handle, NOTIFY_CMD_STOP, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
    }
}


void action_entry_update_score(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry UPDATE_SCORE -");
    delete_timer(COOLING_TIMER);
    mqtt_msg_general_t packet;
    generate_message_ping(&packet);
    xQueueSend(queues.general, &packet, pdMS_TO_TICKS(100));
}


void action_entry_bypass(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry BYPASS -");
    delete_timer(COOLING_TIMER);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_STOP, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_STOP, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_STOP, eSetBits);
    }
    xTaskNotify(task_handle.https_handle, NOTIFY_CMD_START, eSetBits);
}


void action_entry_safe(Fsm *fsm) {
    ESP_LOGI(TAG, "- INFO: Ejecutando acciones on entry SAFE_MODE -");
    delete_timer(HANDSHAKE_TIMER);
    delete_timer(INIT_SYSTEM_TIMER);
    init_timer(HEARTBEAT_SAFE_MODE_TIMER);
    xTaskNotify(task_handle.dht11_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.mq135_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.ky037_handle, NOTIFY_CMD_START, eSetBits);
    xTaskNotify(task_handle.monitor_handle, NOTIFY_CMD_START, eSetBits);
    if (task_handle.send_settings_handle != NULL) {
        xTaskNotify(task_handle.send_settings_handle, NOTIFY_CMD_START, eSetBits);
    }
}