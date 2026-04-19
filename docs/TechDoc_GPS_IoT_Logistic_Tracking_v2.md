# Technical Documentation

## IoT GPS-Based Logistic Package Tracking System

**Version:** 2.0 (Architecture Revision — Device-Centric RFID Model)

**Audience:** Full-Stack Engineering Team

**Stack:** Node.js + Express · MQTT · WebSocket · React.js

**Date:** April 18, 2026

**Scope:** Simulated IoT — Architecture & Design Reference

> **What changed from v1.0:** The previous architecture assigned one GPS simulator per package. This revision replaces that model with a **device-centric architecture** — one IoT device per area or vehicle, aware of its own GPS position and role. Packages carry passive RFID tags and have no sensors of their own. A device detects packages by scanning their RFID tags when they enter its read range, then publishes that event to the platform.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Architecture Principles & Design Decisions](#2-architecture-principles--design-decisions)
3. [Component Architecture](#3-component-architecture)
4. [Device Model](#4-device-model)
5. [RFID Design](#5-rfid-design)
6. [MQTT PubSub Communication Protocol](#6-mqtt-pubsub-communication-protocol)
7. [Package Telemetry Design](#7-package-telemetry-design)
8. [Auto Identification System](#8-auto-identification-system)
9. [Data Flow & Sequence Diagrams](#9-data-flow--sequence-diagrams)
10. [Component Interaction Matrix](#10-component-interaction-matrix)
11. [Error Handling & Resilience Design](#11-error-handling--resilience-design)
12. [Security Design](#12-security-design)
13. [Glossary](#13-glossary)

---

## 1. System Overview

The GPS-Based Logistic Package Tracking System is a simulated IoT platform modelling the full lifecycle of a physical package — from warehouse registration through GPS-tracked transit to final delivery.

In this architecture, **intelligence lives in the device, not the package.** Each IoT device is installed at a fixed location (warehouse gate, transit hub) or aboard a mobile vehicle (truck, delivery van). The device knows its own GPS coordinates, its assigned role, and the facility it serves. Packages are passive — they carry only an **RFID tag** encoding their tracking ID. When a package enters a device's RFID scan range, the device reads the tag and publishes a scan event to the platform over MQTT.

```
┌──────────────────────────────────────────────────────────────────────┐
│  PRESENTATION LAYER          Web Dashboard (React.js)                │
│  Operator · Warehouse Staff · End Customer (role-scoped views)       │
└────────────────────────────────┬─────────────────────────────────────┘
                                 │ WebSocket (live events)
                                 │ REST API (CRUD, config)
┌────────────────────────────────▼─────────────────────────────────────┐
│  APPLICATION LAYER           Node.js + Express Backend               │
│  REST API · WebSocket Server · Alert Engine · RBAC Middleware        │
│  Package Location Resolver · Device Registry · RFID Event Processor  │
└───────────┬──────────────────────────────────────┬───────────────────┘
            │ MQTT Subscribe                        │ Read / Write
            │                                       │
┌───────────▼──────────────────┐       ┌────────────▼──────────────────┐
│  IOT LAYER                   │       │  DATA LAYER                   │
│                              │       │  PostgreSQL + PostGIS          │
│  ┌─────────────────────────┐ │       │                               │
│  │  Fixed Device Simulator │ │       │  Devices · Packages           │
│  │  (Warehouse / Hub gate) │ │       │  RFID Tags · Scan Events      │
│  └─────────────────────────┘ │       │  Routes · Geofences · Alerts  │
│                              │       │  Status Events · Users        │
│  ┌─────────────────────────┐ │       └───────────────────────────────┘
│  │  Mobile Device Simulator│ │
│  │  (Truck / delivery van) │ │
│  └─────────────────────────┘ │
│                              │
│  MQTT Broker (Mosquitto)     │
└──────────────────────────────┘
```

---

## 2. Architecture Principles & Design Decisions

### 2.1 Device-Centric Intelligence

The IoT device is the sole source of ground truth for location and scanning. It:

- Knows its own GPS coordinates (fixed devices: static config; mobile devices: GPS receiver)
- Knows its role and the facility/vehicle it is bound to
- Detects packages passively via RFID — no package action required
- Publishes all events independently; the backend is purely a consumer

This mirrors how real-world logistics IoT works (e.g., airport baggage systems, warehouse dock gates, last-mile van scanners).

### 2.2 Recommended RFID Type: Passive UHF RFID

**Recommendation: Passive UHF RFID (ISO 18000-6C / EPC Gen2)**

| Criterion                   | Passive UHF RFID                                        | Active RFID          |
| --------------------------- | ------------------------------------------------------- | -------------------- |
| Tag cost                    | Very low (~$0.10–$0.50/tag)                             | High (~$15–$50/tag)  |
| Tag power source            | None (powered by reader field)                          | Battery required     |
| Read range                  | 1–12 metres (suitable for dock gates, truck cargo bays) | Up to 100m           |
| Write capability            | Yes (can encode tracking ID at registration)            | Yes                  |
| Logistics industry standard | ✅ Industry standard (GS1 EPC)                          | Niche use cases only |
| Simulation complexity       | Low                                                     | High                 |

Passive UHF RFID is the dominant standard in global logistics (used by Amazon, FedEx, DHL, port terminals). A reader antenna mounted at a gate or inside a vehicle cargo bay energises tags within range, reads their EPC memory bank, and returns the encoded tracking ID — all without any action from the package itself.

### 2.3 Recommended Device Role Assignment: Hybrid Static + Remote

**Recommendation: Static pre-configuration with remote override capability.**

Each device is provisioned at setup time with a **device identity file** (stored locally on the device) containing its `device_id`, `device_type` (fixed/mobile), `facility_id`, and initial `role`. This eliminates dependency on network connectivity for the device to know who it is.

Additionally, the backend can push a **role update command** via an MQTT command topic, allowing remote reassignment (e.g., reassigning a truck device from Route A to Route B) without physical access to the device.

This hybrid approach gives:

- **Operational resilience** — device works offline with its last known role
- **Operational flexibility** — roles can be updated remotely without field visits

### 2.4 Package Location Model

| Device Type               | Package Location Source                         | Behaviour                                                                                 |
| ------------------------- | ----------------------------------------------- | ----------------------------------------------------------------------------------------- |
| **Mobile** (truck/van)    | Inherits device's live GPS                      | Package moves continuously with the truck; location updates every GPS emit cycle          |
| **Fixed** (warehouse/hub) | Last-scan location = device's fixed coordinates | Package location is stamped at scan time; does not move until next scan by another device |

### 2.5 Duplicate RFID Scan Handling

**Recommendation: Time-windowed deduplication.**

A package that remains within a reader's range will be re-read continuously (typical passive UHF readers scan 100–1000 tags/second). Raw duplicate suppression strategy:

- The device firmware (simulated) enforces a **per-tag cooldown window of 30 seconds**: once a tag EPC is read, it is suppressed from re-publishing for 30 seconds.
- The backend RFID Event Processor applies a **secondary deduplication check**: if an identical `(device_id, rfid_epc, status_context)` tuple arrives within 60 seconds, the duplicate is discarded and not persisted.
- A **forced re-scan** can be triggered manually by an operator via the dashboard, bypassing the cooldown for a specific tag.

This two-layer approach catches duplicates from both firmware-level and network-level sources.

---

## 3. Component Architecture

### 3.1 Component Registry

| Component                 | Type           | Responsibility                                                                                                     |
| ------------------------- | -------------- | ------------------------------------------------------------------------------------------------------------------ |
| Fixed Device Simulator    | IoT Simulation | Simulates a stationary RFID reader + GPS unit at a warehouse or hub; publishes scan events and periodic heartbeats |
| Mobile Device Simulator   | IoT Simulation | Simulates an RFID reader + GPS unit aboard a moving truck; publishes scan events + continuous GPS telemetry        |
| MQTT Broker               | Infrastructure | Routes all MQTT messages between device simulators and backend subscribers                                         |
| RFID Event Processor      | Backend Module | Consumes MQTT scan events; deduplicates; resolves RFID EPC to package tracking ID; triggers status transitions     |
| Package Location Resolver | Backend Module | Determines authoritative package location based on device type (inherit vs. last-scan)                             |
| Device Registry           | Backend Module | Maintains the canonical list of all provisioned devices, their types, roles, facility bindings, and current state  |
| REST API Server           | Backend        | CRUD for packages, devices, geofences, routes, users; device provisioning endpoints                                |
| WebSocket Server          | Backend        | Pushes live device telemetry and package events to dashboard clients                                               |
| Alert Engine              | Backend Module | Evaluates scan and telemetry events against alert rules; publishes notifications                                   |
| Geofence Evaluator        | Backend Module | Determines if a mobile device's GPS position crosses a geofence boundary                                           |
| Auto ID Service           | Backend Module | Generates and validates all structured identifiers (devices, packages, RFID tags, events)                          |
| RBAC Middleware           | Backend Module | Enforces role-based access on all API and WebSocket endpoints                                                      |
| Web Dashboard             | Frontend       | Role-scoped React SPA: live map, package timeline, device status, alerts                                           |
| PostgreSQL + PostGIS      | Database       | Persistent store for all entities; PostGIS for geofence spatial queries                                            |

### 3.2 Deployment Topology (MVP)

```
docker-compose.yml
├── mqtt-broker            (Mosquitto 2.x)
├── fixed-device-sim       (Node.js — simulates N fixed reader devices)
├── mobile-device-sim      (Node.js — simulates M truck-mounted devices)
├── backend-api            (Node.js + Express)
├── postgres               (PostgreSQL 15 + PostGIS)
└── frontend               (React SPA via Nginx)
```

The two simulator containers share a simulated package/route manifest injected at startup via environment config. Only the backend API (port 4000) and frontend (port 3000) are exposed to the host.

---

## 4. Device Model

### 4.1 Device Types

#### Fixed Device

Installed permanently at a physical logistics facility — typically mounted at loading dock gates, conveyor entry points, or warehouse receiving areas.

| Property          | Value                                                                 |
| ----------------- | --------------------------------------------------------------------- |
| GPS coordinates   | Static — set at provisioning; never changes                           |
| RFID scan trigger | Continuous polling; any tag entering antenna range is read            |
| Location role     | Acts as a **checkpoint** — packages scanned here are at this facility |
| GPS telemetry     | Not emitted (location is known and static)                            |
| Heartbeat         | Emitted every 60 seconds                                              |

#### Mobile Device

Installed aboard a vehicle (truck, delivery van). Moves continuously along a delivery route.

| Property          | Value                                                                                                                                           |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| GPS coordinates   | Dynamic — updated from onboard GPS receiver every 5 seconds                                                                                     |
| RFID scan trigger | Event-driven — triggered when cargo bay door closes (load event) or opens (delivery event); also continuous low-frequency scan while in transit |
| Location role     | Acts as a **carrier** — packages aboard inherit the truck's live GPS                                                                            |
| GPS telemetry     | Emitted every 5 seconds while vehicle is active                                                                                                 |
| Heartbeat         | Emitted every 60 seconds                                                                                                                        |

### 4.2 Device Identity & Role Assignment

Each device carries a local identity configuration provisioned at setup:

```
Device Identity File (device.config.json)
├── device_id        : "DEV-JKTWH-FIXED-001"
├── device_type      : "fixed" | "mobile"
├── facility_id      : "jkt-wh-01"
├── role             : "warehouse_gate" | "hub_scanner" | "truck" | "delivery_van"
├── location_name    : "Jakarta Warehouse — Gate A"
├── fixed_lat        : -6.2088          (fixed devices only)
├── fixed_lng        : 106.8456         (fixed devices only)
├── rfid_read_range_m: 5.0              (simulated scan radius)
└── provisioned_at   : "2026-04-01T00:00:00Z"
```

**Remote role update** is received via the device's MQTT command topic. On receipt, the device updates its in-memory role and persists the change to its local config file. The backend Device Registry is updated simultaneously.

### 4.3 Device Lifecycle States

```
  PROVISIONED
      │
      ▼
  ONLINE ──────── heartbeat emitting ──────► MQTT Broker
      │
      ├── (fixed) polling RFID range continuously
      │
      ├── (mobile) emitting GPS telemetry every 5s
      │           scanning RFID on load/unload events
      │
      ▼
  OFFLINE (heartbeat missed > 90s)
      │
      ▼
  RECONNECTING (auto-retry with backoff)
      │
      ▼
  ONLINE
```

### 4.4 Simulated Device Behaviour

The simulator containers replicate device behaviour as follows:

**Fixed Device Simulator:**

1. At startup, loads its identity config and list of packages assigned to its facility
2. Emits a heartbeat every 60 seconds
3. At configurable intervals, simulates a package "entering scan range" by selecting a package from its manifest and publishing an RFID scan event
4. Packages depart the facility (removed from manifest) when a truck device picks them up

**Mobile Device Simulator:**

1. At startup, loads its identity config, assigned route (waypoint array), and manifest of packages loaded aboard
2. Begins interpolating GPS position along the route every 5 seconds → publishes GPS telemetry
3. At route start: publishes RFID scan events for all packages loaded (load event)
4. At each delivery waypoint: publishes RFID scan event for package(s) being unloaded (delivery event)
5. Emits heartbeat every 60 seconds

---

## 5. RFID Design

### 5.1 RFID Standard

**Standard: EPC Gen2 UHF RFID (ISO 18000-6C)**

Each package is assigned one passive UHF RFID tag at registration. The tag's **EPC (Electronic Product Code) memory bank** is encoded with the package's tracking ID. The tag has no battery and no active components — it is powered entirely by the electromagnetic field of the reader antenna when within range.

### 5.2 Tag Memory Layout

EPC Gen2 tags have four memory banks. Only the EPC bank is used in this system:

| Memory Bank | Name     | Usage                                                      |
| ----------- | -------- | ---------------------------------------------------------- |
| 00          | Reserved | Lock bits, kill password — not used                        |
| 01          | **EPC**  | **Encoded with package tracking ID**                       |
| 10          | TID      | Tag manufacturer ID — used for tag uniqueness verification |
| 11          | User     | Available for future use (e.g., weight, fragility flags)   |

**EPC Encoding Format:**

The 96-bit EPC memory bank is encoded using GS1's SGTIN-96 scheme adapted for this system:

```
EPC = {HEADER(8)} + {FILTER(3)} + {PARTITION(3)} + {COMPANY(20)} + {ITEM_REF(24)} + {SERIAL(38)}

Simplified simulation encoding (ASCII string in EPC bank):
"LOG-{TRACKING_ID}"

Example: "LOG-PKG-20260418-JKTWH-00042"
```

The backend Auto ID Service resolves the EPC string back to the internal `tracking_id` on every scan event.

### 5.3 RFID Scan Range Simulation

Since there is no physical antenna, scan range is simulated geometrically:

- Each device has a configured `rfid_read_range_m` (e.g., 5 metres for a dock gate, 3 metres for a van cargo bay)
- Each package in the simulator has a simulated physical position relative to the device
- A package is considered "in range" when its simulated distance from the device falls within `rfid_read_range_m`
- The simulator moves packages in and out of range according to the scenario script (arrival at gate, loading onto truck, etc.)

### 5.4 Tag Lifecycle

| Event                     | Tag State   | Description                                                          |
| ------------------------- | ----------- | -------------------------------------------------------------------- |
| Package registered        | Tag encoded | EPC written with tracking ID at warehouse registration terminal      |
| Package in transit        | Tag passive | No signal; tag is inert until energised by a reader                  |
| Package scanned at gate   | Tag read    | Reader energises tag → EPC returned → scan event published           |
| Package delivered         | Tag retired | Tag record marked delivered in DB; physical tag may be recycled      |
| Tag not found (scan miss) | Tag missing | If expected package not scanned within time window → alert triggered |

---

## 6. MQTT PubSub Communication Protocol

### 6.1 Broker Configuration

| Parameter                | Value                                                           |
| ------------------------ | --------------------------------------------------------------- |
| Broker                   | Mosquitto 2.x (MVP)                                             |
| Protocol                 | MQTT v5.0                                                       |
| Internal Port            | 1883 (Docker network only)                                      |
| TLS Port                 | 8883 (external access, future)                                  |
| QoS for RFID scan events | QoS 2 (exactly-once)                                            |
| QoS for GPS telemetry    | QoS 1 (at-least-once)                                           |
| QoS for heartbeat        | QoS 0 (fire-and-forget)                                         |
| Retained messages        | Last GPS telemetry per mobile device; last heartbeat per device |
| Persistent sessions      | Enabled for backend subscriber client                           |
| Authentication           | Username + password per client (device simulators + backend)    |

### 6.2 Topic Hierarchy

```
logistics/{device_type}/{device_id}/{event_type}
```

| Topic Pattern                            | Publisher                 | Subscriber              | Description                                              |
| ---------------------------------------- | ------------------------- | ----------------------- | -------------------------------------------------------- |
| `logistics/fixed/{device_id}/scan`       | Fixed Device Sim          | RFID Event Processor    | RFID scan event from a fixed gate/hub reader             |
| `logistics/mobile/{device_id}/scan`      | Mobile Device Sim         | RFID Event Processor    | RFID scan event from a truck/van reader                  |
| `logistics/mobile/{device_id}/telemetry` | Mobile Device Sim         | MQTT Subscriber Service | Live GPS position of a mobile device (truck)             |
| `logistics/+/{device_id}/heartbeat`      | Any Device Sim            | Device Registry         | Periodic health pulse from any device                    |
| `logistics/+/{device_id}/cmd`            | Backend (Device Registry) | Device Sim              | Remote command channel (role update, force scan, reboot) |
| `logistics/alerts/{tracking_id}`         | Alert Engine              | WebSocket Server        | Alert event for a specific package                       |

**Example topic instances:**

```
logistics/fixed/DEV-JKTWH-FIXED-001/scan
logistics/mobile/DEV-TRUCK-007/telemetry
logistics/mobile/DEV-TRUCK-007/heartbeat
logistics/mobile/DEV-TRUCK-007/cmd
logistics/alerts/PKG-20260418-JKTWH-00042
```

### 6.3 MQTT Payload Schemas

All payloads: JSON, UTF-8, max 4 KB.

#### RFID Scan Event (`/scan`)

Published by any device when a tag enters its read range.

```json
{
  "schema_version": "2.0",
  "event_id": "SCAN-DEV-JKTWH-FIXED-001-000198",
  "device_id": "DEV-JKTWH-FIXED-001",
  "device_type": "fixed",
  "device_role": "warehouse_gate",
  "facility_id": "jkt-wh-01",
  "location_name": "Jakarta Warehouse — Gate A",
  "device_gps": {
    "lat": -6.2088,
    "lng": 106.8456
  },
  "timestamp_utc": "2026-04-18T08:30:00.000Z",
  "rfid_epc": "LOG-PKG-20260418-JKTWH-00042",
  "scan_context": "pickup",
  "signal_strength_dbm": -62,
  "read_count": 1
}
```

**`scan_context` values:**

| Value              | Meaning                                             | Triggered By                         |
| ------------------ | --------------------------------------------------- | ------------------------------------ |
| `arrival`          | Package arrived at fixed facility                   | Fixed device scan                    |
| `pickup`           | Package loaded onto a truck                         | Mobile device scan at departure      |
| `in_transit`       | Periodic confirmation package is still aboard truck | Mobile device scan while moving      |
| `hub_transfer`     | Package transferred between facilities              | Fixed device at transit hub          |
| `out_for_delivery` | Truck has left hub for final delivery leg           | Mobile device scan at hub departure  |
| `delivered`        | Package unloaded at recipient location              | Mobile device scan at delivery point |
| `exception`        | Unexpected scan (wrong facility, unknown tag)       | Any device                           |

#### GPS Telemetry (`/telemetry`) — Mobile Devices Only

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

#### Device Heartbeat (`/heartbeat`)

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

#### Remote Command (`/cmd`) — Backend to Device

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

**Supported commands:**

| Command        | Description                                  |
| -------------- | -------------------------------------------- |
| `update_role`  | Reassign device role and facility            |
| `force_scan`   | Trigger an immediate RFID scan cycle         |
| `set_cooldown` | Update the per-tag duplicate cooldown window |
| `reboot`       | Simulate device restart                      |

#### Alert Payload (`logistics/alerts/{tracking_id}`)

```json
{
  "schema_version": "2.0",
  "alert_id": "ALT-20260418-0034",
  "alert_type": "geofence_breach",
  "severity": "warning",
  "tracking_id": "PKG-20260418-JKTWH-00042",
  "device_id": "DEV-TRUCK-007",
  "timestamp_utc": "2026-04-18T10:45:00.000Z",
  "message": "Truck DEV-TRUCK-007 (carrying PKG-00042) has exited the Jakarta delivery zone.",
  "gps": { "lat": -6.5012, "lng": 107.331 },
  "target_roles": ["operator"],
  "auto_dismiss_after_sec": null
}
```

---

## 7. Package Telemetry Design

### 7.1 How Packages Are Tracked (No Onboard Sensor)

Packages have no sensor or battery. Their location is derived entirely from the device that last interacted with them:

```
Package Location = f(last_scan_device)

IF last_scan_device.type == "mobile":
    package.current_location = device.live_gps   (updates every 5s with truck)
    package.location_mode    = "inherited"

IF last_scan_device.type == "fixed":
    package.current_location = device.fixed_gps  (stamped at scan time, static)
    package.location_mode    = "last_scan"
```

The `Package Location Resolver` runs on every GPS telemetry event from a mobile device to update all packages currently associated with that device.

### 7.2 Package Location Update Pipeline

```
Mobile Device Sim
    │  Publishes GPS telemetry every 5s
    ▼
MQTT Broker
    │
    ▼
MQTT Subscriber Service
    │
    ├── 1. Validate telemetry schema
    ├── 2. Persist device GPS event
    ├── 3. Update device current position in Device Registry
    ├── 4. → Package Location Resolver
    │         Query: "which packages are currently aboard this device?"
    │         Update: set current_lat/lng on all matching packages
    ├── 5. → Geofence Evaluator (check device position against zones)
    ├── 6. → ETA Recalculator (for each aboard package)
    └── 7. → WebSocket broadcast (device position + package positions)
```

### 7.3 RFID Scan Processing Pipeline

```
Any Device Sim
    │  Publishes RFID scan event
    ▼
MQTT Broker
    │
    ▼
RFID Event Processor
    │
    ├── 1. Validate scan event schema
    ├── 2. Deduplication check
    │       └── Is (device_id + rfid_epc) within 60s cooldown window?
    │               YES → discard silently, log metric
    │               NO  → continue
    ├── 3. EPC Resolution
    │       └── Auto ID Service resolves rfid_epc → tracking_id
    │               Not found → publish exception alert, halt
    ├── 4. Determine status transition from scan_context
    │       (e.g. scan_context "pickup" → new_status "in_transit")
    ├── 5. Persist status_event record
    ├── 6. Update package record (status, last_device_id, location_mode)
    ├── 7. → Package Location Resolver (stamp location per device type)
    ├── 8. → Alert Engine (check for alert-triggering contexts)
    └── 9. → WebSocket broadcast (status update + new location)
```

### 7.4 scan_context to Package Status Mapping

| scan_context       | Resulting Package Status | Notes                                                            |
| ------------------ | ------------------------ | ---------------------------------------------------------------- |
| `arrival`          | `at_facility`            | Package checked in at warehouse or hub                           |
| `pickup`           | `in_transit`             | Package loaded onto truck; location now inherited from truck GPS |
| `in_transit`       | `in_transit` (refreshed) | Periodic confirmation; no status change, location updated        |
| `hub_transfer`     | `at_hub`                 | Intermediate facility checkpoint                                 |
| `out_for_delivery` | `out_for_delivery`       | Truck departed hub on final delivery leg                         |
| `delivered`        | `delivered`              | Package unloaded at recipient; simulation ends for this package  |
| `exception`        | `exception`              | Unknown tag, wrong facility, or scan anomaly                     |

### 7.5 ETA Calculation

For packages aboard a mobile device:

```
remaining_distance = sum of Haversine distances from
                     device's current GPS to all remaining route waypoints

eta = now + (remaining_distance / device.current_speed_kmh)
```

For packages at a fixed facility: ETA is not recalculated until the package is picked up by a mobile device.

---

## 8. Auto Identification System

### 8.1 Identifier Formats

#### Device ID

```
DEV-{FACILITY_CODE}-{TYPE_CODE}-{SEQ}

Fixed:  DEV-JKTWH-FIXED-001
Mobile: DEV-TRUCK-007
        DEV-VAN-003
```

| Type Code | Meaning                                          |
| --------- | ------------------------------------------------ |
| `FIXED`   | Fixed installation (warehouse gate, hub scanner) |
| `TRUCK`   | Long-haul truck                                  |
| `VAN`     | Last-mile delivery van                           |

#### RFID EPC (Tag Encoding)

```
LOG-{TRACKING_ID}

Example: LOG-PKG-20260418-JKTWH-00042
```

The `LOG-` prefix namespaces the EPC within the GS1 user-defined range, distinguishing it from commercial product EPCs if a real reader is ever introduced.

#### Package Tracking ID

```
PKG-{YYYYMMDD}-{FACILITY_CODE}-{SEQUENCE}

Example: PKG-20260418-JKTWH-00042
```

#### Scan Event ID

```
SCAN-{DEVICE_ID}-{SEQUENCE}

Example: SCAN-DEV-JKTWH-FIXED-001-000198
```

Sequence is per-device, monotonically incrementing. Enables detection of missed events by the backend.

#### Facility ID

```
{CITY_CODE}-{TYPE}-{SEQ}

jkt-wh-01   Jakarta Warehouse 01
bks-hub-01  Bekasi Transit Hub 01
sby-del-02  Surabaya Delivery Point 02
```

#### Route ID

```
ROUTE-{ORIGIN_FACILITY}-{DEST_FACILITY}-{VERSION}

Example: ROUTE-JKTWH-BKSHUB-v2
```

#### Alert ID & Status Event ID

```
ALT-{YYYYMMDD}-{SEQUENCE}        Example: ALT-20260418-0034
EVT-{TRACKING_ID}-{SEQUENCE}     Example: EVT-PKG-20260418-JKTWH-00042-005
```

### 8.2 EPC Resolution

The Auto ID Service maintains a **EPC-to-TrackingID lookup table** in the database. On every RFID scan event:

1. The `rfid_epc` string is received by the RFID Event Processor
2. Auto ID Service strips the `LOG-` prefix and validates the remaining string against the `PKG-{YYYYMMDD}-{FACILITY}-{SEQ}` pattern
3. The resolved `tracking_id` is looked up in the `packages` table
4. If not found → exception alert is triggered and the event is quarantined in the `unknown_scans` log

### 8.3 Device Provisioning Flow

```
Operator provisions new device via Dashboard
    │
    ▼
REST API → Auto ID Service generates Device ID
    │
    ▼
Device record created in DB (status: provisioned)
    │
    ▼
Device identity config file generated and delivered
(in simulation: injected into simulator container via env/config)
    │
    ▼
Device connects to MQTT broker with its credentials
    │
    ▼
First heartbeat received → Device Registry marks status: online
```

---

## 9. Data Flow & Sequence Diagrams

### 9.1 Package Registration & RFID Tag Encoding

```
Warehouse Staff      REST API          Auto ID Service        Database
      │                  │                   │                    │
      │ POST /packages   │                   │                    │
      │ (sender, recip,  │                   │                    │
      │  origin, dest)   │                   │                    │
      │─────────────────►│                   │                    │
      │                  │ Generate PKG ID   │                    │
      │                  │──────────────────►│                    │
      │                  │ Generate RFID EPC │                    │
      │                  │ "LOG-PKG-..."     │                    │
      │                  │──────────────────►│                    │
      │                  │                   │ Persist package    │
      │                  │                   │ + rfid_tag record  │
      │                  │                   │───────────────────►│
      │                  │ Assign route      │                    │
      │                  │ to package        │                    │
      │◄─────────────────│                   │                    │
      │ Return:          │                   │                    │
      │ - tracking_id    │                   │                    │
      │ - rfid_epc       │                   │                    │
      │ - QR (of pkg ID) │                   │                    │
      │ (Staff prints    │                   │                    │
      │  RFID tag &      │                   │                    │
      │  affixes to pkg) │                   │                    │
```

### 9.2 Package Pickup — Fixed Gate Scan

```
Fixed Device Sim     MQTT Broker    RFID Event Processor    Database     WebSocket Server   Dashboard
      │                  │                  │                   │               │               │
      │ Package enters   │                  │                   │               │               │
      │ gate scan range  │                  │                   │               │               │
      │ Publish /scan    │                  │                   │               │               │
      │ context:"arrival"│                  │                   │               │               │
      │─────────────────►│                  │                   │               │               │
      │                  │ Forward to       │                   │               │               │
      │                  │ subscriber       │                   │               │               │
      │                  │─────────────────►│                   │               │               │
      │                  │                  │ Dedup check       │               │               │
      │                  │                  │ Resolve EPC       │               │               │
      │                  │                  │ → tracking_id     │               │               │
      │                  │                  │ Status: "arrival" │               │               │
      │                  │                  │ → "at_facility"   │               │               │
      │                  │                  │ Persist EVT + PKG │               │               │
      │                  │                  │──────────────────►│               │               │
      │                  │                  │ Location: fixed   │               │               │
      │                  │                  │ device coords     │               │               │
      │                  │                  │──────────────────►│               │               │
      │                  │                  │ Broadcast update  │               │               │
      │                  │                  │──────────────────────────────────►│               │
      │                  │                  │                   │               │ Push to roles │
      │                  │                  │                   │               │──────────────►│
      │                  │                  │                   │               │               │ Map pin updates
      │                  │                  │                   │               │               │ Timeline entry added
```

### 9.3 Package in Transit — Mobile Device Telemetry

```
Mobile Device Sim    MQTT Broker    MQTT Subscriber    Pkg Location Resolver   WebSocket Server   Dashboard
      │                  │                │                     │                     │              │
      │ GPS tick (5s)    │                │                     │                     │              │
      │ Publish /telemetry                │                     │                     │              │
      │─────────────────►│                │                     │                     │              │
      │                  │───────────────►│                     │                     │              │
      │                  │                │ Validate            │                     │              │
      │                  │                │ Persist device GPS  │                     │              │
      │                  │                │ → Resolver          │                     │              │
      │                  │                │────────────────────►│                     │              │
      │                  │                │                     │ Find all packages   │              │
      │                  │                │                     │ aboard this device  │              │
      │                  │                │                     │ Update pkg lat/lng  │              │
      │                  │                │                     │ (inherited mode)    │              │
      │                  │                │                     │ Recalculate ETAs    │              │
      │                  │                │◄────────────────────│                     │              │
      │                  │                │ Geofence check      │                     │              │
      │                  │                │ Broadcast bundle:   │                     │              │
      │                  │                │ device GPS +        │                     │              │
      │                  │                │ all package pos.    │                     │              │
      │                  │                │────────────────────────────────────────► │              │
      │                  │                │                     │                     │─────────────►│
      │                  │                │                     │                     │              │ Truck marker moves
      │                  │                │                     │                     │              │ Package pins follow
```

### 9.4 Delivery Scan — Mobile Device

```
Mobile Device Sim    MQTT Broker    RFID Event Processor    Alert Engine    WebSocket Server   Customer Dashboard
      │                  │                  │                    │                │                   │
      │ Truck arrives at │                  │                    │                │                   │
      │ delivery point   │                  │                    │                │                   │
      │ Scan package RFID│                  │                    │                │                   │
      │ context:"delivered"                 │                    │                │                   │
      │─────────────────►│                  │                    │                │                   │
      │                  │─────────────────►│                    │                │                   │
      │                  │                  │ Dedup check        │                │                   │
      │                  │                  │ Resolve EPC        │                │                   │
      │                  │                  │ Status: "delivered"│                │                   │
      │                  │                  │ Persist EVT + PKG  │                │                   │
      │                  │                  │ Location: truck    │                │                   │
      │                  │                  │ coords at delivery │                │                   │
      │                  │                  │ Disassociate pkg   │                │                   │
      │                  │                  │ from device        │                │                   │
      │                  │                  │ → Alert Engine     │                │                   │
      │                  │                  │───────────────────►│                │                   │
      │                  │                  │                    │ Trigger        │                   │
      │                  │                  │                    │ "delivered"    │                   │
      │                  │                  │                    │ alert          │                   │
      │                  │                  │                    │───────────────►│                   │
      │                  │                  │                    │                │ Push to customer  │
      │                  │                  │                    │                │──────────────────►│
      │                  │                  │                    │                │                   │ "Your package
      │                  │                  │                    │                │                   │  has been delivered"
```

### 9.5 Remote Device Role Update

```
Operator           REST API          Device Registry      MQTT Broker     Device Simulator
    │                  │                   │                   │                 │
    │ PATCH /devices/  │                   │                   │                 │
    │ {id}/role        │                   │                   │                 │
    │─────────────────►│                   │                   │                 │
    │                  │ RBAC check        │                   │                 │
    │                  │ (operator only)   │                   │                 │
    │                  │──────────────────►│                   │                 │
    │                  │                   │ Generate CMD ID   │                 │
    │                  │                   │ Update device     │                 │
    │                  │                   │ record in DB      │                 │
    │                  │                   │ Publish /cmd      │                 │
    │                  │                   │ topic (QoS 2)     │                 │
    │                  │                   │──────────────────►│                 │
    │                  │                   │                   │ Forward to      │
    │                  │                   │                   │ device          │
    │                  │                   │                   │────────────────►│
    │                  │                   │                   │                 │ Apply new role
    │                  │                   │                   │                 │ Update local config
    │                  │                   │                   │                 │ Publish heartbeat
    │                  │                   │                   │◄────────────────│
    │                  │◄──────────────────│                   │                 │
    │ 200 OK           │                   │                   │                 │
    │◄─────────────────│                   │                   │                 │
```

---

## 10. Component Interaction Matrix

| From → To                                           | Protocol       | Direction        | Data Exchanged                                                      |
| --------------------------------------------------- | -------------- | ---------------- | ------------------------------------------------------------------- |
| Fixed Device Sim → MQTT Broker                      | MQTT QoS 2     | Push             | RFID scan events, heartbeats                                        |
| Mobile Device Sim → MQTT Broker                     | MQTT QoS 1/2   | Push             | GPS telemetry (QoS 1), RFID scan events (QoS 2), heartbeats (QoS 0) |
| MQTT Broker → RFID Event Processor                  | MQTT           | Push (sub)       | All `/scan` topic messages                                          |
| MQTT Broker → MQTT Subscriber Service               | MQTT           | Push (sub)       | All `/telemetry` and `/heartbeat` messages                          |
| MQTT Broker → Device Simulator                      | MQTT QoS 2     | Push             | Remote commands via `/cmd` topics                                   |
| RFID Event Processor → Auto ID Service              | Internal call  | Sync             | EPC string → tracking_id resolution                                 |
| RFID Event Processor → Database                     | SQL            | Write            | Scan events, status events, package updates                         |
| RFID Event Processor → Package Location Resolver    | Internal event | Push             | Scan context + device info                                          |
| RFID Event Processor → Alert Engine                 | Internal event | Push             | Scan context events (delivered, exception, etc.)                    |
| MQTT Subscriber Service → Package Location Resolver | Internal event | Push             | Mobile device GPS tick                                              |
| MQTT Subscriber Service → Database                  | SQL            | Write            | Device GPS events, device state                                     |
| MQTT Subscriber Service → Geofence Evaluator        | Internal call  | Sync             | Device GPS coordinates                                              |
| Package Location Resolver → Database                | SQL            | Read/Write       | Package location updates                                            |
| Geofence Evaluator → Database                       | SQL (PostGIS)  | Read             | Geofence polygon spatial queries                                    |
| Geofence Evaluator → Alert Engine                   | Internal event | Push             | Breach event data                                                   |
| Alert Engine → Database                             | SQL            | Write            | Alert records                                                       |
| Alert Engine → MQTT Broker                          | MQTT QoS 2     | Publish          | Alert payloads to `logistics/alerts/{id}`                           |
| Alert Engine → WebSocket Server                     | Internal event | Push             | Alert broadcast data                                                |
| MQTT Subscriber Service → WebSocket Server          | Internal event | Push             | Telemetry broadcast data                                            |
| WebSocket Server → Dashboard                        | WebSocket      | Push             | Live device GPS, package positions, alerts, ETAs                    |
| Dashboard → REST API                                | HTTPS REST     | Request/Response | Package CRUD, device config, auth, geofence management              |
| REST API → Database                                 | SQL            | Read/Write       | All entity operations                                               |
| REST API → Device Registry                          | Internal call  | Sync             | Device provisioning, role updates                                   |
| Device Registry → MQTT Broker                       | MQTT QoS 2     | Publish          | Remote commands to device `/cmd` topics                             |

---

## 11. Error Handling & Resilience Design

### 11.1 Device Offline Detection

| Scenario                       | Detection                                                    | Response                                                                                                                            |
| ------------------------------ | ------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| Device misses heartbeat (>90s) | MQTT Subscriber monitors last heartbeat timestamp per device | Device Registry marks device `offline`; operator alert triggered; packages last seen on this device flagged as "location uncertain" |
| Device reconnects              | New heartbeat received                                       | Device Registry marks `online`; alert auto-dismissed; packages resume normal tracking                                               |
| Mobile device GPS signal lost  | `accuracy_m > 50` in telemetry payload                       | Package location marked "low accuracy"; dashboard shows indicator; ETA paused                                                       |

### 11.2 RFID Scan Anomalies

| Scenario                                             | Detection                                                                                  | Response                                                                     |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| Unknown EPC (tag not in DB)                          | Auto ID resolution returns null                                                            | Event quarantined in `unknown_scans` log; operator exception alert triggered |
| Duplicate scan within cooldown                       | Device-side cooldown + backend 60s window                                                  | Silently discarded; counter incremented for monitoring                       |
| Expected package not scanned                         | Time-window expectation (e.g., truck departs hub without scanning all manifested packages) | Alert Engine triggers "missing scan" alert for operator                      |
| Wrong facility scan (package at unexpected location) | Facility ID on scan event ≠ expected next facility in route                                | Exception status set; operator alert triggered                               |

### 11.3 MQTT & Network Failures

| Scenario                              | Recovery Strategy                                                                   |
| ------------------------------------- | ----------------------------------------------------------------------------------- |
| MQTT broker unreachable (device side) | Device buffers last 50 scan events in local memory; publishes all on reconnect      |
| MQTT broker crash                     | Mosquitto persistent session retains QoS 1/2 messages; delivered on broker restart  |
| Backend subscriber crash              | Docker restart policy `always`; QoS 1/2 messages redelivered by broker on reconnect |
| Database write failure                | In-memory dead-letter buffer (max 500 events); retry with exponential backoff       |

---

## 12. Security Design

### 12.1 Authentication Layers

| Layer                | Mechanism                                                                                                                      |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Dashboard login      | Username + bcrypt password → JWT (HS256, 8h expiry)                                                                            |
| REST API             | Bearer JWT on every request; RBAC Middleware validates role and scope                                                          |
| WebSocket            | JWT passed on handshake; scope-filtered subscriptions enforced                                                                 |
| MQTT devices         | Each device has unique MQTT username/password provisioned at setup; broker ACL restricts each device to its own publish topics |
| Public tracking page | No auth; rate-limited (60 req/min per IP); returns only position, timeline, ETA — no PII                                       |

### 12.2 MQTT Access Control List (ACL)

Each MQTT client is restricted by the broker ACL:

| Client             | Publish Allowed                           | Subscribe Allowed                      |
| ------------------ | ----------------------------------------- | -------------------------------------- |
| Fixed Device Sim   | `logistics/fixed/{own_device_id}/#`       | `logistics/fixed/{own_device_id}/cmd`  |
| Mobile Device Sim  | `logistics/mobile/{own_device_id}/#`      | `logistics/mobile/{own_device_id}/cmd` |
| Backend Subscriber | `logistics/alerts/#`, `logistics/+/+/cmd` | `logistics/#`                          |

No device can publish to another device's topics or subscribe to the full broker feed.

### 12.3 RBAC Enforcement

| Action                               | Operator | Warehouse Staff    | End Customer |
| ------------------------------------ | -------- | ------------------ | ------------ |
| View all devices on map              | ✅       | ❌                 | ❌           |
| View all packages                    | ✅       | ❌ (facility only) | ❌           |
| View single package (by tracking ID) | ✅       | ✅                 | ✅ (public)  |
| Provision / update device            | ✅       | ❌                 | ❌           |
| Send remote command to device        | ✅       | ❌                 | ❌           |
| Create package                       | ✅       | ✅                 | ❌           |
| Override package status              | ✅       | ❌                 | ❌           |
| View alerts                          | ✅       | ✅ (facility)      | ✅ (own pkg) |
| Configure geofences                  | ✅       | ❌                 | ❌           |

---

## 13. Glossary

| Term                      | Definition                                                                                                                                             |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| EPC                       | Electronic Product Code — a globally unique identifier encoded into an RFID tag's memory; used here to reference a package tracking ID                 |
| EPC Gen2                  | The dominant passive UHF RFID standard (ISO 18000-6C); used in global supply chain and logistics                                                       |
| Passive RFID              | An RFID tag with no battery; powered entirely by the electromagnetic field emitted by a reader antenna                                                 |
| UHF RFID                  | Ultra-High Frequency RFID operating at 860–960 MHz; provides read ranges of 1–12 metres suitable for logistics gates and cargo bays                    |
| RFID Reader               | A device that emits an RF field to energise passive tags within range, reads their EPC, and returns the data                                           |
| Scan Context              | A metadata field on each scan event indicating the logical meaning of the scan (arrival, pickup, delivered, etc.)                                      |
| Fixed Device              | An IoT device installed permanently at a facility (warehouse gate, hub scanner); has static GPS coordinates                                            |
| Mobile Device             | An IoT device installed aboard a vehicle (truck, van); has dynamic GPS coordinates updated continuously                                                |
| Device Identity File      | A local configuration file on each device containing its provisioned ID, type, role, and facility binding                                              |
| Package Location Resolver | Backend module that determines a package's current location by inheriting from its carrying device (mobile) or stamping the last scan location (fixed) |
| RFID Event Processor      | Backend module that consumes raw RFID scan events, deduplicates them, resolves EPCs to tracking IDs, and triggers status transitions                   |
| Deduplication Window      | A time window (60 seconds) during which repeated scans of the same tag by the same device are suppressed                                               |
| Heartbeat                 | A periodic MQTT message from a device confirming it is online and operational                                                                          |
| Device Registry           | Backend module maintaining the authoritative list of all provisioned devices and their current state                                                   |
| QoS                       | MQTT Quality of Service delivery guarantee: 0 = fire-and-forget, 1 = at-least-once, 2 = exactly-once                                                   |
| Geofence                  | A virtual geographic polygon boundary; triggers alerts when a mobile device enters or exits it                                                         |
| PostGIS                   | PostgreSQL spatial extension enabling geometric queries (e.g., point-in-polygon for geofence checks)                                                   |
| MQTT ACL                  | Access Control List on the MQTT broker restricting which topics each client can publish to or subscribe from                                           |
| Dead-Letter Buffer        | In-memory queue holding failed event writes for retry; prevents data loss during transient DB failures                                                 |

---

_Document Owner: Engineering Team_
_Version: 2.0 — replaces v1.0 (per-package GPS simulator model)_
_Related Document: PRD — GPS-Based Logistic Package Tracking System MVP v1.0_
_Next Review: After device simulator implementation kickoff_
