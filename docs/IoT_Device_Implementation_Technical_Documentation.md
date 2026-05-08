# Compressed Tech Doc — IoT GPS Logistic Package Tracking

**Version:** compressed from v2.2
**Date:** May 6, 2026
**Audience:** Full-stack engineering team
**Stack:** ESP32 firmware · Node.js/Express · MQTT · WebSocket · React · PostgreSQL/PostGIS
**Scope:** Simulated IoT architecture + current firmware verification state

---

## 1. Core Idea

System tracks packages through **devices**, not package sensors.

Package = passive RFID tag only.
Device = GPS + RFID reader + role/facility identity + MQTT publisher.

When package RFID enters reader range, device reads EPC and publishes scan event. Backend consumes scan, resolves EPC to package, updates status/location, broadcasts dashboard updates.

```text
React Dashboard
  ⇅ REST + WebSocket
Node/Express Backend
  - RFID Event Processor
  - Device Registry
  - Package Location Resolver
  - Alert Engine
  - RBAC
  ⇅ MQTT subscribe/publish
Mosquitto Broker
  ⇅ MQTT topics
Fixed + Mobile IoT Simulators
  ⇅ SQL
PostgreSQL + PostGIS
```

---

## 2. Current Implementation Status

### Done / verified May 6, 2026

| Area                    | State                                                                |
| ----------------------- | -------------------------------------------------------------------- |
| Mobile ESP32 firmware   | Implemented, modularized, builds successfully                        |
| Wokwi wiring            | ESP32 + custom GPS + custom PN532 + LEDs configured                  |
| GPS telemetry           | Emits every 5s with fix, lat/lng, speed                              |
| RFID scan path          | PN532 I2C path tested with simulated cards                           |
| Simulated packages      | `DEADBEEF` and `CAFEBABE` mapped to deterministic EPCs               |
| Wokwi scenarios         | 4 scenarios passed: pickup 1, pickup 2, duplicate cooldown, delivery |
| JSONL serial logs       | `log.v1` implemented as machine-readable evidence                    |
| Log analyzer            | Parses JSONL + legacy human logs                                     |
| MQTT mobile topics      | Firmware publishes telemetry/scan/heartbeat; subscribes cmd          |
| Offline event buffer    | RAM buffer up to 50 events; flush on reconnect                       |
| Remote command handlers | `update_role`, `force_scan`, `set_cooldown`, `reboot` implemented    |

### Not done / not verified

| Area                       | State                                     |
| -------------------------- | ----------------------------------------- |
| Fixed device simulator     | Not implemented in repo                   |
| Backend                    | Not implemented in repo                   |
| Frontend                   | Not implemented in repo                   |
| Database schema/deployment | Not implemented in repo                   |
| Broker deployment          | Not implemented in repo                   |
| Backend MQTT integration   | Unverified                                |
| Remote command round trip  | Unverified                                |
| Full architecture MVP      | Not started                               |
| Firmware runtime tests     | Harness exists, not active in normal boot |

**Rule:** Do not claim full-stack or backend complete until repo contains those components and end-to-end verification exists.

---

## 3. Firmware Layout

`src/main.cpp` = thin Arduino entrypoint. Modules own focused work.

| Module              | Owns                                                                               |
| ------------------- | ---------------------------------------------------------------------------------- |
| `firmware_config.h` | Build defaults: identity, credentials, pins, intervals, logging flags              |
| `firmware_state.*`  | Shared runtime state, clients, topics, timers, counters, buffers, package manifest |
| `logging.cpp`       | `log.v1` JSONL + human serial lines                                                |
| `connectivity.cpp`  | WiFi, MQTT, command handling, publish buffering, flush                             |
| `telemetry.cpp`     | GPS telemetry, heartbeat, scan JSON publish                                        |
| `rfid.cpp`          | PN532 scans, cooldown, UID→EPC mapping, context, package counters                  |
| `scenario.cpp`      | Serial Wokwi test commands                                                         |
| `leds.cpp`          | WiFi/MQTT/RFID/GPS LEDs                                                            |
| `utils.cpp`         | IDs + timestamp helpers                                                            |

---

## 4. Verification Commands

```powershell
pio run
```

```powershell
wokwi-cli chip compile chips/pn532.chip.c -o chips/pn532.chip.wasm
wokwi-cli chip compile chips/gps-neo6m.chip.c -o chips/gps-neo6m.chip.wasm
pio run
```

