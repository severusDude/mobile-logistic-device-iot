/**
 * =============================================================================
 * Machine-Readable Serial Logging
 * =============================================================================
 * Purpose:
 *   Emits canonical log.v1 JSONL records plus optional human-readable serial lines.
 *
 * Responsibilities:
 *   Build shared log fields and expose setup, simple event, and state snapshot helpers.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "logging.h"
#include "utils.h"

void addLogBase(JsonDocument &doc, const char *level, const char *event)
{
  doc["log_type"] = "iot_device";
  doc["schema"] = "log.v1";
  doc["ts"] = buildTimestamp();
  doc["uptime_ms"] = millis();
  doc["level"] = level;
  doc["event"] = event;
  doc["device_id"] = device.deviceId;
  doc["seq"] = ++logSeq;
}

void emitLog(JsonDocument &doc)
{
#if LOG_JSONL
  serializeJson(doc, Serial);
  Serial.println();
  Serial.flush();
#else
  (void)doc;
#endif
}

void logSimpleEvent(const char *level, const char *event, const char *message)
{
  JsonDocument doc;
  addLogBase(doc, level, event);
  if (message)
  {
    doc["message"] = message;
  }
  emitLog(doc);
}

void logSetupStep(const char *step, const char *status, const char *level)
{
  JsonDocument doc;
  addLogBase(doc, level, "setup_step");
  doc["step"] = step;
  doc["status"] = status;
  emitLog(doc);
}

void logStateSnapshot(const char *reason)
{
  JsonDocument doc;
  addLogBase(doc, "INFO", "state_snapshot");
  doc["reason"] = reason;
  doc["wifi_connected"] = device.wifiConnected;
  doc["mqtt_connected"] = device.mqttConnected;
  doc["gps_fix"] = device.gpsFix;
  doc["rfid_cooldown_size"] = (int)rfidCooldownMap.size();
  doc["active_package_count"] = device.activePackageCount;
  doc["scans_today"] = device.scansToday;
  doc["buffer_size"] = (int)eventBuffer.size();
  doc["telemetry_seq"] = telemetrySeq;
  doc["scan_seq"] = scanSeq;
  doc["uptime_sec"] = device.uptimeSec;
  doc["rssi"] = WiFi.RSSI();
  emitLog(doc);
}
