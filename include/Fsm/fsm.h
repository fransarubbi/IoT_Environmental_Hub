#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdatomic.h>

#define TIMEOUT_INIT 0x01                // Flag de timeout en INIT_SYSTEM
#define STATE_SAFE_MODE 0x02             // Flag de mensaje de estado "SAFE_MODE"
#define STATE_BALANCE_MODE 0x04          // Flag de mensaje de estado "BALANCE_MODE"
#define STATE_NORMAL 0x08                // Flag de mensaje de estado "NORMAL"
#define EDGE_WORKING 0x10                // Flag de edge funcionando
#define HANDSHAKE_REQUEST 0x20           // Flag de peticion de handshake
#define TIMEOUT_HEARTBEAT 0x40           // Flag de heartbeat no recibido
#define INIT_BALANCE 0x80                // Flag de iniciar modo de balanceo
#define MESSAGE_ALERT 0x100              // Flag de mensaje de alerta
#define HANDSHAKE_OK 0x200               // Flag de handshake correcto
#define HEALTH_SCORE_DEGRADED 0x400      // Flag de salud de la conexion degradada
#define HEALTH_SCORE_CRITICAL 0x800      // Flag de salud de la conexion critica
#define HEALTH_SCORE_UNAVAILABLE 0x1000  // Flag de salud de la conexion nefasta
#define HEALTH_SCORE_HEALTHY     0x2000  // Flag de salud de la conexion saludable
#define TIMEOUT_SAFE_MODE 0x2000         // Flag de fin de timer para SAFE_MODE
#define UPDATE_FLAG 0x4000               // Flag de actualizacion de firmware disponible
#define UPDATE_OK 0x8000                 // Flag de actualizacion de firmware correcta
#define PHASE_ALERT 1
#define PHASE_DATA 2
#define PHASE_MONITOR 3
#define HEARTBEAT_INCOMING 0x80

#define NOTIFY_CMD_START  0x01
#define NOTIFY_CMD_STOP   0x02

typedef enum {
    CHECK_FIRMWARE,
    INIT_SYSTEM,
    UPDATE,
    NOTIFY_OK,
    INIT_BALANCE_MODE,
    IN_HANDSHAKE,
    ALERT,
    DATA,
    MONITOR,
    OUT_HANDSHAKE,
    NORMAL,
    STORE,
    COOLING_TIME,
    PING,
    UPDATE_SCORE,
    BYPASS,
    SAFE_MODE,
} State;


typedef enum {
    eUpdate,
    eNotUpdate,
    eUpdateOk,
    eUpdateError,
    eFromInitToStore,
    eFromInitToBalance,
    eFromInitToSafe,
    eFromInitToNormal,
    eFromNormalToCooling,
    eFromNormalToBalance,
    eFromInitBalanceToInHandshake,
    eFromInitBalanceToAlert,
    eFromInitBalanceToData,
    eFromInitBalanceToMonitor,
    eFromInHandshakeToAlert,
    eFromInHandshakeToInitBalance,
    eFromAlertToData,
    eFromAlertToInitBalance,
    eFromDataToMonitor,
    eFromDataToInitBalance,
    eFromMonitorToOutHandshake,
    eFromMonitorToInitBalance,
    eFromBalanceToNormal,
    eFromBalanceToStore,
    eFromOutHandshakeToInitBalance,
    eFromCoolingToPing,
    eFromPingToCooling,
    eFromPingToUpdateScore,
    eFromUpdateScoreToCooling,
    eFromScoreToNormal,
    eToBypass,
    eFromStoreToBalance,
    eFromBypassToNormal,
    eFromBypassToBalance,
    eFromSafeToStore,
    eFromSafeToNormal,
} Event;


typedef struct {
    State state;
    uint32_t flag;
} Fsm;


typedef struct {
    atomic_uint_fast32_t duration;
    atomic_uint_fast32_t balance;
    atomic_uint_fast32_t jitter;
    atomic_uint_fast32_t frequency;
} message_variable_t;

extern message_variable_t msg_data;

void fsm_task(void *pvParameter);

typedef void (*Action)(Fsm *fsm);
void action_entry_check_firmware(Fsm *fsm);
void action_entry_update(Fsm *fsm);
void action_entry_init_system(Fsm *fsm);
void action_entry_notify_ok(Fsm *fsm);
void action_entry_init_balance_mode(Fsm *fsm);
void action_entry_in_handshake(Fsm *fsm);
void action_entry_alert(Fsm *fsm);
void action_entry_data(Fsm *fsm);
void action_entry_monitor(Fsm *fsm);
void action_entry_out_handshake(Fsm *fsm);
void action_entry_normal(Fsm *fsm);
void action_entry_store(Fsm *fsm);
void action_entry_cooling(Fsm *fsm);
void action_entry_ping(Fsm *fsm);
void action_entry_update_score(Fsm *fsm);
void action_entry_bypass(Fsm *fsm);
void action_entry_safe(Fsm *fsm);


typedef struct {
    State current;
    Event event;
    State next;
    Action action;
} StateTable;


extern const StateTable table[];
extern const uint8_t SIZE_TABLE;



#endif //FSM_H