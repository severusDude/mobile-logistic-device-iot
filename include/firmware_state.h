/**
 * =============================================================================
 * Firmware Runtime State
 * =============================================================================
 * Purpose:
 *   Declares shared ESP32 runtime objects, counters, buffers, package manifest,
 *   topic strings, and device state used across firmware modules.
 *
 * Responsibilities:
 *   Keep one ownership point in firmware_state.cpp while allowing focused modules
 *   to share the same state as the original monolithic src/main.cpp.
 *
 * Shared state:
 *   WiFi/MQTT/GPS/RFID clients, timers, counters, buffers, and scenario state.
 * =============================================================================
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <string>
#include "firmware_config.h"

struct BufferedEvent
{
  char topic[80];
  char payload[512];
  uint8_t qos;
};

struct SimulatedPackage
{
  const char *uidHex;
  const char *epc;
  bool onboard;
};

struct DeviceState
{
  char deviceId[48] = DEVICE_ID;
  char facilityId[32] = FACILITY_ID;
  char role[32] = DEVICE_ROLE;
  char locationName[80] = LOCATION_NAME;
  int activePackageCount = 0;
  int scansToday = 0;
  long uptimeSec = 0;
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool gpsFix = false;
};

extern char topicTelemetry[80];
extern char topicScan[80];
extern char topicHeartbeat[80];
extern char topicCmd[80];
extern std::vector<BufferedEvent> eventBuffer;
extern std::map<std::string, unsigned long> rfidCooldownMap;
extern SimulatedPackage simulatedPackages[];
extern const size_t simulatedPackageCount;
extern char scenarioScanContext[16];
extern bool scenarioContextArmed;
extern String serialCommandBuffer;
extern DeviceState device;
extern uint32_t telemetrySeq;
extern uint32_t scanSeq;
extern uint32_t cmdSeq;
extern uint32_t logSeq;
extern unsigned long lastTelemetryMs;
extern unsigned long lastHeartbeatMs;
extern unsigned long lastRfidScanMs;
extern unsigned long lastWifiAttemptMs;
extern unsigned long lastMqttAttemptMs;
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern TinyGPSPlus gps;
extern HardwareSerial gpsSerial;
extern Adafruit_PN532 rfid;
