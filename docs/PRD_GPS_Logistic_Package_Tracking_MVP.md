# Product Requirements Document (PRD)
## GPS-Based Logistic Package Tracking System — MVP
**Version:** 1.0  
**Status:** Draft  
**Date:** April 18, 2026  
**Platform:** Web Dashboard (Browser)  
**Simulation Scale:** 10–50 Packages / Small Fleet  

---

## 1. Executive Summary

This document defines the MVP requirements for a simulated GPS-based Logistic Package Tracking System. The system enables logistics operators, warehouse staff, and end customers to monitor package movement in real time via an interactive map and status timeline. The MVP is browser-based, supports a small fleet of 10–50 simulated packages, and includes an alert/notification system for key logistics events.

The simulation layer replaces physical IoT GPS hardware with a software-driven GPS data emitter, making the system fully demonstrable without physical devices.

---

## 2. Problem Statement

Logistics operations lacking real-time visibility suffer from:
- Delayed response to package exceptions (lost, delayed, misrouted)
- Customer dissatisfaction from lack of shipment transparency
- Operational inefficiency due to manual status updates by warehouse staff

This MVP addresses these gaps by providing a unified tracking dashboard that simulates GPS telemetry, visualizes package movement, and proactively alerts stakeholders.

---

## 3. Goals & Success Metrics

### 3.1 Goals
- Simulate realistic GPS-based package movement for a fleet of 10–50 packages
- Provide real-time map visualization and status timelines to three user roles
- Deliver proactive alerts for geofence breaches, delays, and delivery events
- Establish a clean, extensible architecture for future integration with real IoT hardware

### 3.2 MVP Success Metrics
| Metric | Target |
|---|---|
| Map refresh latency | ≤ 2 seconds |
| Simulated packages supported | 10–50 concurrent |
| Alert delivery time (in-app) | ≤ 5 seconds after trigger |
| Dashboard uptime | ≥ 99% during demo/test sessions |
| Role-based views functional | 3 roles fully operational |

---

## 4. Scope

### In Scope (MVP)
- Simulated GPS telemetry engine (software-based, no physical hardware)
- Interactive real-time map with package markers
- Package status timeline per shipment
- Three role-based views: Operator, Warehouse Staff, End Customer
- Alert & notification system (in-app + optional email stub)
- Basic package CRUD (create, assign, track, deliver)
- Geofence definition per delivery zone

### Out of Scope (MVP)
- Real physical GPS/IoT device integration
- Mobile app (iOS/Android)
- Route optimization engine
- Payment or billing module
- Multi-tenant / multi-company support
- Advanced analytics and reporting

---

## 5. User Personas

### 5.1 Logistics Operator / Dispatcher
**Goal:** Monitor all packages across the fleet in real time; respond to exceptions fast.  
**Pain Points:** Lack of unified view, reactive instead of proactive management.  
**Key Actions:** View live map of all packages, assign packages to drivers, acknowledge alerts, manually update package status.

### 5.2 Warehouse Staff
**Goal:** Scan/register packages at pickup and receiving points; update statuses at checkpoints.  
**Pain Points:** Manual, paper-based status updates; no visibility into en-route packages.  
**Key Actions:** Register new packages, mark packages as picked up or received, view packages assigned to their warehouse.

### 5.3 End Customer (Package Recipient)
**Goal:** Know exactly where their package is and when it will arrive.  
**Pain Points:** Vague "in transit" messages; no proactive updates.  
**Key Actions:** Track a specific package via tracking number, view status timeline, receive delivery alerts.

---

## 6. User Stories

### Logistics Operator
- As an operator, I want to see all active packages on a live map so I can monitor fleet status at a glance.
- As an operator, I want to receive an alert when a package deviates from its expected route or zone so I can take corrective action.
- As an operator, I want to click on any package marker on the map to view its full status history.
- As an operator, I want to create a new shipment and assign it a simulated GPS route.
- As an operator, I want to filter packages by status (in transit, delayed, delivered, exception).

### Warehouse Staff
- As warehouse staff, I want to register a new package into the system with sender, recipient, and destination details.
- As warehouse staff, I want to mark a package as "Picked Up" when a driver collects it from the warehouse.
- As warehouse staff, I want to see only packages relevant to my warehouse location.
- As warehouse staff, I want to receive an alert when a package is approaching my warehouse for receiving.

