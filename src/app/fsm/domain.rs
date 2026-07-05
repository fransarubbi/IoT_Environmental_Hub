/// Estados Globales de nivel superior.
/// Determinan el comportamiento macro del sistema.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum StateGlobal {
    Start, // Estado "inutil", solo sirve como arranque para estimular a la FSM
    Init,
    StoreMessage,
    Normal,
    Bypass,
    Balance,
    Safe,
}

/// Sub-estados del estado Init (`StateGlobal::Init`).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum SubStateInit {
    InitCLI,
    CheckFirmware,
    InitMqtt,
    Linkage,
    InitSystem,
    NotifyFirmwareUpdated,
}

/// Sub-estados del estado StoreMessage (`StateGlobal::StoreMessage`).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum SubStateStore {
    Store,
    Cooling,
    UpdateScore,
}

/// Sub-estados del estado Balance (`StateGlobal::Balance`).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum SubStateBalance {
    InitBalanceMode,
    InHandshake,
    Alert,
    Data,
    Monitor,
    OutHandshake,
}

/// Representación compuesta del estado completo de la FSM.
///
/// Utiliza `Option` para modelar la jerarquía. Si un estado padre no está activo,
/// sus hijos deben ser `None`.
#[derive(Debug, Clone)]
pub struct FsmState {
    global: StateGlobal,
    init: Option<SubStateInit>,
    store: Option<SubStateStore>,
    balance: Option<SubStateBalance>,
}

/// Resultado de intentar aplicar un evento al estado actual.
#[derive(Debug)]
pub enum Transition {
    Valid(TransitionValid),
    Invalid(TransitionInvalid),
}

/// Datos resultantes de una transición exitosa.
#[derive(Debug)]
pub struct TransitionValid {
    change_state: FsmState,
    actions: Vec<Action>,
}

impl TransitionValid {
    pub fn change_state(&self) -> FsmState {
        self.change_state.clone()
    }
    pub fn actions(&self) -> Vec<Action> {
        self.actions.clone()
    }
}

/// Datos resultantes de una transición fallida o no permitida.
#[derive(Debug)]
pub struct TransitionInvalid {
    invalid: String,
}

impl TransitionInvalid {
    pub fn get_invalid(&self) -> &str {
        self.invalid.as_str()
    }
}

/// Acciones o Efectos Secundarios (Side Effects).
///
/// Estas variantes son instrucciones para el exterior. La FSM **decide** qué hacer,
/// pero no **ejecuta** la acción (principio de separación de responsabilidades).
#[derive(Debug, PartialEq, Clone)]
pub enum Action {
    ActionInitCli,
    ActionInitWifi,
    ActionInitMqtt,
    ActionLinkageProtocol,
    ActionNotifyFirmware,
    ActionRestart,
}

/// Eventos que alimentan la FSM.
///
/// Estos son los "Triggers" que provocan los cambios de estado.
#[derive(Debug, Clone, PartialEq)]
pub enum Event {
    EventStart,
    EventOkCli,
    EventNotUpdate,
    EventUpdateError,
    EventUpdateSuccessful,
    EventOkMqtt,
    EventLinkageOk,
    EventFromInitToStore,
    EventFromInitToNormal,
    EventFromInitToBalance,
    EventFromInitToSafe,

    EventFromStoreToNormal,
    EventFromStoreToBypass,
    EventFromStoreToSafe,
    EventFromStoreToInHandshake,
    EventFromStoreToInitBalance,

    EventFromBypassToNormal,
    EventFromBypassToInitBalance,

    EventFromInitBalanceToInHandshake,
    EventFromInitBalanceToAlert,
    EventFromInitBalanceToData,
    EventFromInitBalanceToMonitor,
    EventNewerEpoch,
    EventFromInHandshakeToSafe,
    EventFromInHandshakeToAlert,
    EventFromAlertToData,
    EventFromDataToMonitor,
    EventFromMonitorToOutHandshake,
    EventFromOutHandshakeToSafe,
    EventFromOutHandshakeToNormal,

