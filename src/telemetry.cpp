/**
 * =============================================================================
 * Telemetry, Heartbeat, And Scan Event Publishing
 * =============================================================================
 * Purpose:
 *   Builds MQTT JSON payloads for device telemetry, heartbeat, and RFID scan events.
 *
 * Responsibilities:
 *   Preserve payload schemas, QoS intent, retained telemetry, and serial evidence logs.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "telemetry.h"
#include "connectivity.h"
#include "logging.h"
#include "utils.h"

void publishTelemetry()
{
  JsonDocument doc;
  doc["schema_version"] = SCHEMA_VERSION;
  doc["device_id"] = device.deviceId;
  doc["device_type"] = "mobile";
  doc["facility_id"] = device.facilityId;
  doc["timestamp_utc"] = buildTimestamp();
  doc["sequence_no"] = ++telemetrySeq;

  JsonObject gpsObj = doc["gps"].to<JsonObject>();
  if (device.gpsFix)
  {
    gpsObj["lat"] = gps.location.lat();
    gpsObj["lng"] = gps.location.lng();
    gpsObj["altitude_m"] = gps.altitude.isValid() ? gps.altitude.meters() : 0.0;
    gpsObj["accuracy_m"] = gps.hdop.isValid() ? gps.hdop.hdop() * 5.0 : 99.9;
    gpsObj["heading_deg"] = gps.course.isValid() ? gps.course.deg() : 0.0;
    gpsObj["speed_kmh"] = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  }
  else
  {
    // No fix — publish last-known or zeros to signal uncertainty
    gpsObj["lat"] = 0.0;
    gpsObj["lng"] = 0.0;
    gpsObj["altitude_m"] = 0.0;
    gpsObj["accuracy_m"] = 999.9; // large value signals "no fix"
    gpsObj["heading_deg"] = 0.0;
    gpsObj["speed_kmh"] = 0.0;
  }

  doc["active_package_count"] = device.activePackageCount;
  doc["battery_pct"] = 85; // simulated constant
  doc["signal_strength"] = WiFi.RSSI() > -70 ? "good" : (WiFi.RSSI() > -85 ? "fair" : "poor");
  doc["gps_fix"] = device.gpsFix;

  char payload[512];
  serializeJson(doc, payload, sizeof(payload));

  bool published = publishWithBuffer(topicTelemetry, payload, 1, true);
  JsonDocument logDoc;
  addLogBase(logDoc, published ? "INFO" : "WARN", "telemetry");
  logDoc["telemetry_seq"] = telemetrySeq;
  logDoc["published"] = published;
  logDoc["topic"] = topicTelemetry;
  logDoc["buffer_size"] = (int)eventBuffer.size();
  logDoc["gps_fix"] = device.gpsFix;
  logDoc["lat"] = device.gpsFix ? gps.location.lat() : 0.0;
  logDoc["lng"] = device.gpsFix ? gps.location.lng() : 0.0;
  logDoc["speed_kmh"] = device.gpsFix && gps.speed.isValid() ? gps.speed.kmph() : 0.0;
  logDoc["active_package_count"] = device.activePackageCount;
  emitLog(logDoc);

  if (!published)
  {
#if LOG_HUMAN
    Serial.println(F("[TEL] Buffered (MQTT unavailable)"));
#endif
  }
  else
  {
#if LOG_HUMAN
    char logBuf[120];
    snprintf(logBuf, sizeof(logBuf),
             "[TEL] #%u lat=%.4f lng=%.4f spd=%.1fkm/h fix=%s",
             telemetrySeq,
             device.gpsFix ? gps.location.lat() : 0.0,
             device.gpsFix ? gps.location.lng() : 0.0,
             device.gpsFix ? gps.speed.kmph() : 0.0,
             device.gpsFix ? "YES" : "NO");
    Serial.println(logBuf);
    Serial.flush();
#endif
  }
}

void publishHeartbeat()
{
  JsonDocument doc;
  doc["schema_version"] = SCHEMA_VERSION;
  doc["device_id"] = device.deviceId;
  doc["device_type"] = "mobile";
  doc["device_role"] = device.role;
  doc["facility_id"] = device.facilityId;
  doc["timestamp_utc"] = buildTimestamp();
  doc["uptime_sec"] = device.uptimeSec;
  doc["status"] = "online";
  doc["rfid_reader_status"] = "healthy";
  doc["gps_fix"] = device.gpsFix;
  doc["packages_scanned_today"] = device.scansToday;
  doc["wifi_rssi"] = WiFi.RSSI();

  char payload[512];
  serializeJson(doc, payload, sizeof(payload));
  bool published = mqttClient.publish(topicHeartbeat, payload, false);

#if LOG_HUMAN
  Serial.printf("[HB] Heartbeat published. Uptime: %lds\n", device.uptimeSec);
  Serial.flush();
#endif
  JsonDocument logDoc;
  addLogBase(logDoc, published ? "INFO" : "WARN", "heartbeat");
  logDoc["published"] = published;
  logDoc["topic"] = topicHeartbeat;
  logDoc["uptime_sec"] = device.uptimeSec;
  logDoc["wifi_connected"] = device.wifiConnected;
  logDoc["mqtt_connected"] = device.mqttConnected;
  logDoc["gps_fix"] = device.gpsFix;
  logDoc["packages_scanned_today"] = device.scansToday;
  logDoc["rssi"] = WiFi.RSSI();
  emitLog(logDoc);
}

void publishScanEvent(const char *epcStr, const char *scanContext)
{
  JsonDocument doc;
  doc["schema_version"] = SCHEMA_VERSION;
  String eventId = buildScanEventId();
  doc["event_id"] = eventId;
  doc["device_id"] = device.deviceId;
  doc["device_type"] = "mobile";
  doc["device_role"] = device.role;
  doc["facility_id"] = device.facilityId;
  doc["location_name"] = device.locationName;
  doc["timestamp_utc"] = buildTimestamp();
  doc["rfid_epc"] = epcStr;
  doc["scan_context"] = scanContext;
  doc["signal_strength_dbm"] = -62; // simulated
  doc["read_count"] = 1;

  JsonObject gpsObj = doc["device_gps"].to<JsonObject>();
  if (device.gpsFix)
  {
    gpsObj["lat"] = gps.location.lat();
    gpsObj["lng"] = gps.location.lng();
  }
  else
  {
    gpsObj["lat"] = 0.0;
    gpsObj["lng"] = 0.0;
  }

  char payload[512];
  serializeJson(doc, payload, sizeof(payload));

  bool published = publishWithBuffer(topicScan, payload, 2, false);
  JsonDocument logDoc;
  addLogBase(logDoc, published ? "INFO" : "WARN", "scan_publish");
  logDoc["event_id"] = eventId;
  logDoc["epc"] = epcStr;
  logDoc["scan_context"] = scanContext;
  logDoc["published"] = published;
  logDoc["topic"] = topicScan;
  logDoc["buffer_size"] = (int)eventBuffer.size();
  emitLog(logDoc);

  if (!published)
  {
#if LOG_HUMAN
    Serial.println(F("[SCAN] Buffered (MQTT unavailable)"));
#endif
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[SCAN] Published: epc=%s ctx=%s\n", epcStr, scanContext);
#endif
  }
}
