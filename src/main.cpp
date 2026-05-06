/**
 * =============================================================================
 * IoT GPS-Based Logistic Package Tracking System
 * MOBILE DEVICE FIRMWARE — Truck / Delivery Van
 * =============================================================================
 * Module purpose:
 *   Thin Arduino entrypoint for ESP32 mobile-device firmware.
 *
 * Responsibilities:
 *   Initialize hardware/services in setup(), run scheduler orchestration in loop(),
 *   and delegate feature behavior to focused modules.
 *
 * Behavior-preservation note:
 *   Firmware behavior, MQTT topics, JSON schemas, serial logs, RFID cooldowns, and
 *   Wokwi scenario commands are preserved from the original monolithic main.cpp.
 * =============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <tests.h>
#include "connectivity.h"
#include "firmware_config.h"
#include "firmware_state.h"
#include "leds.h"
#include "logging.h"
#include "rfid.h"
#include "scenario.h"
#include "telemetry.h"

void setup()
{
  Serial.setDebugOutput(false);
  Serial.setTxBufferSize(2048);
  Serial.flush();

  Serial.begin(SERIAL_BAUD);
  delay(500);

#if LOG_HUMAN
  Serial.println(F("\n[MOBILE] GPS Logistic Tracker - Mobile Device Firmware v2.1"));
  Serial.printf("[MOBILE] Device ID: %s | Role: %s\n", device.deviceId, device.role);
  Serial.flush();
#endif
  JsonDocument bootLog;
  addLogBase(bootLog, "INFO", "boot");
  bootLog["firmware"] = "mobile-logistic-device";
  bootLog["firmware_version"] = "2.1";
  bootLog["schema_version"] = SCHEMA_VERSION;
  bootLog["role"] = device.role;
  bootLog["facility_id"] = device.facilityId;
  bootLog["location_name"] = device.locationName;
  bootLog["log_jsonl"] = LOG_JSONL;
  bootLog["log_human"] = LOG_HUMAN;
  emitLog(bootLog);

  // LED pins
  pinMode(PIN_LED_WIFI, OUTPUT);
  pinMode(PIN_LED_MQTT, OUTPUT);
  pinMode(PIN_LED_RFID, OUTPUT);
  pinMode(PIN_LED_GPS, OUTPUT);
  digitalWrite(PIN_LED_WIFI, LOW);
  digitalWrite(PIN_LED_MQTT, LOW);
  digitalWrite(PIN_LED_RFID, LOW);
  digitalWrite(PIN_LED_GPS, LOW);
  logSetupStep("leds_ready");

  // Build MQTT topic strings
  setupTopics();
  logSetupStep("topics_ready");

  // GPS Serial
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#if LOG_HUMAN
  Serial.println(F("[GPS] UART2 initialised at 9600 baud"));
  Serial.flush();
#endif
  JsonDocument gpsLog;
  addLogBase(gpsLog, "INFO", "setup_step");
  gpsLog["step"] = "gps_uart_ready";
  gpsLog["baud"] = GPS_BAUD;
  gpsLog["rx_pin"] = GPS_RX_PIN;
  gpsLog["tx_pin"] = GPS_TX_PIN;
  emitLog(gpsLog);

  // I2C + PN532
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(1000);
  rfid.begin();
  delay(1000);

  // Run tests
  // runAllTests();

  uint32_t versiondata = rfid.getFirmwareVersion();
  // Serial.println(versiondata);
  if (!versiondata)
  {
#if LOG_HUMAN
    Serial.println(F("[RFID] ERROR: PN532 not found — check wiring!"));
#endif
    JsonDocument rfidLog;
    addLogBase(rfidLog, "ERROR", "setup_step");
    rfidLog["step"] = "rfid_pn532";
    rfidLog["status"] = "not_found";
    emitLog(rfidLog);
    // Non-fatal: continue without RFID
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[RFID] PN532 found. Chip: 0x%X | Firmware: %d.%d\n",
                  (versiondata >> 24) & 0xFF,
                  (versiondata >> 16) & 0xFF,
                  (versiondata >> 8) & 0xFF);
#endif
    rfid.SAMConfig();
#if LOG_HUMAN
    Serial.println(F("[RFID] SAM configured"));
#endif
    JsonDocument rfidLog;
    addLogBase(rfidLog, "INFO", "setup_step");
    rfidLog["step"] = "rfid_pn532";
    rfidLog["status"] = "ready";
    rfidLog["chip"] = (versiondata >> 24) & 0xFF;
    rfidLog["firmware_major"] = (versiondata >> 16) & 0xFF;
    rfidLog["firmware_minor"] = (versiondata >> 8) & 0xFF;
    emitLog(rfidLog);
  }

  Serial.flush();

  // WiFi
  connectWifi();

  // MQTT
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
  mqttClient.setBufferSize(1024);
  connectMqtt();

  logSetupStep("setup_complete");
  logStateSnapshot("setup_complete");
#if LOG_HUMAN
  Serial.println(F("[MOBILE] Setup complete. Entering main loop."));
  Serial.flush();
#endif
  delay(200);
}

// =============================================================================
// LOOP
// =============================================================================
void loop()
{
  unsigned long now = millis();

  // --- Wokwi scenario command parser (serial-only test input) ---
  handleSerialScenarioCommands();

  // --- Feed GPS parser ---
  while (gpsSerial.available())
  {
    gps.encode(gpsSerial.read());
  }
  device.gpsFix = gps.location.isValid() && gps.location.age() < 2000;
  device.uptimeSec = now / 1000;

  // --- WiFi reconnect ---
  if (!WiFi.isConnected())
  {
    device.wifiConnected = false;
    device.mqttConnected = false;
    if (now - lastWifiAttemptMs > WIFI_RECONNECT_MS)
    {
      lastWifiAttemptMs = now;
      connectWifi();
    }
  }
  else
  {
    device.wifiConnected = true;
  }

  // --- MQTT loop + reconnect ---
  if (device.wifiConnected)
  {
    if (!mqttClient.connected())
    {
      device.mqttConnected = false;
      if (now - lastMqttAttemptMs > MQTT_RECONNECT_MS)
      {
        lastMqttAttemptMs = now;
        connectMqtt();
      }
    }
    else
    {
      device.mqttConnected = true;
      mqttClient.loop();
      flushEventBuffer();
    }
  }

  // --- GPS telemetry publish ---
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)
  {
    lastTelemetryMs = now;
    publishTelemetry();
  }

  // --- Heartbeat publish ---
  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS)
  {
    lastHeartbeatMs = now;
    publishHeartbeat();
  }

  // --- RFID passive scan (continuous, low freq) ---
  if (now - lastRfidScanMs >= RFID_SCAN_INTERVAL_MS)
  {
    lastRfidScanMs = now;
    handleRfidScan();
  }

  // --- Clean expired cooldown entries periodically ---
  cleanExpiredCooldowns();

  updateLeds();
}
