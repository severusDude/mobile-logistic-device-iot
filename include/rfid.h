/**
 * =============================================================================
 * RFID And Package State Module
 * =============================================================================
 * Purpose:
 *   Declares PN532 scan handling, cooldown, EPC mapping, and package counters.
 *
 * Responsibilities:
 *   Read tags, suppress duplicates, map simulated UIDs, and maintain onboard package counts.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include <string>
#include "firmware_state.h"

void handleRfidScan();
bool isTagInCooldown(const std::string &epc);
void markTagCooldown(const std::string &epc);
void cleanExpiredCooldowns();
const char *resolvePackageEpc(const char *uidHex);
const char *resolveScanContext();
void updatePackageCounters(const char *uidHex, const char *scanContext);
int findSimulatedPackage(const char *uidHex);
void resetScenarioState();
