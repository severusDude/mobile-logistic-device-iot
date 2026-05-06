/**
 * =============================================================================
 * Firmware Utility Module
 * =============================================================================
 * Purpose:
 *   Declares shared ID and timestamp builders.
 *
 * Responsibilities:
 *   Build scan event IDs and GPS/millis fallback timestamps.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include "firmware_state.h"

String buildScanEventId();
String buildTimestamp();