### End Customer
- As a customer, I want to enter my tracking number and see my package on a map.
- As a customer, I want to see a clear timeline of my package's journey (e.g., Registered → Picked Up → In Transit → Out for Delivery → Delivered).
- As a customer, I want to receive an in-app notification when my package is out for delivery.
- As a customer, I want to see the estimated time of arrival (ETA) on my tracking page.

---

## 7. Functional Requirements

### 7.1 GPS Simulation Engine
| ID | Requirement |
|---|---|
| FR-SIM-01 | The system shall generate simulated GPS coordinates for each active package at configurable intervals (default: every 5 seconds). |
| FR-SIM-02 | Simulated routes shall follow predefined waypoints between origin and destination. |
| FR-SIM-03 | The engine shall support 10–50 concurrent simulated packages without degradation. |
| FR-SIM-04 | The simulation shall support configurable speed profiles (normal, delayed, fast). |
| FR-SIM-05 | The engine shall expose a REST/WebSocket API that the dashboard consumes. |

### 7.2 Real-Time Map
| ID | Requirement |
|---|---|
| FR-MAP-01 | The dashboard shall display all active packages as markers on an interactive map. |
| FR-MAP-02 | Package markers shall update position in near real time (≤ 2s refresh). |
| FR-MAP-03 | Clicking a marker shall open a detail panel with package info and status timeline. |
| FR-MAP-04 | The map shall support zoom, pan, and cluster markers when packages are nearby. |
| FR-MAP-05 | Geofence zones (delivery areas) shall be drawn as polygons on the map. |
| FR-MAP-06 | Packages that breach a geofence boundary shall be visually highlighted (e.g., red marker). |

### 7.3 Status Timeline
| ID | Requirement |
|---|---|
| FR-TL-01 | Each package shall have a status timeline showing all recorded state transitions with timestamps. |
| FR-TL-02 | Standard statuses: Registered, Picked Up, In Transit, At Hub, Out for Delivery, Delivered, Exception. |
| FR-TL-03 | The timeline shall display the active/current status prominently. |
| FR-TL-04 | Timeline entries shall include location name (e.g., "Jakarta Warehouse") and GPS coordinates. |

### 7.4 Alert & Notification System
| ID | Requirement |
|---|---|
| FR-ALT-01 | The system shall trigger alerts for the following events: geofence breach, package delay (exceeds ETA by configurable threshold), package delivered, package exception. |
| FR-ALT-02 | In-app notifications shall appear in a notification center/bell icon within ≤ 5 seconds of the trigger event. |
| FR-ALT-03 | Alerts shall be role-filtered: operators see all alerts; customers see only their package alerts; warehouse staff see facility-relevant alerts. |
| FR-ALT-04 | Unread alerts shall be visually distinguished (badge count, bold text). |
| FR-ALT-05 | The system shall support an email notification stub (logs email content to console/file for MVP; no actual email sending required). |
| FR-ALT-06 | Operators shall be able to acknowledge and dismiss alerts. |

### 7.5 Package Management
| ID | Requirement |
|---|---|
| FR-PKG-01 | Warehouse staff and operators can create a new package with: tracking ID (auto-generated), sender info, recipient info, origin, destination, and package weight/dimensions. |
| FR-PKG-02 | The system shall assign a simulated GPS route upon package creation. |
| FR-PKG-03 | Operators can manually override a package's status. |
| FR-PKG-04 | Packages can be filtered and searched by: tracking ID, status, origin, destination, date range. |
| FR-PKG-05 | End customers access package details via a public tracking page using their tracking number (no login required). |

### 7.6 Role-Based Access Control (RBAC)
| ID | Requirement |
|---|---|
| FR-RBAC-01 | Three roles shall be supported: Operator, Warehouse Staff, End Customer. |
| FR-RBAC-02 | Operators have full access to all packages, the live map, alerts, and management functions. |
| FR-RBAC-03 | Warehouse Staff have access to packages linked to their facility; they cannot view unrelated packages or fleet-wide map. |
| FR-RBAC-04 | End Customers access only their own package via tracking number; no login is required. |
| FR-RBAC-05 | Authentication for Operator and Warehouse Staff roles shall use username/password (JWT-based session). |

---

## 8. Non-Functional Requirements

