/**
 * =============================================================================
 * Telemetry Publishing Module
 * =============================================================================
 * Purpose:
 *   Declares telemetry, heartbeat, and RFID scan event MQTT publishers.
 *
 * Responsibilities:
 *   Build JSON payloads and send them through MQTT/buffer paths without changing schema.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include "firmware_state.h"

void publishTelemetry();
void publishHeartbeat();
void publishScanEvent(const char *epcStr, const char *scanContext);