```powershell
wokwi-cli . --timeout 10000 --serial-log-file "logs/wokwi-cli-$(Get-Date -Format 'yyyy-MM-dd_HHmm').log"
```

```powershell
wokwi-cli . --timeout 45000 --scenario scenarios/pickup-package-1.yaml --serial-log-file logs/wokwi-pickup-package-1-2026-05-06-final.log
wokwi-cli . --timeout 60000 --scenario scenarios/pickup-two-packages.yaml --serial-log-file logs/wokwi-pickup-two-packages-2026-05-06-fixed2.log
wokwi-cli . --timeout 45000 --scenario scenarios/duplicate-cooldown.yaml --serial-log-file logs/wokwi-duplicate-cooldown-2026-05-06-fixed.log
wokwi-cli . --timeout 45000 --scenario scenarios/delivery-package-1.yaml --serial-log-file logs/wokwi-delivery-package-1-2026-05-06-fixed.log
```

Analyze logs:

```powershell
python tools/analyze_logs.py logs/manual.log
Get-Content logs\manual.log | python tools/analyze_logs.py -
```

Required after chip edit: recompile matching `.wasm`, then `pio run`.

---

## 5. Simulated RFID Packages

| Card      | UID        | EPC                            |
| --------- | ---------- | ------------------------------ |
| Package 1 | `DEADBEEF` | `LOG-PKG-20260506-JKTWH-00001` |
| Package 2 | `CAFEBABE` | `LOG-PKG-20260506-JKTWH-00002` |

Unknown UID fallback: `LOG-{UID_HEX}`.

Scenario path:

```text
Wokwi YAML
  → PN532 custom-chip control
  → virtual card UID exposed
  → ESP32 reads PN532 over I2C
  → firmware maps UID to EPC
  → firmware publishes/logs scan with context
```

PN532 controls:

| Control | Values | Meaning                 |
| ------- | ------ | ----------------------- |
| `card1` | `0/1`  | remove/place package 1  |
| `card2` | `0/1`  | remove/place package 2  |
| `reset` | `0/1`  | clear active RFID field |

Serial commands:

| Command               | Effect                                            |
| --------------------- | ------------------------------------------------- |
| `SCENARIO PICKUP`     | next scan context = `pickup`                      |
| `SCENARIO IN_TRANSIT` | next scan context = `in_transit`                  |
| `SCENARIO DELIVERED`  | next scan context = `delivered`                   |
| `SCENARIO RESET`      | clear context, cooldowns, onboard state, counters |
| `FORCE_SCAN`          | clear cooldown, attempt scan immediately          |
| `STATUS`              | emit `state_snapshot` JSONL                       |

Counter rules:

| Context            | Active package count                         |
| ------------------ | -------------------------------------------- |
| `pickup`           | +1 only if known package not already onboard |
| `in_transit`       | unchanged                                    |
| `delivered`        | -1 only if known package onboard             |
| duplicate cooldown | suppressed; unchanged                        |

---

## 6. Architecture Decisions

### Device-centric intelligence

Device is source of scan + location truth. Backend consumes and resolves; package stays passive.

### RFID choice

Recommended real-world tech: **Passive UHF RFID / EPC Gen2 / ISO 18000-6C**.

Why:

| Criterion    | Passive UHF RFID                  |
| ------------ | --------------------------------- |
| Cost         | low: roughly $0.10–$0.50/tag      |
| Power        | no battery                        |
| Range        | 1–12 m                            |
| Industry fit | logistics standard                |
| Simulation   | lower complexity than active RFID |

### Device role assignment

Use hybrid model:

1. Static local config at provisioning: `device_id`, `device_type`, `facility_id`, `role`, coordinates.
2. Remote MQTT command override for reassignment.

Result: device works offline with last role; operator can still reassign remotely.

### Duplicate scan handling

Two layers:

| Layer    |                                          Window | Action                  |
| -------- | ----------------------------------------------: | ----------------------- |
| Firmware |                                     30s per EPC | suppress repeat publish |
| Backend  | 60s per `(device_id, rfid_epc, status_context)` | discard duplicate       |

Manual force scan can bypass cooldown.

---

## 7. Device Model

### Fixed device

Permanent facility reader.

| Field     | Behavior                                |
| --------- | --------------------------------------- |
| GPS       | static provisioned coordinates          |
| RFID      | continuous polling                      |
| Role      | checkpoint: package located at facility |
| Telemetry | no GPS telemetry                        |
| Heartbeat | every 60s                               |

