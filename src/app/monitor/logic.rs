//! Módulo de Métricas del Sistema (ESP-IDF)
//! Extrae información vital del RTOS.

use crate::{
    app::message::domain::{Metadata, Monitor},
    bsp::wifi::get_unix_epoch,
};
use esp_idf_svc::sys::{
    MALLOC_CAP_8BIT, esp_get_free_heap_size, esp_get_minimum_free_heap_size, esp_timer_get_time,
    heap_caps_get_largest_free_block,
};

/// Extrae las métricas actuales de hardware y memoria de la ESP32
pub async fn monitor(
    sender_id: &str,
    dest_id: &str,
    network_id: &str,
    ssid: &str,
    rssi: i8,
) -> Monitor {
    // Obtenemos los valores desde las capas bajas de C (FreeRTOS / ESP-IDF) de forma segura
    let free_heap = unsafe { esp_get_free_heap_size() };
    let min_free_heap = unsafe { esp_get_minimum_free_heap_size() };

    // El bloque más grande nos indica qué tan fragmentada está la memoria
    let largest_block = unsafe { heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) };

    // El tiempo desde el encendido en microsegundos, lo pasamos a segundos
    let uptime_us = unsafe { esp_timer_get_time() };
    let uptime_sec = (uptime_us / 1_000_000) as u64;

    Monitor {
        metadata: Metadata {
            sender_user_id: sender_id.to_string(),
            destination_id: dest_id.to_string(),
            timestamp: get_unix_epoch(),
        },
        network: network_id.to_string(),
        heap_free: free_heap,
        heap_min_free: min_free_heap,
        heap_largest_block: largest_block as u32,
        uptime_sec,
        wifi_ssid: ssid.to_string(),
        wifi_rssi: rssi,
    }
}
