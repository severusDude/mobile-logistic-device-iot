# Product Requirements Document (PRD)
## IoT Logistic Controls — Research Prototype Scope

**Version:** 2.0  
**Status:** Research Scope Baseline  
**Date:** May 7, 2026  
**Platform:** Web dashboard + MQTT backend + simulated ESP32 IoT device  
**Research Scale:** 1 mobile device, 1 facility, 2-3 RFID package tags  

---

## 1. Purpose

This document sets strict project boundaries for a research assignment. The goal is not to build a full commercial logistics platform. The goal is to demonstrate and evaluate a core Internet of Things workflow for package tracking:

```text
RFID package scan + GPS telemetry
  -> ESP32/Wokwi simulated device
  -> MQTT broker
  -> backend ingestion and database persistence
  -> simple web dashboard/progress view
```

The research focuses on whether RFID scan events and GPS telemetry can be captured, transmitted, stored, and visualized reliably enough to support a prototype logistics tracking scenario.

---

## 2. Research Problem

Logistics package tracking often depends on manual status updates and limited shipment visibility. For research scope, the problem is narrowed to:

- How to identify a package using RFID.
- How to associate the package with a device location using GPS.
- How to transmit IoT data from device to backend using MQTT.
- How to store and display package/device tracking events in a dashboard.

This research does not attempt to solve full delivery operations, routing, customer notification, fleet optimization, or enterprise user management.

---

## 3. Research Objectives

1. Design a basic IoT package tracking architecture using ESP32, RFID, GPS, MQTT, backend storage, and web dashboard.
2. Implement a simulated mobile IoT device that can publish telemetry, heartbeat, and RFID scan events.
3. Implement backend ingestion that validates MQTT payloads and persists device/package events.
4. Display latest device state, raw event logs, and package timeline information on a simple dashboard/API.
5. Define internal package CRUD for researcher/operator package setup, RFID mapping, assignment, and status override on a small dataset.
6. Evaluate prototype behavior using defined test scenarios and progress evidence.

---

## 4. Core Scope

### 4.1 In Scope

| Area | Boundary |
|---|---|
| IoT device | ESP32/Wokwi-based simulated mobile device with GPS and RFID scan behavior. |
| RFID packages | 2-3 deterministic simulated RFID tags/packages. |
| GPS telemetry | Mobile device publishes GPS payload at configured interval. |
| MQTT | Local Mosquitto broker for telemetry, scan, heartbeat, and command topics. |
| Backend | MQTT subscriber, schema validation, raw event storage, device state update, package event update. |
| Database | PostgreSQL/Prisma schema for facility, device, package, telemetry, heartbeat, raw event, package event, unknown scan, and command records. |
| Dashboard | Simple operator/research dashboard showing device list, event feed, and command controls. |
| Package tracking | Package location is inferred from last scanned mobile device GPS. |
| Package management | Internal operator/research package CRUD for a small dataset: list, create, read/detail, update metadata/status/assignment, and delete/archive. |
| Testing | Scenario-based testing through Wokwi logs, MQTT messages, API checks, and dashboard observation. |
| Documentation | Research progress documentation and implementation evidence. |

### 4.2 Out of Scope

| Removed complexity | Reason |
|---|---|
| Multi-role RBAC for operator/warehouse/customer | Too broad for research prototype; one researcher/operator view is enough. |
| Public customer tracking page | Product feature, not needed to prove IoT data flow. |
| Interactive live map | Optional future enhancement; current research can use coordinates, logs, and timeline. |
| Geofence polygons and breach detection | Adds spatial complexity beyond core IoT pipeline. |
| Alert/notification center | Product workflow beyond research core. |
| Email notification stub | Not needed for IoT data-flow validation. |
| Route optimization | Explicitly outside research scope. |
| ETA calculation | Requires route/speed model; not core evidence. |
| 10-50 concurrent packages | Research scale is 2-3 tags and one mobile device. |
| 200+ package scalability | Not relevant for course prototype. |
| Multi-tenant/company support | Enterprise feature, out of research scope. |
| Production HTTPS and broker ACL | Security should be discussed as limitation/future work, not implemented as research requirement. |
| Real physical deployment | Wokwi/simulation is acceptable for research evidence. |

---

## 5. Actors

| Actor | Description | Current scope |
|---|---|---|
| Researcher / Operator | Person running firmware simulation, backend worker, broker, dashboard, and test scenarios. | In scope |
| IoT Device | Simulated mobile ESP32 device with RFID reader and GPS. | In scope |
| Package Tag | Passive RFID identity represented by deterministic simulated tag UID/EPC. | In scope |
| Warehouse Staff | Real operational user persona. | Out of scope |
| End Customer | Public tracking user. | Out of scope |
| Admin / Multi-role user manager | Product/enterprise role. | Out of scope |

---

## 6. User Stories

### In-Scope Research Stories