### Mobile device

Truck/van reader.

| Field     | Behavior                                        |
| --------- | ----------------------------------------------- |
| GPS       | dynamic, every 5s                               |
| RFID      | load/unload events + low-frequency transit scan |
| Role      | carrier: packages inherit truck GPS             |
| Telemetry | every 5s                                        |
| Heartbeat | every 60s                                       |

### Lifecycle

```text
PROVISIONED → ONLINE → OFFLINE if heartbeat missed >90s → RECONNECTING → ONLINE
```

### Identity config

```json
{
  "device_id": "DEV-JKTWH-FIXED-001",
  "device_type": "fixed",
  "facility_id": "jkt-wh-01",
  "role": "warehouse_gate",
  "location_name": "Jakarta Warehouse — Gate A",
  "fixed_lat": -6.2088,
  "fixed_lng": 106.8456,
  "rfid_read_range_m": 5.0,
  "provisioned_at": "2026-04-01T00:00:00Z"
}
```

---

## 8. MQTT Protocol

Base topic:

```text
logistics/{device_type}/{device_id}/{event_type}
```

| Topic                                    | Publisher     | Consumer                |
| ---------------------------------------- | ------------- | ----------------------- |
| `logistics/fixed/{device_id}/scan`       | fixed device  | RFID Event Processor    |
| `logistics/mobile/{device_id}/scan`      | mobile device | RFID Event Processor    |
| `logistics/mobile/{device_id}/telemetry` | mobile device | MQTT Subscriber Service |
| `logistics/+/{device_id}/heartbeat`      | any device    | Device Registry         |
| `logistics/+/{device_id}/cmd`            | backend       | device                  |
| `logistics/alerts/{tracking_id}`         | Alert Engine  | WebSocket Server        |

QoS:

| Message        | QoS |
| -------------- | --: |
| RFID scan      |   2 |
| GPS telemetry  |   1 |
| Heartbeat      |   0 |
| Remote command |   2 |

Broker: Mosquitto 2.x. Payloads: JSON UTF-8, max 4 KB.

---

## 9. Core Payloads

### Scan event

```json
{
  "schema_version": "2.0",
  "event_id": "SCAN-DEV-JKTWH-FIXED-001-000198",
  "device_id": "DEV-JKTWH-FIXED-001",
  "device_type": "fixed",
  "device_role": "warehouse_gate",
  "facility_id": "jkt-wh-01",
  "location_name": "Jakarta Warehouse — Gate A",
  "device_gps": { "lat": -6.2088, "lng": 106.8456 },
  "timestamp_utc": "2026-04-18T08:30:00.000Z",
  "rfid_epc": "LOG-PKG-20260418-JKTWH-00042",
  "scan_context": "pickup",
  "signal_strength_dbm": -62,
  "read_count": 1
}
```

`scan_context` values:

```text
arrival | pickup | in_transit | hub_transfer | out_for_delivery | delivered | exception
```

### Mobile telemetry

```json
{
  "schema_version": "2.0",
  "device_id": "DEV-TRUCK-007",
  "device_type": "mobile",
  "facility_id": "jkt-wh-01",
  "timestamp_utc": "2026-04-18T10:23:45.000Z",
  "sequence_no": 142,
  "gps": {
    "lat": -6.3241,
    "lng": 107.1089,
    "altitude_m": 12.0,
    "accuracy_m": 4.5,
    "heading_deg": 87.3,
    "speed_kmh": 42.0
  },
  "active_package_count": 4,
  "battery_pct": 88,
  "signal_strength": "good"
}
```

### Heartbeat

```json
{
  "schema_version": "2.0",
  "device_id": "DEV-JKTWH-FIXED-001",
  "device_type": "fixed",
  "device_role": "warehouse_gate",
  "facility_id": "jkt-wh-01",
  "timestamp_utc": "2026-04-18T10:00:00.000Z",
  "uptime_sec": 86400,
  "status": "online",
  "rfid_reader_status": "healthy",
  "packages_scanned_today": 37
}
```

### Remote command

```json
{
  "schema_version": "2.0",
  "command_id": "CMD-20260418-0012",
  "issued_by": "operator-user-001",
  "timestamp_utc": "2026-04-18T11:00:00.000Z",
  "command": "update_role",
  "payload": {
    "new_role": "hub_scanner",
    "new_facility_id": "bks-hub-01",
    "new_location_name": "Bekasi Transit Hub — Gate B"
  }
}
```

