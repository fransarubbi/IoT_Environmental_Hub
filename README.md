# IoT Environmental Hub

IoT Environmental Hub is an embedded system firmware for the ESP32 (ESP-IDF using PlatformIO) that monitors environmental conditions and securely publishes the readings to an MQTT broker using mutual TLS (mTLS). The project combines local sensor acquisition, secure communications, encrypted payloads, persistent configuration, and robust offline buffering so no sensor data is lost when connectivity drops.

This README explains what the project does, how it is organized, the security model, how to build and flash the firmware with PlatformIO, how to configure the device via UART, and how the device behaves at boot and during normal runtime.

---

Table of contents
- Project overview
- Key features
- Supported sensors and hardware
- Software architecture
- Security (mTLS + AES-CTR-256)
- Configuration and NVS
- Boot / interactive configuration flow
- MQTT payload and topics
- Offline buffering and queueing
- Building, flashing and monitoring (PlatformIO + ESP-IDF)
- UART interactive interface
- Troubleshooting
- Contributing and license
- Where to find source details (settings_t, partitions, etc.)

---

Project overview
---------------
This firmware runs on an ESP32 microcontroller and performs the following tasks:
- Reads environmental sensors: air quality (MQ-135), sound level (KY-037), temperature and humidity (DHT11).
- Publishes measurements to a remote MQTT broker over TLS using client and CA certificates embedded in the firmware (mTLS).
- Encrypts message payloads with AES-CTR-256; the AES key is provided interactively via UART.
- Stores device configuration in non-volatile storage (NVS) so the device can automatically reconnect after power cycles.
- Buffers generated messages in a local queue when the broker connection is unavailable and automatically drains the queue when the connection is restored.
- Runs under FreeRTOS with tasks separating sensor sampling, network/MQTT, encryption/serialization, and storage/queueing.

Key features
------------
- Secure MQTT with mTLS (client cert, private key and CA cert embedded / packaged).
- AES-CTR-256 encryption for payload confidentiality; IV is included with each message so the broker can decrypt messages.
- Persistent configuration in NVS (WiFi credentials, MQTT URI, device name, sample interval, etc.).
- Interactive UART console for initial provisioning and runtime configuration.
- Boot-time 20-second prompt to optionally modify stored configuration after power-up.
- Queue-based local buffering to avoid data loss during network / broker outages.
- Remote configuration via broker: the broker can change three runtime fields (sample interval, device name, and topic).
- Detailed telemetry published: IP, MAC, timestamp, device name, sample period, sensor readings, and connected SSID.

Supported sensors and hardware
------------------------------
- ESP32 microcontroller (ESP-IDF framework; built with PlatformIO).
- MQ-135: Air quality / gas sensor (analog output -> ADC).
- KY-037: Sound (microphone module; digital interrupt).
- DHT11: Temperature and humidity (single-wire digital).
- WiFi for network connectivity.

Note: exact pin assignments, ADC channels and sensor wiring are configured in the source. Check the hardware configuration section in the code (or search for sensor init / pin macros) to adapt pin assignments to your hardware.

Software architecture
---------------------
High-level tasks:
- Sensor task(s): periodically sample MQ-135, KY-037 and DHT11 according to the configured sample interval.
- MQTT / Network task: maintain TLS connection to the broker, (re)connect on demand, publish messages.
- Encryption/Serialization task: collect sensor data, serialize into JSON, encrypt payload with AES-CTR-256, attach IV, and push the message to the outbound queue.
- Storage/Queue manager: persist outbound messages to an in-memory queue with a configurable maximum capacity. When the queue is full, the task is blocked.
- UART / CLI task: interactive console for configuring the device and entering the AES key.

FreeRTOS provides scheduling for these tasks and synchronization primitives are used to protect shared resources (NVS, queue, WiFi/MQTT state).

Security: mTLS and AES-CTR-256
-----------------------------
- MQTT connections use TLS with client certificate authentication (mTLS). CA, client certificate and client private key are embedded into the firmware to ensure authenticated, encrypted communication.
- Message payloads are encrypted with AES in CTR mode using a 256-bit key (AES-CTR-256). The AES key is *not* embedded in firmware; it must be entered via UART at provisioning or runtime. This reduces risk if firmware binary is extracted.
- Each encrypted message includes the IV (initialization vector) necessary for decryption. Typically the IV is transmitted alongside the ciphertext (e.g., in the MQTT message structure).
- The broker (or a secure service behind the broker) is expected to know the AES key and be able to decrypt the AES-CTR payloads.
- Always protect UART access and physical access to the device in production, since entering the AES key and other secrets requires local access.

Configuration and NVS
---------------------
- Configuration is stored in NVS (non-volatile storage) to survive power cycles. Typical stored fields include:
  - WiFi SSID
  - WiFi password
  - MQTT URI (broker address and port)
  - Device name / ID
  - Sample interval (sampling period)
  - Destination topic for sensor messages
  - Any other fields present in the firmware's settings structure

