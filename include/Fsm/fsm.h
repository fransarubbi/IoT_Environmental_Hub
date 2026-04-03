/**
* @file fsm.h
 * @brief Definición de la Máquina de Estados Finitos (FSM) del sistema.
 *
 * Este módulo orquesta el comportamiento global del dispositivo. Define los estados,
 * eventos y la estructura de datos compartida que utilizan otras tareas para
 * sincronizarse con el flujo principal.
 */


#ifndef FSM_H
#define FSM_H

#include <stdint.h>
#include <stdatomic.h>


/* --- Flags de Eventos para el sistema --- */
#define TIMEOUT_INIT              0x01          /**< Timeout durante la inicialización del sistema. */
#define UPDATE_FLAG               0x02          /**< Notificación de firmware disponible. */
#define STATE_SAFE_MODE           0x04          /**< Orden de entrar en Modo Seguro. */
#define STATE_BALANCE_MODE        0x08          /**< Orden de entrar en Modo Balanceo. */
#define STATE_NORMAL              0x10          /**< Orden de entrar en Modo Normal. */
#define UPDATE_OK                 0x20          /**< Actualización OTA exitosa. */
#define HANDSHAKE_REQUEST         0x40          /**< Solicitud de Handshake recibida. */
#define TIMEOUT_HEARTBEAT         0x80          /**< Watchdog de aplicación expirado (pérdida de conexión). */
#define MESSAGE_ALERT             0x100         /**< Alerta crítica detectada. */
#define HEALTH_SCORE_DEGRADED     0x200         /**< Calidad de red degradada. */
#define HEALTH_SCORE_CRITICAL     0x400         /**< Calidad de red crítica. */
#define HEALTH_SCORE_UNAVAILABLE  0x800         /**< Red no disponible. */
#define HEALTH_SCORE_HEALTHY      0x1000        /**< Red saludable. */
#define TIMEOUT_COOLING           0x2000        /**< Fin del tiempo de enfriamiento. */
#define TIMEOUT_BALANCE           0x4000        /**< Timeout de los estados INIT_BALANCE_MODE, IN_HANDSHAKE y OUT_HANDSHAKE. */
#define TIMEOUT_BYPASS            0x8000        /**< Fin del tiempo en modo Bypass. */
#define TIMEOUT_SAFE_MODE         0x10000       /**< Fin del tiempo en modo Seguro. */
#define NEWER_EPOCH               0x20000       /**< Detección de nueva sesión de balanceo (Epoch mayor). */
#define PHASE_ALERT               0x40000       /**< Fase de alerta en protocolo de balanceo. */
#define PHASE_DATA                0x80000       /**< Fase de datos en protocolo de balanceo. */
#define PHASE_MONITOR             0x100000      /**< Fase de monitorización en protocolo de balanceo. */
#define HEARTBEAT_INCOMING        0x200000      /**< Latido recibido desde el Edge. */
#define ALERT_EMPTY_QUEUE         0x400000      /**< Cola de alertas vacia. */
#define DATA_EMPTY_QUEUE          0x800000      /**< Cola de data vacia. */
#define MONITOR_EMPTY_QUEUE       0x1000000     /**< Cola de monitor vacia. */
#define SAFE_MODE_EMPTY_QUEUE     0x2000000     /**< Colas vacias en safe mode. */
#define LINKAGE_OK                0x4000000     /**< Linkage OK. */

/* --- Comandos de Notificación a Tareas --- */
#define NOTIFY_CMD_START    0x01   /**< Comando para iniciar/reanudar una tarea. */
#define NOTIFY_CMD_STOP     0x02   /**< Comando para pausar/detener una tarea. */
#define NOTIFY_CMD_DESTROY  0x04   /**< Comando para eliminar una tarea. */


/**
 * @brief Estados posibles del sistema.
 */
typedef enum {
    CHECK_FIRMWARE,
    LINKAGE,
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
    UPDATE_SCORE,
    BYPASS,
    SAFE_MODE,
} State;


