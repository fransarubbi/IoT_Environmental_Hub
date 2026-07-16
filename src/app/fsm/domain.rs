use heapless::{String, Vec};
use serde::{Deserialize, Serialize};

pub const ACTION_VECTOR_CAPACITY: usize = 15;

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
    CheckFirmware,
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

#[derive(Clone, PartialEq, Eq)]
pub enum StateGeneral {
    InitSystem,
    Store,
    Cooling,
    UpdateScore,
    Bypass,
    Safe { frequency: u32, jitter: u32 },
    Normal,
    InitBalance,
    InHandshake,
    Alert { frequency: u32, jitter: u32 },
    Data { frequency: u32, jitter: u32 },
    Monitor { frequency: u32, jitter: u32 },
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
    actions: Vec<Action, ACTION_VECTOR_CAPACITY>,
}

impl TransitionValid {
    pub fn change_state(&self) -> FsmState {
        self.change_state.clone()
    }
    pub fn actions(&self) -> Vec<Action, ACTION_VECTOR_CAPACITY> {
        self.actions.clone()
    }
}

/// Datos resultantes de una transición fallida o no permitida.
#[derive(Debug)]
pub struct TransitionInvalid {
    invalid: String<20>,
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
    OnEntryCheckFirmware,
    OnEntryLinkageProtocol,
    OnEntryNotifyFirmware,
    OnEntryRestart,
    OnEntryInitSystem,
    OnEntryStore,
    OnEntryCooling,
    OnEntryUpdateScore,
    OnEntryNormal,
    OnEntryBypass,
    OnEntrySafe,
    OnEntryAlert,
    OnEntryData,
    OnEntryMonitor,
    OnEntryInHandshake,
    OnEntryOutHandshake,
    OnEntryInitBalance,
}

/// Eventos que alimentan la FSM.
///
/// Estos son los "Triggers" que provocan los cambios de estado.
#[derive(Debug, Clone, PartialEq)]
pub enum Event {
    EventStart,
    EventNotUpdate,
    EventUpdateSuccessful,
    EventLinkageOk,
    EventEdgeIsDead,
    EventInitBalance,
    EventToSafe,
    EventToNormal,
    EventLowScore,
    EventBadScore,
    EventTimeoutCooling,
    EventGoodScore,
    EventTimeoutBypass,
    EventAlertGenerated,
    EventNewerEpoch,
    EventToAlert,
    EventToData,
    EventToMonitor,
    EventToInHandshake,
    EventToOutHandshake,
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
                next_fsm.init = Some(SubStateInit::CheckFirmware);