Supported commands: `update_role`, `force_scan`, `set_cooldown`, `reboot`.

---

## 10. Package Location Logic

Package has no onboard GPS.

```text
if last_scan_device.type == mobile:
  package.location = device.live_gps
  package.location_mode = inherited

if last_scan_device.type == fixed:
  package.location = device.fixed_gps
  package.location_mode = last_scan
```

Mobile telemetry pipeline:

```text
Mobile telemetry every 5s
  → validate
  → persist device GPS
  → update Device Registry
  → find packages aboard device
  → update package lat/lng
  → geofence check
  → ETA recalc
  → WebSocket broadcast
```

RFID scan pipeline:

```text
Scan event
  → validate schema
  → dedup check
  → resolve EPC to tracking_id
  → status transition from scan_context
  → persist status event + package update
  → stamp/inherit location
  → alert checks
  → WebSocket broadcast
```

Status mapping:

| scan_context       | package status         |
| ------------------ | ---------------------- |
| `arrival`          | `at_facility`          |
| `pickup`           | `in_transit`           |
| `in_transit`       | `in_transit` refreshed |
| `hub_transfer`     | `at_hub`               |
| `out_for_delivery` | `out_for_delivery`     |
| `delivered`        | `delivered`            |
| `exception`        | `exception`            |

ETA for mobile packages:

```text
remaining_distance = sum(Haversine(current GPS → remaining route waypoints))
eta = now + remaining_distance / current_speed_kmh
```

---

## 11. IDs

| Entity        | Format                                  | Example                            |
| ------------- | --------------------------------------- | ---------------------------------- |
| Device        | `DEV-{FACILITY_CODE}-{TYPE_CODE}-{SEQ}` | `DEV-JKTWH-FIXED-001`              |
| Mobile device | `DEV-TRUCK-{SEQ}` / `DEV-VAN-{SEQ}`     | `DEV-TRUCK-007`                    |
| RFID EPC      | `LOG-{TRACKING_ID}`                     | `LOG-PKG-20260418-JKTWH-00042`     |
| Package       | `PKG-{YYYYMMDD}-{FACILITY_CODE}-{SEQ}`  | `PKG-20260418-JKTWH-00042`         |
| Scan event    | `SCAN-{DEVICE_ID}-{SEQ}`                | `SCAN-DEV-JKTWH-FIXED-001-000198`  |
| Facility      | `{CITY_CODE}-{TYPE}-{SEQ}`              | `jkt-wh-01`                        |
| Route         | `ROUTE-{ORIGIN}-{DEST}-{VERSION}`       | `ROUTE-JKTWH-BKSHUB-v2`            |
| Alert         | `ALT-{YYYYMMDD}-{SEQ}`                  | `ALT-20260418-0034`                |
| Status event  | `EVT-{TRACKING_ID}-{SEQ}`               | `EVT-PKG-20260418-JKTWH-00042-005` |

EPC resolution:

```text
rfid_epc received
  → strip LOG-
  → validate PKG pattern
  → lookup package
  → if missing: quarantine unknown scan + alert
```

---

## 12. Backend Components

| Component                 | Responsibility                                           |
| ------------------------- | -------------------------------------------------------- |
| RFID Event Processor      | consume scans, dedup, EPC resolution, status transitions |
| Package Location Resolver | fixed = stamp; mobile = inherit live GPS                 |
| Device Registry           | provisioned devices, roles, status, heartbeat state      |
| MQTT Subscriber Service   | consume telemetry + heartbeat                            |
| Alert Engine              | missing scans, exceptions, geofence, delivered alerts    |
| Geofence Evaluator        | PostGIS point-in-polygon checks                          |
| Auto ID Service           | generate/validate devices, packages, RFID EPCs, events   |
| REST API                  | package CRUD plus device/raw-event/timeline endpoints; geofence, route, user management remain future work |
| WebSocket Server          | live telemetry/package/alert push                        |
| RBAC Middleware           | scope API + WebSocket access                             |

MVP deployment target:

```text
docker-compose.yml
├── mqtt-broker
├── fixed-device-sim
├── mobile-device-sim
├── backend-api
├── postgres
└── frontend
```

Only backend API `4000` and frontend `3000` exposed to host.

---

## 13. Error Handling