| ID | Category | Requirement |
|---|---|---|
| NFR-01 | Performance | Map and timeline shall load within 3 seconds on standard broadband. |
| NFR-02 | Scalability | Architecture shall support scaling to 200+ packages without refactoring core components. |
| NFR-03 | Reliability | Simulation engine shall auto-restart on failure; dashboard shall show a "reconnecting" state. |
| NFR-04 | Security | All API endpoints (except public tracking) shall require authentication. HTTPS enforced. |
| NFR-05 | Usability | Dashboard shall be responsive for screens ≥ 1024px wide. |
| NFR-06 | Maintainability | Codebase shall follow modular architecture; simulation layer must be swappable with real IoT data source. |
| NFR-07 | Browser Support | Chrome 110+, Firefox 110+, Edge 110+. |

---

## 9. System Architecture (High-Level)

```
┌─────────────────────────────────────────────────────┐
│                  Web Dashboard (Browser)             │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Live Map │  │Status Timeline│  │ Notifications │  │
│  └────┬─────┘  └──────┬───────┘  └──────┬────────┘  │
│       └───────────────┼──────────────────┘           │
│               WebSocket / REST API                   │
└───────────────────────┼─────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────┐
│                  Backend Server                      │
│  ┌───────────────┐   ┌──────────────────────────┐   │
│  │  REST API     │   │  WebSocket Event Server   │   │
│  │  (Packages,   │   │  (Live GPS updates,       │   │
│  │   Auth, RBAC) │   │   Alerts)                 │   │
│  └──────┬────────┘   └──────────┬───────────────┘   │
│         └──────────────┬────────┘                    │
│              ┌──────────▼──────────┐                 │
│              │   GPS Simulation    │                 │
│              │   Engine            │                 │
│              │  (Waypoint-based    │                 │
│              │   route emitter)    │                 │
│              └──────────┬──────────┘                │
└─────────────────────────┼───────────────────────────┘
                          │
              ┌───────────▼───────────┐
              │      Database         │
              │  (Packages, Routes,   │
              │   Events, Users,      │
              │   Geofences)          │
              └───────────────────────┘
```

### Recommended Tech Stack

| Layer | Technology |
|---|---|
| Frontend | React.js + Leaflet.js (map) or Mapbox GL JS |
| Backend | Node.js + Express or FastAPI (Python) |
| Real-time | WebSocket (Socket.IO) |
| Database | PostgreSQL (with PostGIS for geospatial) or MongoDB |
| Auth | JWT + bcrypt |
| GPS Simulation | Node.js/Python background worker with waypoint interpolation |
| Deployment (MVP) | Docker Compose (local) or single cloud VM |

---

## 10. Data Models (Core Entities)

### Package
```json
{
  "tracking_id": "PKG-20260418-001",
  "status": "in_transit",
  "sender": { "name": "", "address": "" },
  "recipient": { "name": "", "address": "", "email": "" },
  "origin": { "name": "Jakarta Warehouse", "lat": -6.2088, "lng": 106.8456 },
  "destination": { "name": "Surabaya Hub", "lat": -7.2575, "lng": 112.7521 },
  "current_position": { "lat": -6.9, "lng": 107.6, "timestamp": "" },
  "eta": "2026-04-18T18:00:00Z",
  "assigned_route_id": "ROUTE-001",
  "weight_kg": 2.5,
  "created_at": "",
  "updated_at": ""
}
```

### Status Event (Timeline Entry)
```json
{
  "event_id": "EVT-001",
  "tracking_id": "PKG-20260418-001",
  "status": "picked_up",
  "location_name": "Jakarta Warehouse",
  "lat": -6.2088,
  "lng": 106.8456,
  "timestamp": "2026-04-18T08:30:00Z",
  "note": "Package collected by driver"
}
```

### Alert
```json
{
  "alert_id": "ALT-001",
  "type": "geofence_breach",
  "tracking_id": "PKG-20260418-001",
  "message": "Package PKG-001 has exited Jakarta delivery zone.",
  "target_roles": ["operator"],
  "is_read": false,
  "triggered_at": "2026-04-18T10:15:00Z"
}
```

### Geofence
```json
{
  "geofence_id": "GF-001",
  "name": "Jakarta Delivery Zone",
  "polygon_coordinates": [[-6.1, 106.7], [-6.1, 107.0], [-6.4, 107.0], [-6.4, 106.7]],
  "alert_on_enter": false,
  "alert_on_exit": true
}
```