- As a researcher, I want the simulated device to publish GPS telemetry so the backend can store latest device position.
- As a researcher, I want the simulated RFID scan to identify a package EPC so the backend can update package status.
- As a researcher, I want the backend to store raw MQTT events so every test run has inspectable evidence.
- As a researcher, I want a package timeline API so I can verify status changes from scan events.
- As a researcher/operator, I want to manage package records internally so package CRUD, RFID mapping, assignment, and status override can support repeatable prototype scenarios.
- As a researcher, I want a simple dashboard showing devices and event logs so prototype behavior can be observed during tests.
- As a researcher, I want unknown RFID tags to be recorded separately so failure cases can be analyzed.

### Deferred Product Stories

- Customer public tracking.
- Warehouse staff scoped dashboard.
- Operator alert acknowledgement.
- Interactive package map.
- Geofence breach handling.

---

## 7. Functional Requirements

### 7.1 IoT Device Simulation

| ID | Requirement | Priority |
|---|---|---|
| FR-IOT-01 | System shall run a simulated ESP32 mobile device in Wokwi or compatible environment. | Must |
| FR-IOT-02 | Device shall simulate GPS telemetry with latitude, longitude, timestamp, and GPS fix state. | Must |
| FR-IOT-03 | Device shall simulate RFID package scans using deterministic package identifiers. | Must |
| FR-IOT-04 | Device shall publish telemetry, scan, and heartbeat payloads to MQTT topics. | Must |
| FR-IOT-05 | Device may support remote commands such as force scan, cooldown update, role update, and reboot for demonstration. | Should |

### 7.2 MQTT Communication

| ID | Requirement | Priority |
|---|---|---|
| FR-MQTT-01 | System shall use a local MQTT broker for prototype communication. | Must |
| FR-MQTT-02 | Backend worker shall subscribe to mobile telemetry, scan, and heartbeat topics. | Must |
| FR-MQTT-03 | Backend shall persist supported MQTT payloads as raw events. | Must |
| FR-MQTT-04 | Backend shall reject or log invalid/unknown payloads without crashing the worker. | Must |
| FR-MQTT-05 | Backend may publish remote device commands to mobile device command topics. | Should |

### 7.3 Backend Data Processing

| ID | Requirement | Priority |
|---|---|---|
| FR-BE-01 | Backend shall validate incoming payloads with explicit schemas. | Must |
| FR-BE-02 | Backend shall update device state from telemetry and heartbeat messages. | Must |
| FR-BE-03 | Backend shall resolve RFID EPC to a known package record. | Must |
| FR-BE-04 | Backend shall create package event records for valid scan events. | Must |
| FR-BE-05 | Backend shall update package current status and latest known location from scan/telemetry flow. | Must |
| FR-BE-06 | Backend shall store unknown scans for analysis when RFID EPC is not registered. | Must |
| FR-BE-07 | Backend shall mark stale devices offline after heartbeat timeout. | Should |

### 7.4 Package Management

| ID | Requirement | Priority |
|---|---|---|
| FR-PKG-01 | System shall support internal package list and detail lookup for researcher/operator use. | Should |
| FR-PKG-02 | System shall support internal package creation with tracking ID, RFID EPC, sender, recipient, status, and facility/device assignment fields. | Should |
| FR-PKG-03 | System shall support internal package metadata, status, RFID EPC, facility, and device assignment updates. | Should |
| FR-PKG-04 | System shall support package delete or archive behavior for small research datasets. | Could |
| FR-PKG-05 | Package CRUD shall remain local/research-facing and shall not imply customer portal, RBAC, route optimization, ETA, or production shipment workflow. | Must |

### 7.5 Dashboard and API

| ID | Requirement | Priority |
|---|---|---|
| FR-UI-01 | Dashboard shall show current device list and status. | Must |
| FR-UI-02 | Dashboard shall show recent MQTT/raw event feed. | Must |
| FR-UI-03 | Dashboard shall refresh from backend data without manual page reload. | Should |
| FR-UI-04 | API shall expose package timeline by tracking ID for verification. | Must |
| FR-UI-05 | Dashboard may include device command controls for demonstration. | Should |
| FR-UI-06 | Dashboard may include internal package management screens for package list/search, create/edit, detail, assignment, and status override. | Should |

### 7.6 Testing and Evidence

| ID | Requirement | Priority |
|---|---|---|
| FR-TEST-01 | Project shall include repeatable scenarios for pickup, duplicate scan handling, and delivery/status flow where available. | Must |
| FR-TEST-02 | Test evidence shall include firmware logs, backend persisted raw events, and API/dashboard verification. | Must |
| FR-TEST-03 | Research documentation shall track implemented, pending, and out-of-scope items. | Must |

---

## 8. Non-Functional Requirements

