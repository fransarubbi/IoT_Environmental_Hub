use crate::{
    app::message::domain::{MessageFromEdge, SerializedMessage},
    bsp::mqtt::IncomingMessage,
};
use std::sync::Mutex;

pub const CORE_POOL_SIZE: usize = 20;

/// Esta estructura contiene el espacio reservado para los datos pesados.
/// Usamos `Option` para saber qué dato está cargado actualmente.
#[derive(Default)]
pub struct CorePayload {
    pub from_edge: Option<MessageFromEdge>,
    pub serialized: Option<SerializedMessage>,
    pub incoming: Option<IncomingMessage>,
}

/// Nuestro Pool Estático Global
pub static CORE_DATA_POOL: [Mutex<CorePayload>; CORE_POOL_SIZE] = [
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
    Mutex::new(CorePayload {
        from_edge: None,
        serialized: None,
        incoming: None,
    }),
];
