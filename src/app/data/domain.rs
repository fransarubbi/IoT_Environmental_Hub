use async_channel::{Receiver, Sender};
use edge_executor::LocalExecutor;
use std::sync::{Arc, RwLock};

use crate::app::system_settings::domain::SystemSettings;

pub struct DataService {
    sender: Sender<DataServiceResponse>,
    receiver: Receiver<DataServiceCommand>,
    settings: Arc<RwLock<SystemSettings>>,
}

pub enum DataServiceResponse {}

pub enum DataServiceCommand {}

impl DataService {
    pub fn new(
        sender: Sender<DataServiceResponse>,
        receiver: Receiver<DataServiceCommand>,
        settings: Arc<RwLock<SystemSettings>>,
    ) -> Self {
        Self {
            sender,
            receiver,
            settings,
        }
    }

    pub async fn run<'a>(self, executor: &'a LocalExecutor<'a>) {
        // Crear vectores para datos del dht11, del mq135 y del ky037. Usarlos como "Base de Datos"
        loop {}
    }
}
