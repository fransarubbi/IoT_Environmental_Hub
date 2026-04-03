# IoT Environmental Hub

## Summary

IoT Environmental Hub is a C firmware for ESP32 (ESP-IDF/PlatformIO framework) designed to function as an environmental hub: it reads sensors (DHT11, MQ-135, KY-037, etc.), constructs messages, publishes via MQTT over TLS/mTLS, supports configuration via UART, performs OTA updates from GitHub, and maintains a health/telemetry system. The design is intended for deployments in IoT networks with an MQTT broker and edge nodes.

## High-Level Content

- Platform: ESP32 (board `nodemcu-32s`).
- Framework/Build: ESP-IDF (used with PlatformIO). PlatformIO environment: `nodemcu-32s`.
- Language: C with FreeRTOS (tasks, timers, queues, semaphores).
- Communication: WiFi, MQTT over TLS (MQTTS, MQTT v5) with mTLS (CA certificate + client + key) and HTTPS for OTA.
- Storage: NVS to persist settings (SSID, password, MQTT URI, IDS).
- Architecture: Modular (modules based on responsibility), producer-consumer pattern with queues, FSM for hub state, and use of event groups for synchronization.
- OTA: Supports OTA via HTTPS from a release on GitHub; version verification is obtained by reading `version.txt`.

## Main Modules (Summary and Responsibilities)

- System (System/system.h/.c)
- Global initialization orchestration (queues, event groups, drivers).
- Defines global structures (queues, event group, mem, task handle).
- Settings (include/Setting, src/settings.c)
- Configuration via UART (port 0, 115200).
- Available commands (W_SSID, W_PASS, M_URI, NET, EDGE, URL_BYPASS, NAME, SAMPLE, ENERGY, SHOW, EXIT, HELP).
- Persistence in NVS (`device_setting` namespace).
- Generates dynamic MQTT topics (e.g., `iot/{id_network}/hub/{mac}/data`).
- Power mode management (LOW_CONSUMPTION, BALANCED, PERFORMANCE) that changes PM settings (CPU frequency and light_sleep).
- Use of atomics and mutexes for secure access.
- MQTT (include/MQTT, src/mqtt.c)
- Initializes MQTT client with TLS/mTLS (built-in certificates: `ca_crt`, `client1_crt`, `client1_key`).
- Configures MQTT v5, keepalive, last-will (`/devices/esp32/status`), and automatic reconnection.
- Assembles fragmented received messages and sends them to the parser queue.
- Publishing: `mqtt_publish()`; enables subscriptions when the system is ready (`mqtt_enable_subscribe_topics()`).
- OTA (include/OTA, src/ota.c)
- Version check by reading `version.txt` on GitHub (URL in code).
- Simple SemVer comparator; if the remote version is greater than the local version, sends an update flag.
- Download the binary using `esp_https_ota` and apply a secure OTA update (crt bundle).
- Sensors / Drivers
- DHT11 (include/DHT11, src/dht11.c)
- MQ-135 (include/MQ135, src/mq135.c)
- KY-037 (include/KY037, src/ky037.c)
- Converter, ADC helpers (includes `converter.c` files).
- Each driver handles reading and normalization to data structures (message generation in `Message/message.c`).
- Parser / Message (src/parser.c, src/message.c)
- They assemble the payload (using `mpack` for serialization).
- Generation of packets for sending and queuing logic.
- FSM (include/Fsm, src/fsm.c)
- Main hub state machine (e.g., INIT, NORMAL, CHECK_FIRMWARE, OTA, BALANCE_MODE, SAFE_MODE).
- Interacts with `queues.flag` for control steps and with `healthscore`.
- Healthscore / Heartbeat / Monitor (src/healthscore.c, src/heartbeat.c, src/monitor.c)
- Logs health events: disconnections, PUBACKs, latencies, etc.
- Publishes heartbeats and performs periodic monitoring.
- Timer / Time (src/timer.c, src/time.c)
- Handles timers, notifications, and delays based on FreeRTOS.
- Utilities: https_bypass (for communication with specific HTTPS servers), system helpers, converter, etc.

