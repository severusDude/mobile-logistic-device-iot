/**
 * =============================================================================
 * Wokwi Scenario Command Module
 * =============================================================================
 * Purpose:
 *   Declares serial-only Wokwi scenario command parsing.
 *
 * Responsibilities:
 *   Arm scan contexts, force scans, reset scenario state, and emit status snapshots.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include "firmware_state.h"

void handleSerialScenarioCommands();
void processScenarioCommand(String command);