/**
 * @brief Eventos que disparan transiciones en la FSM.
 * Estos eventos son generados por el módulo `converter.c`.
 */
typedef enum {
    eUpdate,
    eNotUpdate,
    eUpdateOk,
    eUpdateError,
    eLinkageOk,
    eFromInitToStore,
    eFromInitToBalance,
    eFromInitToSafe,
    eFromInitToNormal,
    eFromNormalToCooling,
    eFromNormalToBalance,
    eFromNormalToInHandshake,
    eFromInitBalanceToStore,
    eFromInitBalanceToInHandshake,
    eFromInitBalanceToAlert,
    eFromInitBalanceToData,
    eFromInitBalanceToMonitor,
    eFromInitBalanceToSafe,
    eFromInHandshakeToAlert,
    eFromInHandshakeToStore,
    eFromInHandshakeToSafe,
    eFromAlertToData,
    eFromAlertToStore,
    eFromDataToMonitor,
    eFromDataToStore,
    eFromMonitorToOutHandshake,
    eFromMonitorToStore,
    eFromOutHandshakeToStore,
    eFromOutHandshakeToNormal,
    eFromOutHandshakeToSafe,
    eFromCoolingToUpdateScore,
    eFromCoolingToInitBalance,
    eFromCoolingToInHandshake,
    eFromUpdateScoreToCooling,
    eFromUpdateScoreToNormal,
    eFromUpdateScoreToInitBalance,
    eFromUpdateScoreToInHandshake,
    eToBypass,
    eFromStoreToBalance,
    eFromStoreToBypass,
    eFromStoreToNormal,
    eFromStoreToSafe,
    eFromStoreToInitBalance,
    eFromStoreToInHandshake,
    eFromBypassToNormal,
    eFromBypassToBalance,
    eFromSafeToStore,
    eFromSafeToNormal,
    eNewerEpoch,
    eRepeatHandshake,
} Event;


/**
 * @brief Estructura de control de la FSM.
 */
typedef struct {
    State state;
} Fsm;


/**
 * @brief Variables compartidas atómicas para parámetros del balance mode general.
 * Permiten lectura/escritura segura entre tareas.
 */
typedef struct {
    atomic_uint_fast32_t duration;
    atomic_uint_fast32_t balance;
} balance_mode_parameters_t;


/**
 * @brief Variables compartidas atómicas para parámetros de las fases.
 * Permiten lectura/escritura segura entre tareas.
 */
typedef struct {
    atomic_uint_fast32_t balance;
    atomic_uint_fast32_t frequency;
    atomic_uint_fast32_t jitter;
} phase_parameters_t;


/**
 * @brief Variables compartidas atómicas para parámetros del modo seguro.
 * Permiten lectura/escritura segura entre tareas.
 */
typedef struct {
    atomic_uint_fast32_t frequency;
    atomic_uint_fast32_t jitter;
} safe_mode_parameters_t;


void fsm_task(void *pvParameter);

/* Definición del puntero a función para las acciones */
typedef void (*Action)(Fsm *fsm);

/* Acciones de entrada a estados (On Entry) */
void action_entry_check_firmware(Fsm *fsm);
void action_entry_linkage(Fsm *fsm);
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
void action_entry_update_score(Fsm *fsm);
void action_entry_bypass(Fsm *fsm);
void action_entry_safe(Fsm *fsm);


/**
 * @brief Estructura de la Tabla de Transición de Estados.
 */
typedef struct {
    State current;
    Event event;
    State next;
    Action action;
} StateTable;


extern const StateTable table[];
extern const uint8_t SIZE_TABLE;
extern balance_mode_parameters_t balance;
extern phase_parameters_t phase;
extern safe_mode_parameters_t safe_mode;


/** @brief Estado global compartido atómicamente para lectura desde otras tareas. */
typedef _Atomic(State) AtomicState;
extern AtomicState shared_state;


#endif //FSM_H