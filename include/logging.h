/**
 * =============================================================================
 * Serial Logging Module
 * =============================================================================
 * Purpose:
 *   Declares machine-readable JSONL and optional human serial logging helpers.
 *
 * Responsibilities:
 *   Add common log fields, emit JSONL, and publish setup/state/simple events.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include <ArduinoJson.h>
#include "firmware_state.h"

void addLogBase(JsonDocument &doc, const char *level, const char *event);
void emitLog(JsonDocument &doc);
void logSimpleEvent(const char *level, const char *event, const char *message = nullptr);
void logSetupStep(const char *step, const char *status = "ok", const char *level = "INFO");
void logStateSnapshot(const char *reason);