| ID | Category | Requirement | Boundary |
|---|---|---|---|
| NFR-01 | Simplicity | Prototype should prioritize clear data flow over feature breadth. | Must |
| NFR-02 | Observability | Raw events and logs must be inspectable for research evidence. | Must |
| NFR-03 | Maintainability | Firmware, backend schemas, processors, and UI components should remain modular. | Must |
| NFR-04 | Reliability | Worker should handle invalid payloads and MQTT reconnect attempts without stopping normal tests. | Should |
| NFR-05 | Performance | Dashboard/realtime feed should update within about 5 seconds during local tests. | Should |
| NFR-06 | Security | Security limitations must be documented; production-grade auth/ACL is out of implementation scope. | Document only |
| NFR-07 | Scalability | Scale testing beyond prototype is out of scope. | Deferred |

---

## 9. Simplified Architecture

```text
Wokwi ESP32 Mobile Device
  - GPS simulator
  - RFID scan simulator
  - telemetry/scan/heartbeat publisher
        |
        v
Local Mosquitto Broker
        |
        v
Backend Worker
  - topic parser
  - Zod payload validation
  - raw event persistence
  - device/package processors
        |
        v
PostgreSQL + Prisma
        |
        v
Next.js API + Dashboard
  - health/device/raw-event/timeline endpoints
  - internal package CRUD endpoints
  - SSE snapshot feed
  - simple SimCon dashboard
```

---

## 10. Core Data Entities

| Entity | Purpose |
|---|---|
| Facility | Research facility/location label. |
| Device | Mobile IoT device identity, status, heartbeat, telemetry state. |
| Package | Package tracking ID and RFID EPC mapping. |
| PackageEvent | Timeline event created from scan payloads. |
| DeviceTelemetry | GPS telemetry history. |
| DeviceHeartbeat | Heartbeat history and device liveness evidence. |
| RawMqttEvent | Audit log of inbound/outbound MQTT payloads. |
| UnknownScan | Evidence for unregistered RFID tag scans. |
| DeviceCommand | Optional command publish audit record. |

Deferred entities:

- Geofence.
- Route/waypoint.
- Alert subscription/read state.
- Customer account/session.
- Multi-company tenant.

---

## 11. Research Test Scenarios

| Scenario | Expected evidence |
|---|---|
| Device heartbeat | Device becomes/ stays online; heartbeat stored in DB; event appears in raw logs. |
| GPS telemetry | Device last GPS fields update; telemetry stored; terminal feed shows telemetry. |
| Known RFID pickup | Known package EPC creates package event; package status/location updates. |
| Unknown RFID scan | Unknown scan record created; no package update occurs. |
| Duplicate scan handling | Duplicate event ID or firmware cooldown prevents duplicate package event. |
| Internal package CRUD | Researcher/operator can create or update package records used by scan scenarios when package CRUD is implemented. |
| Device offline timeout | Device status changes to offline after stale heartbeat threshold, if worker detector runs. |
| Remote force scan command | Command record created and MQTT command published, if broker/device command path active. |

---

## 12. Success Criteria

The research prototype is successful when:

1. Simulated ESP32/Wokwi device can emit telemetry, heartbeat, and RFID scan data.
2. Backend worker can receive MQTT messages and persist raw events.
3. Known RFID EPC can be resolved to package record.
4. Package timeline can show scan-derived status events.
5. Internal package CRUD scope is either implemented with local API/UI evidence or clearly tracked as an in-scope pending work item.
6. Dashboard/API can show device state and event feed during local test.
7. Research documentation clearly separates implemented, pending, and out-of-scope work.

---

## 13. Explicit Non-Goals

The following must not be treated as required for this research prototype:

- Customer-facing tracking portal.
- Full operator/warehouse/customer RBAC.
- Production authentication and HTTPS enforcement.
- Geofence alerting.
- Delivery ETA.
- Route optimization.
- Interactive map clustering.
- Email/push notification.
- Large fleet simulation.
- Multi-tenant deployment.
- Real hardware procurement.

---

## 14. Milestones

| Phase | Deliverable | Status tracking |
|---|---|---|
| Phase 1 | Firmware simulation and scenario evidence | Track in research documentation |
| Phase 2 | MQTT broker + backend worker ingestion | Track in research documentation |
| Phase 3 | Database schema + seed package/device data | Track in research documentation |
| Phase 4 | Device/package API and timeline verification | Track in research documentation |
| Phase 5 | Simple dashboard observation | Track in research documentation |
| Phase 6 | Final testing, limitations, and report writing | Track in research documentation |

---

## 15. Glossary

| Term | Definition |
|---|---|
| RFID EPC | Package identity code read by device. |
| Mobile device | Simulated truck/transport device with GPS and RFID reader. |
| Telemetry | GPS/location payload sent by device. |
| Heartbeat | Device liveness/health payload. |
| Scan event | RFID package read event. |
| Raw event | Stored MQTT payload for audit/evidence. |
| Package timeline | Ordered package status/history events. |
| Research prototype | Limited implementation built to demonstrate and evaluate technical feasibility. |

---

*Document Owner: Researcher / Engineering Student*  
*Next Review: After scenario testing and report chapter updates.*