                let valid = TransitionValid {
                    change_state: next_fsm,
                    actions: Vec::new(),
                };
                Transition::Valid(valid)
            }
            _ => invalid(),
        }
    }

    fn step_init(&self, event: Event) -> Transition {
        match (&self.init, event) {
            (Some(SubStateInit::CheckFirmware), Event::EventNotUpdate) => {
                let next_fsm = self.clone();
                state_check_firmware_event_not_update(next_fsm)
            }
            (Some(SubStateInit::CheckFirmware), Event::EventUpdateSuccessful) => {
                let next_fsm = self.clone();
                state_check_firmware_event_update_successful(next_fsm)
            }
            (Some(SubStateInit::Linkage), Event::EventLinkageOk) => {
                let next_fsm = self.clone();
                state_linkage_event_linkage_ok(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                state_init_system_event_to_store(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventToNormal) => {
                let next_fsm = self.clone();
                state_init_system_event_to_normal(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventInitBalance) => {
                let next_fsm = self.clone();
                state_init_system_to_balance(next_fsm)
            }
            (Some(SubStateInit::InitSystem), Event::EventToSafe) => {
                let next_fsm = self.clone();
                state_init_system_to_safe(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_store(&self, event: Event) -> Transition {
        match (&self.store, event) {
            (Some(SubStateStore::Store), Event::EventToNormal) => {
                let next_fsm = self.clone();
                state_store_event_to_normal(next_fsm)
            }
            (Some(SubStateStore::Cooling), Event::EventAlertGenerated) => {
                let next_fsm = self.clone();
                state_store_event_to_bypass(next_fsm)
            }
            (Some(SubStateStore::Cooling), Event::EventTimeoutCooling) => {
                let next_fsm = self.clone();
                state_store_event_to_update(next_fsm)
            }
            (Some(SubStateStore::UpdateScore), Event::EventLowScore) => {
                let next_fsm = self.clone();
                state_store_event_to_cooling(next_fsm)
            }
            (Some(SubStateStore::UpdateScore), Event::EventBadScore) => {
                let next_fsm = self.clone();
                state_update_event_to_store(next_fsm)
            }
            (Some(SubStateStore::UpdateScore), Event::EventInitBalance) => {
                let next_fsm = self.clone();
                state_update_event_to_balance(next_fsm)
            }
            (Some(SubStateStore::UpdateScore), Event::EventAlertGenerated) => {
                let next_fsm = self.clone();
                state_update_event_to_bypass(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_normal(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Normal, Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                state_normal_event_to_store(next_fsm)
            }
            (StateGlobal::Normal, Event::EventInitBalance) => {
                let next_fsm = self.clone();
                state_normal_event_to_init_balance(next_fsm)
            }
            (StateGlobal::Normal, Event::EventLowScore) => {
                let next_fsm = self.clone();
                state_normal_event_to_cooling(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_bypass(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Bypass, Event::EventTimeoutBypass) => {
                let next_fsm = self.clone();
                state_bypass_event_to_normal(next_fsm)
            }
            (StateGlobal::Bypass, Event::EventInitBalance) => {
                let next_fsm = self.clone();
                state_bypass_event_to_init_balance(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_balance(&self, event: Event) -> Transition {
        match (&self.balance, event) {
            (Some(SubStateBalance::InitBalanceMode), Event::EventToInHandshake) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_in_handshake(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode), Event::EventToAlert) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_alert(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode), Event::EventToData) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_data(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode), Event::EventToMonitor) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_monitor(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode), Event::EventToSafe) => {
                let next_fsm = self.clone();
                state_init_balance_event_to_safe(next_fsm)
            }
            (Some(SubStateBalance::InitBalanceMode), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            (Some(SubStateBalance::InHandshake), Event::EventToSafe) => {
                let next_fsm = self.clone();
                state_in_handshake_event_to_safe(next_fsm)
            }
            (Some(SubStateBalance::InHandshake), Event::EventToAlert) => {
                let next_fsm = self.clone();
                state_in_handshake_event_to_alert(next_fsm)
            }
            (Some(SubStateBalance::InHandshake), Event::EventNewerEpoch) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::InHandshake), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            (Some(SubStateBalance::Alert), Event::EventToData) => {
                let next_fsm = self.clone();
                state_alert_event_to_data(next_fsm)
            }
            (Some(SubStateBalance::Alert), Event::EventNewerEpoch) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Alert), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            (Some(SubStateBalance::Data), Event::EventToMonitor) => {
                let next_fsm = self.clone();
                state_data_event_to_monitor(next_fsm)
            }
            (Some(SubStateBalance::Data), Event::EventNewerEpoch) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Data), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            (Some(SubStateBalance::Monitor), Event::EventToOutHandshake) => {
                let next_fsm = self.clone();
                state_monitor_event_to_out_handshake(next_fsm)
            }
            (Some(SubStateBalance::Monitor), Event::EventNewerEpoch) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::Monitor), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake), Event::EventToSafe) => {
                let next_fsm = self.clone();
                state_out_handshake_event_to_safe(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake), Event::EventToNormal) => {
                let next_fsm = self.clone();
                state_out_handshake_event_to_normal(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake), Event::EventNewerEpoch) => {
                let next_fsm = self.clone();
                newer_epoch(next_fsm)
            }
            (Some(SubStateBalance::OutHandshake), Event::EventEdgeIsDead) => {
                let next_fsm = self.clone();
                event_to_store(next_fsm)
            }
            _ => invalid(),
        }
    }

    fn step_safe(&self, event: Event) -> Transition {
        match (&self.global, event) {
            (StateGlobal::Safe, Event::EventToNormal) => {
                let next_fsm = self.clone();
                state_safe_event_to_normal(next_fsm)
            }
            (StateGlobal::Safe, Event::EventEdgeIsDead) => {
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
fn compute_on_entry(old: &FsmState, new: &FsmState) -> Vec<Action, ACTION_VECTOR_CAPACITY> {
    let mut actions = Vec::new();

    if old.init != new.init {
        if let Some(state) = &new.init {
            match state {
                SubStateInit::CheckFirmware => {
                    actions
                        .push(Action::OnEntryCheckFirmware)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateInit::Linkage => {
                    actions
                        .push(Action::OnEntryLinkageProtocol)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateInit::InitSystem => {
                    actions
                        .push(Action::OnEntryInitSystem)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateInit::NotifyFirmwareUpdated => {
                    actions
                        .push(Action::OnEntryRestart)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
            }
        }
    }

    if old.store != new.store {
        if let Some(state) = &new.store {
            match state {
                SubStateStore::Store => {
                    actions
                        .push(Action::OnEntryStore)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateStore::Cooling => {
                    actions
                        .push(Action::OnEntryCooling)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateStore::UpdateScore => {
                    actions
                        .push(Action::OnEntryUpdateScore)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
            }
        }
    }

    if old.global != new.global && new.global == StateGlobal::Normal {
        actions
            .push(Action::OnEntryNormal)
            .expect("ACTION_VECTOR_CAPACITY demasiado chico");
    }

    if old.global != new.global && new.global == StateGlobal::Safe {
        actions
            .push(Action::OnEntrySafe)
            .expect("ACTION_VECTOR_CAPACITY demasiado chico");
    }

    if old.global != new.global && new.global == StateGlobal::Bypass {
        actions
            .push(Action::OnEntryBypass)
            .expect("ACTION_VECTOR_CAPACITY demasiado chico");
    }

    if old.balance != new.balance {
        if let Some(state) = &new.balance {
            match state {
                SubStateBalance::Alert => {
                    actions
                        .push(Action::OnEntryAlert)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateBalance::Data => {
                    actions
                        .push(Action::OnEntryData)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateBalance::Monitor => {
                    actions
                        .push(Action::OnEntryMonitor)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateBalance::InHandshake => {
                    actions
                        .push(Action::OnEntryInHandshake)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateBalance::OutHandshake => {
                    actions
                        .push(Action::OnEntryOutHandshake)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
                }
                SubStateBalance::InitBalanceMode => {
                    actions
                        .push(Action::OnEntryInitBalance)
                        .expect("ACTION_VECTOR_CAPACITY demasiado chico");
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
        invalid: String::<20>::try_from("Transición inválida.").unwrap(),
    };
    Transition::Invalid(invalid)
}

fn state_check_firmware_event_not_update(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::Linkage);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_check_firmware_event_update_successful(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::NotifyFirmwareUpdated);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_linkage_event_linkage_ok(mut next_fsm: FsmState) -> Transition {
    next_fsm.init = Some(SubStateInit::InitSystem);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_system_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.init = None;
    next_fsm.store = Some(SubStateStore::Store);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_system_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.init = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_system_to_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.init = None;
    next_fsm.balance = Some(SubStateBalance::InitBalanceMode);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_system_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.init = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_store_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.store = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_store_event_to_bypass(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Bypass;
    next_fsm.store = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_store_event_to_update(mut next_fsm: FsmState) -> Transition {
    next_fsm.store = Some(SubStateStore::UpdateScore);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_store_event_to_cooling(mut next_fsm: FsmState) -> Transition {
    next_fsm.store = Some(SubStateStore::Cooling);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_update_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.store = Some(SubStateStore::Store);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_update_event_to_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.store = None;
    next_fsm.global = StateGlobal::Balance;
    next_fsm.balance = Some(SubStateBalance::InitBalanceMode);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_update_event_to_bypass(mut next_fsm: FsmState) -> Transition {
    next_fsm.store = None;
    next_fsm.global = StateGlobal::Bypass;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_normal_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.store = Some(SubStateStore::Store);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_normal_event_to_init_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.balance = Some(SubStateBalance::InitBalanceMode);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_normal_event_to_cooling(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.store = Some(SubStateStore::Cooling);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_bypass_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_bypass_event_to_init_balance(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Balance;
    next_fsm.balance = Some(SubStateBalance::InitBalanceMode);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_balance_event_to_in_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::InHandshake);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_balance_event_to_alert(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Alert);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_balance_event_to_data(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Data);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_balance_event_to_monitor(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Monitor);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_init_balance_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_in_handshake_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_in_handshake_event_to_alert(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Alert);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_alert_event_to_data(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Data);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_data_event_to_monitor(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::Monitor);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_monitor_event_to_out_handshake(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::OutHandshake);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_out_handshake_event_to_safe(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Safe;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_out_handshake_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;
    next_fsm.balance = None;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = None;
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.store = Some(SubStateStore::Store);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn newer_epoch(mut next_fsm: FsmState) -> Transition {
    next_fsm.balance = Some(SubStateBalance::InitBalanceMode);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_safe_event_to_normal(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::Normal;

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}

fn state_safe_event_to_store(mut next_fsm: FsmState) -> Transition {
    next_fsm.global = StateGlobal::StoreMessage;
    next_fsm.store = Some(SubStateStore::Store);

    let valid = TransitionValid {
        change_state: next_fsm,
        actions: Vec::new(),
    };
    Transition::Valid(valid)
}
