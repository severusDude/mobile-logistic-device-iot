/**
 * =============================================================================
 * LED Status Module
 * =============================================================================
 * Purpose:
 *   Declares status LED update behavior for WiFi, MQTT, RFID, and GPS.
 *
 * Responsibilities:
 *   Drive existing GPIO indicators with unchanged status semantics.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include "firmware_state.h"

void updateLeds();
