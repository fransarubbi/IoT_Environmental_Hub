# IoT Environmental Hub

Embedded firmware for an ESP32 microcontroller that collects environmental data from various sensors and transmits it to an MQTT broker. Built with Rust and ESP-IDF, featuring a mediator pattern architecture with state machines for robust system management.

## Features

- **Multi-Sensor Data Collection**: Reads from DHT11 (humidity/temperature) and KY-037 (sound level) sensors
- **MQTT Communication**: Publishes sensor data to dynamically generated MQTT topics with mTLS support
- **Hierarchical Network**: Connects to an edge node with higher hierarchy for network coordination
- **UART CLI Configuration**: Interactive command-line interface via UART for device configuration
- **Over-The-Air (OTA) Updates**: Firmware updates without physical access to the device
- **Energy Modes**: Configurable CPU frequency and sensor polling rates based on energy profiles
- **Non-Blocking Architecture**: Zero-heap design using static memory pools for message data management
- **Device Heartbeat Detection**: State machine monitoring for detecting when edge nodes go offline
- **Persistent Storage**: NVS (Non-Volatile Storage) for configuration persistence

## Architecture

### Design Pattern
The firmware implements a **mediator pattern** with a hierarchical state machine architecture:

- **Main State Machine**: Central orchestrator that directs all system operations
- **Heartbeat Monitor State Machine**: Dedicated state machine for detecting device connectivity status
- **Task-Based Concurrency**: Multiple tasks coordinating through the mediator

### Memory Management
To avoid dynamic heap allocation, the system uses a **static memory pool** design:
- Heavy message data is stored in a pre-allocated static pool
- A pool index is passed to tasks for efficient data access
- Eliminates runtime memory fragmentation and allocation failures

### Sensor Integration
- **DHT11** (Temperature & Humidity): Uses the RMT (Remote Control) peripheral for precise timing
- **KY-037** (Sound Level): Uses the PCNT (Pulse Counter) peripheral for frequency measurement

### Dynamic MQTT Topics
MQTT topics are generated dynamically based on key configuration parameters, allowing flexible topic hierarchies without hardcoding.

## Technology Stack

- **Language**: Rust 🦀
- **Platform**: ESP32 microcontroller
- **Framework**: ESP-IDF (Espressif IoT Development Framework)
- **Build System**: Cargo with official ESP-IDF Rust tooling
- **Protocol**: MQTT with mTLS
- **Storage**: NVS (Non-Volatile Storage)

## Building and Flashing

### Prerequisites
- Rust toolchain with `esp32` target installed
- ESP-IDF tools (install via `esp-idf.py`)
- USB-to-UART adapter for flashing and CLI access

### Build
```bash
cargo build --release
```

### Flash
```bash
cargo run --release
```

## Configuration

### UART CLI
Connect via UART (115200 baud) to access the configuration interface. Available commands allow configuration of:

- MQTT broker address and credentials
- WiFi SSID and password
- Energy profile (affects CPU frequency and sensor polling rate)
- NVS settings management
- Energy Profiles

The device supports multiple energy modes that directly impact:
- CPU clock frequency
- Sensor polling intervals
- Power consumption

Select the appropriate profile based on your deployment requirements and power constraints.

## Network Hierarchy
The device operates as a child node in a hierarchical network structure:

- Connects to an edge node with higher hierarchy
- Transmits sensor data and system status
- Monitors edge node heartbeats for connectivity detection
- Handles reconnection when edge node becomes unavailable

## OTA Updates
Firmware updates are delivered over-the-air without requiring physical access to the device