    EventFromSafeToNormal,
    EventFromSafeToStore,
}

impl FsmState {
    /// Crea una nueva instancia de la FSM en el estado inicial.
    pub fn new() -> Self {
        Self {
            global: StateGlobal::Start,
            init: None,
            store: None,
            balance: None,
        }
    }

    /// Lógica interna de despacho según el estado global.
    fn step_inner(&self, event: Event) -> Transition {
        match self.global {
            StateGlobal::Start => self.step_start(event),
            StateGlobal::Init => self.step_init(event),
            StateGlobal::StoreMessage => self.step_store(event),
            StateGlobal::Normal => self.step_normal(event),
            StateGlobal::Bypass => self.step_bypass(event),
            StateGlobal::Balance => self.step_balance(event),
            StateGlobal::Safe => self.step_safe(event),
        }
    }

    fn step_start(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Start, Event::EventStart) => {
                let mut next_fsm = self.clone();
                next_fsm.global = StateGlobal::Init;
                next_fsm.init = Some(SubStateInit::InitCLI);

                let valid = TransitionValid {
                    change_state: next_fsm,
                    actions: vec![],
                };
                Transition::Valid(valid)
            }
            _ => invalid(),
        }
    }

    fn step_init(&self, event: Event) -> Transition {
        match (&self.init, event) {
            (Some(SubStateInit::InitCLI), Event::EventOkCli) => {
                let next_fsm = self.clone();
                state_init_cli_event_ok_cli(next_fsm)
            }
            (Some(SubStateInit::CheckFirmware), Event::EventNotUpdate | Event::EventUpdateError) => {
                let next_fsm = self.clone();
                state_check_firmware_event_not_update_update_error(next_fsm)
            }
            (Some(SubStateInit::CheckFirmware), Event::EventUpdateSuccessful) => {
                let next_fsm = self.clone();
                state_check_firmware_event_update_successful(next_fsm)
            }
            (Some(SubStateInit::InitMqtt), Event::EventOkMqtt) => {
                let next_fsm = self.clone();
                state_init_mqtt_event_ok_mqtt(next_fsm)
            }
            (Some(SubStateInit::Linkage), Event::EventLinkageOk) => {
                let next_fsm = self.clone();
                state_linkage_event_linkage_ok(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventFromInitToStore) => {
                let next_fsm = self.clone();
                state_init_system_event_to_store(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventFromInitToNormal) => {
                let next_fsm = self.clone();
                state_init_system_event_to_normal(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventFromInitToBalance) => {
                let next_fsm = self.clone();
                state_init_system_to_balance(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventFromInitToSafe) => {
                let next_fsm = self.clone();
                state_init_system_to_safe(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_store(&self, event: Event) -> Transition {
        match (&self.store, event) {
            (Some(SubStateStore::Store, Event::EventFromStoreToNormal)) => {
                let next_fsm = self.clone();
                state_store_event_to_normal(next_fsm)
            }
            (Some(SubStateStore::Cooling, Event::EventFromStoreToBypass)) => {
                let next_fsm = self.clone();
                state_store_event_to_bypass(next_fsm)
            }
            (Some(SubStateStore::UpdateScore, Event::EventFromStoreToSafe)) => {
                let next_fsm = self.clone();
                state_store_event_to_safe(next_fsm)
            }
            (Some(SubStateStore::UpdateScore, Event::EventFromStoreToInitBalance)) => {
                let next_fsm = self.clone();
                state_store_event_to_init_balance(next_fsm)
            }
            (Some(SubStateStore::UpdateScore, Event::EventFromStoreToInHandshake)) => {
                let next_fsm = self.clone();
                state_store_event_to_handshake(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_normal(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Normal, Event::EventFromNormalToCooling) => {
                let next_fsm = self.clone();
                state_normal_event_to_cooling(next_fsm)
            }
            (StateGlobal::Normal, Event::EventFromNormalToInitBalance) => {
                let next_fsm = self.clone();
                state_normal_event_to_init_balance(next_fsm)
            }
            (StateGlobal::Normal, Event::EventFromNormalToInHandshake) => {
                let next_fsm = self.clone();
                state_normal_event_to_handshake(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_bypass(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Bypass, Event::EventFromBypassToNormal) => {
                let next_fsm = self.clone();
                state_bypass_event_to_normal(next_fsm)
            }
            (StateGlobal::Bypass, Event::EventFromBypassToInitBalance) => {
                let next_fsm = self.clone();
                state_bypass_event_to_init_balance(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_balance(&self, event: Event) -> Transition {
        match (&self.balance, event) {
            (Some(SubStateBalance::InitBalanceMode, Event::EventFromInitBalanceToInHandshake)) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_in_handshake(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode, Event::EventFromInitBalanceToAlert)) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_alert(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode, Event::EventFromInitBalanceToData)) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_data(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode, Event::EventFromInitBalanceToMonitor)) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_monitor(next_fsm)
            }
            (Some(SubStateBalance::InHandshake, Event::EventFromInHandshakeToSafe)) => {
                let next_fsm = self.clone();
                state_in_handshake_event_to_safe(next_fsm)
            }
            (Some(SubStateBalance::InHandshake, Event::EventFromInHandshakeToAlert)) => {
                let next_fsm = self.clone();
                state_in_handshake_event_to_alert(next_fsm)
            }
            (Some(SubStateBalance::InHandshake, Event::EventNewerEpoch)) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Alert, Event::EventFromAlertToData)) => {
                let next_fsm = self.clone();
                state_alert_event_to_data(next_fsm)
            }
            (Some(SubStateBalance::Alert, Event::EventNewerEpoch)) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Data, Event::EventFromDataToMonitor)) => {
                let next_fsm = self.clone();
                state_data_event_to_monitor(next_fsm)
            }
            (Some(SubStateBalance::Data, Event::EventNewerEpoch)) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Monitor, Event::EventFromMonitorToOutHandshake)) => {
                let next_fsm = self.clone();
                state_monitor_event_to_out_handshake(next_fsm)
            }
            (Some(SubStateBalance::Monitor, Event::EventNewerEpoch)) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake, Event::EventFromOutHandshakeToSafe)) => {
                let next_fsm = self.clone();
                state_out_handshake_event_to_safe(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake, Event::EventFromOutHandshakeToNormal)) => {
                let next_fsm = self.clone();
                state_out_handshake_event_to_normal(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake, Event::EventNewerEpoch)) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_safe(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Safe, Event::EventFromSafeToNormal) => {
                let next_fsm = self.clone();
                state_safe_event_to_normal(next_fsm)
            }
            (StateGlobal::Safe, Event::EventFromSafeToStore) => {
                let next_fsm = self.clone();
                state_safe_event_to_store(next_fsm)
            }
            _ => invalid(),
        }
    }

    /// Función principal de transición (API Pública).
    ///
    /// 1. Calcula el siguiente estado llamando a `step_inner`.
    /// 2. Si la transición es válida, calcula las acciones de entrada (`compute_on_entry`)
    ///    comparando el estado actual (`self`) con el nuevo (`change_state`) 
    /// 3. Retorna la transición completa con todas las acciones acumuladas.
    pub fn step(&self, event: Event) -> Transition {
        let transition = self.step_inner(event);

        match transition {
            Transition::Valid(mut t) => {
                let entry_actions = compute_on_entry(self, &t.change_state);
                t.actions.extend(entry_actions);
                Transition::Valid(t)
            }
            invalid => invalid,
        }
    }
}

/// Calcula las acciones de entrada (`OnEntry...`) detectando cambios de estado.
///
/// Compara el estado anterior (`old`) con el nuevo (`new`) para identificar
/// qué sub-estados han cambiado e inyectar las acciones correspondientes.
fn compute_on_entry(old: &FsmState, new: &FsmState) -> Vec<Action> {
    let mut actions = Vec::new();

    if old.init != new.init {
        if let Some(state) = &new.init {
            match state {
                SubStateInit::InitCLI => {
                    actions.push(Action::ActionInitCli);
                }
                SubStateInit::CheckFirmware => {
                    actions.push(Action::ActionInitWifi);
                }
                SubStateInit::InitMqtt => {
                    actions.push(Action::ActionInitMqtt);
                }
                SubStateInit::Linkage => {
                    actions.push(Action::ActionLinkageProtocol);
                }
                SubStateInit::InitSystem => {
                    actions.push();
                }
                SubStateInit::NotifyFirmwareUpdated => {
                    actions.push(Action::ActionRestart);
                }
            }
        }
    }

    if old.store != new.store {
        if let Some(state) = &new.store {
            match state {
                SubStateStore::Store => {
                    actions.push(Action::a);
                }
                SubStateStore::Cooling => {
                    actions.push(Action::a);
                }
                SubStateStore::UpdateScore => {
                    actions.push(Action::a);
                }
            }
        }
    }

    if old.global != new.global && new.global == StateGlobal::Normal {
        actions.push(Action::onEntryNormal);
    }

    if old.global != new.global && new.global == StateGlobal::Safe {
        actions.push(Action::onEntrySafe);
    }

    if old.global != new.global && new.global == StateGlobal::Bypass {
        actions.push(Action::onEntryBypass);
    }

    if old.balance != new.balance {
        if let Some(state) = &new.balance {
            match state {
                SubStateBalance::a => {
                    actions.push(Action::a);
                }
            }
        }
    }

    actions
}



// --- Funciones auxiliares de transición ---
// Cada una define un cambio atómico de estado.

/// Retorna una transición inválida genérica para Init.
fn invalid() -> Transition {
    let invalid = TransitionInvalid {
        invalid: "Transición inválida.".to_string(),
    };
    Transition::Invalid(invalid)
}

fn state_init_cli_event_ok_cli(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::CheckFirmware);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_check_firmware_event_not_update_update_error(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::InitMqtt);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_check_firmware_event_update_successful(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::NotifyFirmwareUpdated);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_init_mqtt_event_ok_mqtt(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::Linkage);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_linkage_event_linkage_ok(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::InitSystem);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_init_system_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.init = None;
    next_fsm.store = SubStateStore::Store;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::Valid(valid)
}

fn state_init_system_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.init = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    Transition::valid(valid)
}

fn state_init_system_to_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.init = None;
    next_fsm.balance = SubStateBalance::InitBalanceMode;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_system_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.init = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_store_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.store = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_store_event_to_bypass(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Bypass;
    next_fsm.store = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_store_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.store = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_store_event_to_init_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.store = None;
    next_fsm.balance = SubStateBalance::InitBalanceMode;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_store_event_to_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.store = None;
    next_fsm.balance = SubStateBalance::InHandshake;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_normal_event_to_cooling(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.store = SubStateStore::Cooling;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_normal_event_to_init_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.balance = SubStateStore::InitBalanceMode;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_normal_event_to_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.store = SubStateStore::InHandshake;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_bypass_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_bypass_event_to_init_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.store = SubStateStore::InitBalanceMode;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_balance_event_to_in_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::InHandshake;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_balance_event_to_in_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::InHandshake;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_balance_event_to_alert(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Alert;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_balance_event_to_data(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Data;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_init_balance_event_to_monitor(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Monitor;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_in_handshake_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_in_handshake_event_to_alert(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Alert;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_alert_event_to_data(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Data;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_data_event_to_monitor(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::Monitor;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_monitor_event_to_out_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::OutHandshake;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_out_handshake_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_out_handshake_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn newer_epoch(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = SubStateBalance::InitBalanceMode;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_safe_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}

fn state_safe_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Store;
    next_fsm.store = SubStateStore::Store;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: vec![],
    };
    TransitionValid::valid(valid)
}