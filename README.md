# Mobile Logistic Device IoT

ESP32 firmware for a simulated mobile logistics tracker. The device represents a truck or delivery van that reads package RFID tags, publishes GPS telemetry, and sends heartbeat and scan events over MQTT.

This repository currently contains the firmware, Wokwi simulation assets, scenarios, and log-analysis tooling. Backend, frontend, database, and broker deployment are documented as research architecture but are not implemented in this repository.

## Features

- ESP32 Arduino firmware for a mobile tracking device.
- Wokwi simulation with ESP32 DevKit, custom NEO-6M GPS chip, custom PN532 RFID chip, and status LEDs.
- MQTT publishing for telemetry, RFID scan, and heartbeat messages.
- MQTT command subscription for device control demonstrations.
- JSONL serial logs for repeatable research evidence.
- Scenario files for pickup, duplicate cooldown, and delivery flows.
- Python log analyzer for Wokwi and manual serial logs.

## Repository Layout

| Path                    | Purpose                                                                  |
| ----------------------- | ------------------------------------------------------------------------ |
| `src/`                  | Firmware modules and Arduino entrypoint.                                 |
| `include/`              | Firmware headers and compile-time configuration.                         |
| `chips/`                | Wokwi custom-chip source and compiled WASM for GPS and PN532 simulation. |
| `scenarios/`            | Wokwi CLI scenario YAML files.                                           |
| `tools/analyze_logs.py` | Serial log analyzer for JSONL and legacy human logs.                     |
| `docs/`                 | Product scope and technical architecture notes.                          |
| `platformio.ini`        | PlatformIO board, libraries, build flags, and monitor settings.          |
| `diagram.json`          | Wokwi circuit definition.                                                |

## Prerequisites

- PlatformIO Core or PlatformIO IDE.
- Wokwi CLI for running simulations and scenarios.
- Python 3.10 or newer for `tools/analyze_logs.py`.
- Network access if testing WiFi/MQTT against the configured broker.

The default build flags in `platformio.ini` use:

- WiFi SSID: `Wokwi-GUEST`
- MQTT broker: `broker.hivemq.com`
- Device ID: `DEV-TRUCK-001`
- Facility ID: `jkt-wh-01`

## Quick Start

Build firmware

```bash
pio run
```

Open the project in Wokwi or run a short CLI simulation

```bash
mkdir -p logs
wokwi-cli . --timeout 10000 --serial-log-file logs/wokwi-manual.log
```

Run a scenario

```bash
mkdir -p logs
wokwi-cli . --timeout 45000 \
 --scenario scenarios/pickup-package-1.yaml \
 --serial-log-file logs/wokwi-pickup-package-1.log
```

Analyze a log

```bash
python tools/analyze_logs.py logs/wokwi-pickup-package-1.log
```

Pipe serial output into the analyzer

```bash
cat logs/wokwi-pickup-package-1.log | python tools/analyze_logs.py -
```

## Common Scenarios

| Scenario            | Command                                                                                                                          |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Pickup package 1    | `wokwi-cli . --timeout 45000 --scenario scenarios\pickup-package-1.yaml --serial-log-file logs\wokwi-pickup-package-1.log`       |
| Pickup two packages | `wokwi-cli . --timeout 60000 --scenario scenarios\pickup-two-packages.yaml --serial-log-file logs\wokwi-pickup-two-packages.log` |
| Duplicate cooldown  | `wokwi-cli . --timeout 45000 --scenario scenarios\duplicate-cooldown.yaml --serial-log-file logs\wokwi-duplicate-cooldown.log`   |
| Delivery package 1  | `wokwi-cli . --timeout 45000 --scenario scenarios\delivery-package-1.yaml --serial-log-file logs\wokwi-delivery-package-1.log`   |

## MQTT Topics

Topics are built from `DEVICE_ID` and the mobile device type:

| Topic                                    | Direction         | Purpose                                 |
| ---------------------------------------- | ----------------- | --------------------------------------- |
| `logistics/mobile/{DEVICE_ID}/telemetry` | device publishes  | GPS telemetry and active package count. |
| `logistics/mobile/{DEVICE_ID}/scan`      | device publishes  | RFID package scan event.                |
| `logistics/mobile/{DEVICE_ID}/heartbeat` | device publishes  | Device liveness and health state.       |
| `logistics/mobile/{DEVICE_ID}/cmd`       | device subscribes | Remote command input.                   |

Publish behavior:

- Telemetry interval: `TELEMETRY_INTERVAL_MS`, default `5000`.
- Heartbeat interval: `HEARTBEAT_INTERVAL_MS`, default `60000`.
- RFID scan interval: `RFID_SCAN_INTERVAL_MS`, default `3000`.
- RFID duplicate cooldown: `RFID_COOLDOWN_MS`, default `30000`.
- Offline event buffer: up to `EVENT_BUFFER_MAX`, default `50`.

## Simulated Packages

| Package   | UID        | EPC                            |
| --------- | ---------- | ------------------------------ |
| Package 1 | `DEADBEEF` | `LOG-PKG-20260506-JKTWH-00001` |
| Package 2 | `CAFEBABE` | `LOG-PKG-20260506-JKTWH-00002` |

Unknown RFID UIDs fall back to `LOG-{UID_HEX}`.

## Serial Scenario Commands

These commands can be sent through the Wokwi serial monitor or Wokwi scenario files:

| Command               | Effect                                                           |
| --------------------- | ---------------------------------------------------------------- |
| `SCENARIO PICKUP`     | Arms the next scan as `pickup`.                                  |
| `SCENARIO IN_TRANSIT` | Arms the next scan as `in_transit`.                              |
| `SCENARIO DELIVERED`  | Arms the next scan as `delivered`.                               |
| `SCENARIO RESET`      | Clears scenario context, cooldowns, onboard state, and counters. |
| `FORCE_SCAN`          | Clears cooldowns and attempts an immediate scan.                 |
| `STATUS`              | Emits a state snapshot log event.                                |

## MQTT Commands

The device accepts JSON commands on `logistics/mobile/{DEVICE_ID}/cmd`.

| Command        | Effect                                                                       |
| -------------- | ---------------------------------------------------------------------------- |
| `update_role`  | Updates device role, facility ID, and location name in runtime state.        |
| `force_scan`   | Clears RFID cooldowns and attempts an immediate scan.                        |
| `set_cooldown` | Acknowledges requested cooldown intent; firmware uses compile-time constant. |
| `reboot`       | Restarts the ESP32 after a short delay.                                      |

Example command payload:

```json
{
  "schema_version": "2.1",
  "command_id": "CMD-TEST-001",
  "command": "force_scan",
  "payload": {}
}
```

## Logging And Evidence

Firmware can emit both human-readable serial logs and machine-readable JSONL logs. The defaults are enabled in `platformio.ini`:

- `LOG_JSONL=1`
- `LOG_HUMAN=1`

Use the analyzer to summarize setup, connectivity, telemetry, RFID scan, cooldown, and scenario evidence:

```powershell
python tools\analyze_logs.py logs\manual.log
```

## Documentation

- [Product Requirements Document](docs/PRD_GPS_Logistic_Package_Tracking_MVP.md)
- [Technical Documentation](docs/IoT_Device_Implementation_Technical_Documentation.md)

## Current Scope

Implemented in this repository:

- Mobile ESP32 firmware.
- Wokwi wiring and custom chips.
- GPS telemetry publishing.
- RFID scan publishing and cooldown handling.
- Heartbeat publishing.
- MQTT command handlers.
- Scenario-driven simulation evidence.
- Serial log analyzer.

Not implemented in this repository:

- Backend MQTT subscriber.
- Database schema and persistence.
- Web dashboard.
- Broker deployment configuration.
- End-to-end full-stack verification.