- Refer to the source code for the exact fields and types. Search for the settings_t structure in the codebase to see the precise fields and default values.

Partitions and flash layout
---------------------------
- The firmware uses a partitioned flash layout so NVS and other regions use the available flash space appropriately.
- The partition table and sizes are defined in the project's partition CSV or as part of the ESP-IDF project configuration. Adjust the partition sizes if you need larger NVS storage or persistent areas.
- Check the partition table file in the repository to learn the exact layout.

Boot / interactive configuration flow
-------------------------------------
- On boot, the device checks NVS to see if a configuration already exists.
- If a configuration exists, the device gives the user a 20-second window to confirm if they want to modify the stored configuration via the UART interactive console. This allows unattended automatic restarts in production but gives local access for changes when needed.
- If no configuration is found, the device drops into the UART interactive provisioning mode to collect WiFi credentials and other necessary configuration fields.
- The AES encryption key must be provided via UART (it is not stored in firmware). The console accepts the key, which will be kept in RAM for the session (but not permanently stored, unless the code is extended to do so — check security implications).

MQTT payload and topics
-----------------------
The messages published to MQTT include the following information (typical JSON structure; check the code for exact keys and format):

Example (decrypted) JSON payload:
{
  "Dispositivo": "my-esp32",
  "IPv4": "192.168.1.1",
  "WiFi SSID": "myWiFi",
  "MAC": "AA:BB:CC:DD:EE:FF",
  "Fecha": "Domingo 26/Oct/2025 22:51:14",
  "Contador de pulsos de sonido": 4,
  "Maxima duracion de pulso": 12,
  "Temperatura": 30,
  "Humedad": 56,
  "CO2 ppm": 440.00,
  "CO ppm": 10.00,
  "NH3 ppm": 0.70,
  "C6H6 ppm": 0.50,
  "NO2 ppm": 0.02,
  "Sample min": 1
}

When published, the actual MQTT payload is the AES-CTR-256 ciphertext (base64 or binary), accompanied with the IV in a known format so the broker can decrypt and parse the JSON.

Topics:
- The firmware publishes to a configurable topic. The broker may also publish configuration commands to a control topic that the device subscribes to. Remote configuration via broker allows changing:
  - sample interval
  - device name
  - destination topic
- The exact topic naming convention and control topic are configured in settings; check your runtime configuration or source code to see exact topics.

Offline buffering and queueing
-----------------------------
- When the MQTT connection is down, sensor data is still sampled and encrypted, then pushed into a local queue with a maximum capacity (set in code).
- When the connectivity is restored, the queue is drained in FIFO order (or the configured order) and messages are published automatically.
- This ensures minimized data loss when transient network or broker outages occur.
- Be aware of the queue maximum capacity; if the device remains offline for extended periods and the queue is full, newer data will lost.

UART interactive interface
--------------------------
- The device exposes a simple interactive UART console for provisioning and runtime control.
- At a minimum, the console lets you:
  - View current configuration
  - Set WiFi SSID and password
  - Set MQTT URI and topic
  - Set device name
  - Set sample interval
  - Enter the AES encryption key
  - View available commands
- When connected via a serial terminal (115200 8N1 by default unless changed), you will be prompted at boot if a configuration exists. If you want to change settings, respond within the 20-second window.
- The console supports a help command or shows the list of commands interactively (check the firmware output to learn the exact commands).

Where to find the settings structure and other implementation details
--------------------------------------------------------------------
- The exact configuration fields (settings_t) are defined in the source code. Search the repository for settings_t to find the structure and the default values for each field.
- Partition table file and NVS configuration are included in the repository. Look for a partition CSV that sets up the flash layout.
- Certificate and key files are in the repo (or embedded during build). The code shows how certificates are loaded into the TLS stack.

Troubleshooting
---------------
- If the device cannot connect to WiFi:
  - Check SSID/password correctness.
  - Confirm AP is available and in range.
  - Check serial logs for DHCP errors or handshake failures.
- If TLS handshake fails:
  - Verify certificates are correct and match the broker (CA, client cert, key).
  - Check if the broker requires SNI or specific TLS versions.
- If MQTT publishes fail:
  - Confirm the topic, broker address and port.
  - Inspect serial logs for MQTT error codes.
- If encryption/decryption mismatch occurs:
  - Confirm AES key entered via UART matches the one on the broker side.

Final notes
-----------
This document describes the high-level design and operational details of the IoT Environmental Hub firmware. For source-level fields (e.g., the exact contents of settings_t), partition CSV format and the list of console commands, consult the code in the repository. Search for:
- settings_t
- partition table file (partitions.csv)
- uart/console or cli files
- MQTT publish and subscription handlers
- sensor drivers / ADC sampling code

Author
------
Sarubbi Rodoni Franco Ezequiel.