---

## 11. UX / Screen Inventory

| Screen | Accessible By | Description |
|---|---|---|
| Login | Operator, Warehouse Staff | Username/password login |
| Operator Dashboard | Operator | Full live map + package list + alert center |
| Package Detail Modal | Operator, Warehouse Staff | Map focus + full status timeline for one package |
| Package Management | Operator, Warehouse Staff | Table of packages with filters; create/edit actions |
| Warehouse View | Warehouse Staff | Facility-scoped package list; mark picked up / received |
| Alert Center | Operator, Warehouse Staff | List of all alerts with read/unread state; acknowledge action |
| Public Tracking Page | End Customer | Enter tracking number → map pin + status timeline + ETA |
| Simulation Control Panel | Operator | Start/pause/reset simulation; adjust speed profile (MVP debug tool) |

---

## 12. Alert Trigger Matrix

| Event | Operator | Warehouse Staff | End Customer |
|---|---|---|---|
| Package registered | ✅ | ✅ (own facility) | ❌ |
| Package picked up | ✅ | ✅ (own facility) | ✅ |
| Geofence breach (exit) | ✅ | ❌ | ❌ |
| Package delayed (ETA exceeded) | ✅ | ❌ | ✅ |
| Package approaching warehouse | ✅ | ✅ (own facility) | ❌ |
| Out for delivery | ✅ | ❌ | ✅ |
| Delivered | ✅ | ❌ | ✅ |
| Package exception | ✅ | ✅ (own facility) | ✅ |

---

## 13. Assumptions & Constraints

- **Simulation only:** No real GPS hardware is required for the MVP. The simulation engine is the sole source of location data.
- **Single city/region scope:** Simulated routes are scoped to a defined geographic area (e.g., Java island, Indonesia) for realism.
- **No offline mode:** The dashboard requires an active internet/local server connection.
- **Single warehouse per staff account** in MVP (multi-facility support deferred).
- **Email notifications** are stubbed (logged) and not actually sent in the MVP.
- **ETA calculation** is based on simulated speed and remaining waypoints, not real traffic data.

---

## 14. Milestones & Suggested MVP Timeline

| Phase | Deliverable | Estimated Duration |
|---|---|---|
| Phase 1 | Project setup, DB schema, Auth, RBAC | 1 week |
| Phase 2 | GPS Simulation Engine + WebSocket API | 1 week |
| Phase 3 | Live Map Frontend + Package Markers | 1 week |
| Phase 4 | Status Timeline + Package Management UI | 1 week |
| Phase 5 | Alert & Notification System | 1 week |
| Phase 6 | Warehouse Staff View + Customer Tracking Page | 1 week |
| Phase 7 | Integration Testing + Bug Fixes + Demo Prep | 1 week |
| **Total** | **Full MVP** | **~7 weeks** |

---

## 15. Open Questions

1. Should the simulation support **multi-stop routes** (e.g., package passes through a hub before final delivery)?
2. Is a **Simulation Control Panel** (start/pause/reset) needed for all operators, or only for admin/demo purposes?
3. Should geofences be **pre-configured** or allow operators to draw custom zones on the map?
4. What is the **delay threshold** (in minutes past ETA) before a "delayed" alert is triggered?
5. Should the public tracking page require any form of **verification** (e.g., last 4 digits of phone) or be open by tracking number alone?

---

## 16. Glossary

| Term | Definition |
|---|---|
| GPS Simulation Engine | Software module that emits fake GPS coordinates following predefined routes, replacing real IoT hardware |
| Geofence | A virtual geographic boundary; the system triggers alerts when a package enters or exits it |
| Waypoint | A GPS coordinate checkpoint along a simulated delivery route |
| ETA | Estimated Time of Arrival, calculated from current position and remaining route |
| Tracking ID | Unique auto-generated identifier for each package (e.g., PKG-20260418-001) |
| WebSocket | A persistent, bidirectional connection used to push live GPS updates to the browser without polling |
| RBAC | Role-Based Access Control; restricts system features based on the user's assigned role |

---

*Document Owner: Product Team*  
*Next Review: After stakeholder feedback on open questions (Section 15)*
