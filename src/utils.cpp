/**
 * =============================================================================
 * Firmware Utility Helpers
 * =============================================================================
 * Purpose:
 *   Provides shared event ID and timestamp helpers.
 *
 * Responsibilities:
 *   Keep scan event sequence format and GPS/fallback timestamp behavior unchanged.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "utils.h"

String buildScanEventId()
{
  char buf[64];
  snprintf(buf, sizeof(buf), "SCAN-%s-%06u", DEVICE_ID, ++scanSeq);
  return String(buf);
}

// =============================================================================
// UTILITY: ISO 8601 timestamp
// Uses GPS time if available, otherwise millis-based placeholder
// =============================================================================
String buildTimestamp()
{
  char buf[32];
  if (gps.date.isValid() && gps.time.isValid())
  {
    snprintf(buf, sizeof(buf),
             "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
  }
  else
  {
    // Fallback: use millis as a relative offset from epoch placeholder
    unsigned long s = millis() / 1000;
    snprintf(buf, sizeof(buf),
             "2026-04-18T%02lu:%02lu:%02lu.000Z",
             (s / 3600) % 24, (s / 60) % 60, s % 60);
  }
  return String(buf);
}