## Software Design and Patterns

- Modularity by responsibility (each "include/X" + "src/X.c" folder implements a layer).
- Producer-consumer: multiple producers (sensors, tasks) generate messages that are queued; sending tasks consume the queues.
- FSM: high-level operational logic is implemented with a state machine (Fsm module).
- Event Groups and Task Notifications: for synchronization between subsystem init (e.g., connected MQTT) and dependent tasks.
- Use of atomic and mutex: to protect shared settings between tasks (prevents data races).
- Robustness:
    - Memory error handling (malloc checks and full queue handling).
    - Retries and reconnection in MQTT with fixed backoff.
- Security:
    - MQTT over TLS (MQTTS) with mTLS: uses a CA + client certificate + key.
    - Prefixes are validated in configuration (`mqtts://` for MQTT, `https://` for URL bypass).

## MQTT: Topics and Conventions

Topics are generated dynamically with `create_mqtt_topics()`:

Publish:
- iot/{id_network}/hub/{mac}/data
- iot/{id_network}/hub/{mac}/alert_air
- iot/{id_network}/hub/{mac}/alert_temp
- iot/{id_network}/hub/{mac}/monitor
- iot/{id_network}/hub/{mac}/hub_setting_ok
- iot/{id_network}/hub/{mac}/hub_firmware_ok
- iot/{id_network}/hub/{mac}/balance_mode_handshake 
- iot/{id_network}/hub/{mac}/setting 
- iot/{id_network}/hub/{mac}/ping 
- iot/{id_network}/hub/{mac}/empty_queue

Listen (from Edge/Controller): 
- iot/{id_edge}/state/normal 
- iot/{id_edge}/state/balance 
- iot/{id_edge}/state/safe 
- iot/{id_edge}/state/phase 
- iot/{id_edge}/handshake 
- iot/{id_edge}/heartbeat 
- iot/{id_network}/new_firmware 
- iot/{id_network}/new_settings_to_hub 
- iot/{id_network}/setting_ok 
- iot/{id_network}/delete_hub 
- iot/{id_network}/active_hub

## Configuration and use (UART & NVS)

- UART port: `UART_NUM_0`, baud 115200.
- Interactive configuration mode: when initializing if there are no settings in NVS, the device enters UART mode to enter: 
- Commands: `W_SSID <ssid>`, `W_PASS <password>`, `M_URI <mqtts://...>`, `NET <red_id>`, `EDGE <edge_id>`, `URL_BYPASS <https://...>`, `NAME <device_name>`, `SAMPLE <rate>`, `ENERGY <0|1|2>`, `SHOW`, `EXIT`, `HELP`.
- Persistence: `setting_save_to_nvs()` saves `cfg_node`, `cfg_net`, `cfg_wifi`, and `cfg_mqtt_uri`.

- Notes:
`ENERGY` selects one of three modes and reconfigures `esp_pm_configure()` with MIN/MID/MAX frequencies (80/160/240 MHz) and light_sleep according to the mode.

## Supported Hardware

Implemented Sensors:
- DHT11 (temperature/humidity)
- MQ-135 (air quality/gas)
- KY-037 (sound sensor)
- ADC/auxiliary conversion in `converter.c`.

## Important Files and Paths

- src/*.c — implementation
- include/*/*.h — public headers per module
- platformio.ini — build and board configuration
- partitions.csv — partition table (see file)
- version.txt — version file in the repository (used by OTA version check)
- CMakeLists.txt / sdkconfig.nodemcu-32s — for compilation with ESP-IDF

## Security and Privacy

- Encrypted communication: MQTT over TLS (mQTTS) and HTTPS for OTA.
- mTLS (client authentication) with certificate and key included in the firmware (file `src/certs`).
- Caution: Including private keys directly in the firmware poses a risk if the firmware is shared. For production, use a key management solution (HSM) or generate credentials per device, not embedded in a public repository.
- Validations: `settings.c` validates mqtts:// and https:// prefixes during configuration.