| Failure                  | Detection                       | Response                                                 |
| ------------------------ | ------------------------------- | -------------------------------------------------------- |
| missed heartbeat >90s    | last heartbeat monitor          | mark offline, operator alert, package location uncertain |
| reconnect                | heartbeat resumes               | mark online, dismiss alert, resume tracking              |
| GPS accuracy >50m        | telemetry check                 | mark low accuracy, pause ETA                             |
| unknown EPC              | EPC lookup miss                 | quarantine + exception alert                             |
| duplicate scan           | cooldown windows                | suppress/discard                                         |
| expected package missing | manifest/time-window check      | missing-scan alert                                       |
| wrong facility           | scan facility != expected route | exception status + alert                                 |
| broker unreachable       | device publish failure          | buffer last 50 scan events                               |
| broker crash             | persistent sessions             | QoS 1/2 redelivery                                       |
| backend crash            | Docker restart + QoS            | redelivery on reconnect                                  |
| DB write failure         | write exception                 | dead-letter buffer max 500 + retry backoff               |

---

## 14. Security

Auth layers:

| Layer           | Control                                    |
| --------------- | ------------------------------------------ |
| Dashboard       | username/password + bcrypt + JWT HS256, 8h |
| REST            | Bearer JWT + RBAC                          |
| WebSocket       | JWT handshake + scoped subscriptions       |
| MQTT devices    | per-device username/password + broker ACL  |
| Public tracking | no auth, rate-limited, no PII              |

MQTT ACL:

| Client        | Publish                              | Subscribe     |
| ------------- | ------------------------------------ | ------------- |
| Fixed device  | `logistics/fixed/{own_device_id}/#`  | own `/cmd`    |
| Mobile device | `logistics/mobile/{own_device_id}/#` | own `/cmd`    |
| Backend       | alerts + cmd topics                  | `logistics/#` |

RBAC:

| Action                  | Operator | Warehouse     | Customer   |
| ----------------------- | -------- | ------------- | ---------- |
| view all devices        | yes      | no            | no         |
| view all packages       | yes      | facility only | no         |
| view one package        | yes      | yes           | public/own |
| provision/update device | yes      | no            | no         |
| send command            | yes      | no            | no         |
| create package          | yes      | yes           | no         |
| override status         | yes      | no            | no         |
| view alerts             | yes      | facility      | own        |
| configure geofence      | yes      | no            | no         |

---

## 15. High-Risk Integration Checks

Before marking MVP complete:

1. Implement backend MQTT subscriber.
2. Capture real broker payloads for scan/telemetry/heartbeat.
3. Persist scan/status/device GPS events.
4. Verify EPC → tracking ID lookup.
5. Verify duplicate suppression backend-side.
6. Verify package location inheritance from mobile GPS.
7. Verify WebSocket dashboard updates.
8. Test remote command round trip through MQTT `/cmd`.
9. Add fixed-device simulator or adjust architecture claim.
10. Activate/automate firmware tests if required for CI.

---

## 16. Glossary

| Term                      | Meaning                                                |
| ------------------------- | ------------------------------------------------------ |
| EPC                       | RFID tag code containing package tracking identity     |
| EPC Gen2                  | Passive UHF RFID logistics standard                    |
| Passive RFID              | batteryless tag powered by reader field                |
| UHF RFID                  | 860–960 MHz RFID, 1–12 m range                         |
| Scan context              | logical scan meaning: arrival, pickup, delivered, etc. |
| Fixed device              | static facility scanner                                |
| Mobile device             | truck/van scanner with live GPS                        |
| Device identity file      | local provisioned device config                        |
| Package Location Resolver | backend module deriving package location               |
| RFID Event Processor      | scan consumer/dedup/resolver/status updater            |
| Dedup window              | time window suppressing repeat tag reads               |
| Heartbeat                 | periodic online health MQTT event                      |
| Device Registry           | canonical device state service                         |
| QoS                       | MQTT delivery guarantee                                |
| Geofence                  | spatial boundary for alerts                            |
| PostGIS                   | PostgreSQL spatial extension                           |
| MQTT ACL                  | broker topic permission rules                          |
| Dead-letter buffer        | retry queue for failed writes                          |

---

## 17. Owner / Review

**Owner:** Engineering Team
**Source version:** 2.2 — replaces v1.0 per-package GPS simulator model
**Related:** PRD — GPS-Based Logistic Package Tracking System MVP v1.0
**Next review:** after device simulator implementation kickoff
