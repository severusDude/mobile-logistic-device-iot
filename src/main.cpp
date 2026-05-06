/**
 * =============================================================================
 * IoT GPS-Based Logistic Package Tracking System
 * MOBILE DEVICE FIRMWARE — Truck / Delivery Van
 * =============================================================================
 * Hardware:
 *   - ESP32 DevKit V1
 *   - NEO-6M GPS Module  (UART2: RX=3, TX=1)
 *   - PN532 RFID Reader  (I2C:   SDA=IO21, SCL=IO22)
 *   - LED: Blue  IO2  — WiFi status
 *   - LED: Green IO4  — MQTT status
 *   - LED: Yellow IO5 — RFID scan activity
 *   - LED: Red   IO18 — GPS fix / error
 *
 * Firmware responsibilities:
 *   1. Connect to WiFi and MQTT broker
 *   2. Load device identity from build flags
 *   3. Emit GPS telemetry every 5 seconds (QoS 1)
 *   4. Scan RFID on load/unload events + continuous low-freq scan in transit
 *   5. Publish heartbeat every 60 seconds (QoS 0)
 *   6. Subscribe to /cmd topic and handle remote commands
 *   7. Duplicate RFID scan suppression (30s per-tag cooldown)
 *   8. Buffer up to 50 events when MQTT unavailable; flush on reconnect
 *
 * MQTT Topics:
 *   Publish:   logistics/mobile/{device_id}/telemetry   QoS 1
 *              logistics/mobile/{device_id}/scan         QoS 2
 *              logistics/mobile/{device_id}/heartbeat    QoS 0
 *   Subscribe: logistics/mobile/{device_id}/cmd         QoS 2
 *
 * Tech stack: Arduino (PlatformIO), PubSubClient, TinyGPSPlus, PN532, ArduinoJson
 * =============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include <tests.h>
#include <map>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Build-flag injected identity (set in platformio.ini build_flags)
// ---------------------------------------------------------------------------
#ifndef DEVICE_ID
#define DEVICE_ID "DEV-TRUCK-007"
#endif
#ifndef FACILITY_ID
#define FACILITY_ID "jkt-wh-01"
#endif
#ifndef DEVICE_ROLE
#define DEVICE_ROLE "truck"
#endif
#ifndef LOCATION_NAME
#define LOCATION_NAME "Jakarta Warehouse Truck 007"
#endif
#ifndef SCHEMA_VERSION
#define SCHEMA_VERSION "2.1"
#endif

// ---------------------------------------------------------------------------
// WiFi & MQTT credentials
// ---------------------------------------------------------------------------
#ifdef WIFI_PASS
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD WIFI_PASS
#endif
#endif
#ifdef MQTT_BROKER
#ifndef MQTT_HOST
#define MQTT_HOST MQTT_BROKER
#endif
#endif
#ifdef MQTT_USER
#ifndef MQTT_USERNAME
#define MQTT_USERNAME MQTT_USER
#endif
#endif
#ifdef MQTT_PASS
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD MQTT_PASS
#endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef MQTT_HOST
#define MQTT_HOST "broker.hivemq.com"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME DEVICE_ID
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "device-secret"
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID DEVICE_ID
#endif

// ---------------------------------------------------------------------------
// MQTT topic strings (populated at runtime from DEVICE_ID)
// ---------------------------------------------------------------------------
char topicTelemetry[80];
char topicScan[80];
char topicHeartbeat[80];
char topicCmd[80];

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------
#define PIN_LED_WIFI 2
#define PIN_LED_MQTT 4
#define PIN_LED_RFID 5
#define PIN_LED_GPS 18
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif
#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif
#ifndef GPS_RX_PIN
#define GPS_RX_PIN 3
#endif
#ifndef GPS_TX_PIN
#define GPS_TX_PIN 1
#endif
#ifdef RFID_SDA_PIN
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN RFID_SDA_PIN
#endif
#endif
#ifdef RFID_SCL_PIN
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN RFID_SCL_PIN
#endif
#endif
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif
#ifndef PN532_IRQ_PIN
#define PN532_IRQ_PIN 19
#endif
#ifndef PN532_RESET_PIN
#define PN532_RESET_PIN 23
#endif

// ---------------------------------------------------------------------------
// Timing constants
// ---------------------------------------------------------------------------
#ifndef TELEMETRY_INTERVAL_MS
#define TELEMETRY_INTERVAL_MS 5000UL // GPS publish every 5s
#endif
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 60000UL // Heartbeat every 60s
#endif
#ifndef RFID_SCAN_INTERVAL_MS
#define RFID_SCAN_INTERVAL_MS 3000UL // Passive scan every 3s while moving
#endif
#ifndef RFID_COOLDOWN_MS
#define RFID_COOLDOWN_MS 30000UL // Per-tag dedup cooldown
#endif
#ifndef WIFI_RECONNECT_MS
#define WIFI_RECONNECT_MS 5000UL
#endif
#ifndef MQTT_RECONNECT_MS
#define MQTT_RECONNECT_MS 3000UL
#endif
#ifndef MQTT_KEEPALIVE_SEC
#define MQTT_KEEPALIVE_SEC 60
#endif
#ifndef LOG_JSONL
#define LOG_JSONL 1
#endif
#ifndef LOG_HUMAN
#define LOG_HUMAN 1
#endif

// ---------------------------------------------------------------------------
// Event buffer for offline resilience (max 50 events)
// ---------------------------------------------------------------------------
#define EVENT_BUFFER_MAX 50
struct BufferedEvent
{
  char topic[80];
  char payload[512];
  uint8_t qos;
};
std::vector<BufferedEvent> eventBuffer;

// ---------------------------------------------------------------------------
// RFID per-tag cooldown table  { epc_string -> last_scan_ms }
// ---------------------------------------------------------------------------
std::map<std::string, unsigned long> rfidCooldownMap;

// ---------------------------------------------------------------------------
// Simulated package manifest for Wokwi PN532 card scenarios
// ---------------------------------------------------------------------------
struct SimulatedPackage
{
  const char *uidHex;
  const char *epc;
  bool onboard;
};

SimulatedPackage simulatedPackages[] = {
    {"DEADBEEF", "LOG-PKG-20260506-JKTWH-00001", false},
    {"CAFEBABE", "LOG-PKG-20260506-JKTWH-00002", false},
};

char scenarioScanContext[16] = "";
bool scenarioContextArmed = false;
String serialCommandBuffer;

// ---------------------------------------------------------------------------
// Device runtime state
// ---------------------------------------------------------------------------
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
DeviceState device;

// ---------------------------------------------------------------------------
// Sequence counters
// ---------------------------------------------------------------------------
uint32_t telemetrySeq = 0;
uint32_t scanSeq = 0;
uint32_t cmdSeq = 0;
uint32_t logSeq = 0;

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
unsigned long lastTelemetryMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastRfidScanMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
Adafruit_PN532 rfid(PN532_IRQ_PIN, PN532_RESET_PIN);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void setupTopics();
void connectWifi();
void connectMqtt();
void publishTelemetry();
void publishHeartbeat();
void publishScanEvent(const char *epcStr, const char *scanContext);
void handleRfidScan();
void handleSerialScenarioCommands();
void processScenarioCommand(String command);
void flushEventBuffer();
void bufferEvent(const char *topic, const char *payload, uint8_t qos);
bool publishWithBuffer(const char *topic, const char *payload, uint8_t qos, bool retained = false);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void handleCommand(JsonDocument &cmd);
void updateLeds();
String buildScanEventId();
String buildTimestamp();
bool isTagInCooldown(const std::string &epc);
void markTagCooldown(const std::string &epc);
void cleanExpiredCooldowns();
const char *resolvePackageEpc(const char *uidHex);
const char *resolveScanContext();
void updatePackageCounters(const char *uidHex, const char *scanContext);
int findSimulatedPackage(const char *uidHex);
void resetScenarioState();
void addLogBase(JsonDocument &doc, const char *level, const char *event);
void emitLog(JsonDocument &doc);
void logSimpleEvent(const char *level, const char *event, const char *message = nullptr);
void logSetupStep(const char *step, const char *status = "ok", const char *level = "INFO");
void logStateSnapshot(const char *reason);

// =============================================================================
// MACHINE-READABLE SERIAL LOGGING
// =============================================================================
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

// =============================================================================
// SETUP
// =============================================================================
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

// =============================================================================
// TOPIC SETUP
// =============================================================================
void setupTopics()
{
  snprintf(topicTelemetry, sizeof(topicTelemetry), "logistics/mobile/%s/telemetry", DEVICE_ID);
  snprintf(topicScan, sizeof(topicScan), "logistics/mobile/%s/scan", DEVICE_ID);
  snprintf(topicHeartbeat, sizeof(topicHeartbeat), "logistics/mobile/%s/heartbeat", DEVICE_ID);
  snprintf(topicCmd, sizeof(topicCmd), "logistics/mobile/%s/cmd", DEVICE_ID);
}

// =============================================================================
// WiFi
// =============================================================================
void connectWifi()
{
#if LOG_HUMAN
  Serial.printf("[WiFi] Connecting to ");
  Serial.print(WIFI_SSID);
  Serial.print(" ...");
#endif
  JsonDocument startLog;
  addLogBase(startLog, "INFO", "wifi_status");
  startLog["status"] = "connecting";
  startLog["ssid"] = WIFI_SSID;
  emitLog(startLog);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (!WiFi.isConnected() && millis() - start < 10000)
  {
    delay(250);
#if LOG_HUMAN
    Serial.print(".");
    Serial.flush();
#endif
  }

#if LOG_HUMAN
  Serial.println();
  Serial.flush();
#endif

  if (WiFi.isConnected())
  {
    device.wifiConnected = true;
#if LOG_HUMAN
    Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
#endif
    JsonDocument okLog;
    addLogBase(okLog, "INFO", "wifi_status");
    okLog["status"] = "connected";
    okLog["ssid"] = WIFI_SSID;
    okLog["ip"] = WiFi.localIP().toString();
    okLog["rssi"] = WiFi.RSSI();
    emitLog(okLog);
    digitalWrite(PIN_LED_WIFI, HIGH);
  }
  else
  {
#if LOG_HUMAN
    Serial.println(F("\n[WiFi] Connection failed — will retry"));
#endif
    JsonDocument failLog;
    addLogBase(failLog, "WARN", "wifi_status");
    failLog["status"] = "failed";
    failLog["ssid"] = WIFI_SSID;
    failLog["retry_ms"] = WIFI_RECONNECT_MS;
    emitLog(failLog);
    digitalWrite(PIN_LED_WIFI, LOW);
  }

  Serial.flush();
}

// =============================================================================
// MQTT
// =============================================================================
void connectMqtt()
{
#if LOG_HUMAN
  Serial.printf("[MQTT] Connecting to %s:%d ...\n", MQTT_HOST, MQTT_PORT);
#endif
  JsonDocument startLog;
  addLogBase(startLog, "INFO", "mqtt_status");
  startLog["status"] = "connecting";
  startLog["host"] = MQTT_HOST;
  startLog["port"] = MQTT_PORT;
  emitLog(startLog);
  bool ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  if (ok)
  {
    device.mqttConnected = true;
#if LOG_HUMAN
    Serial.println(F("[MQTT] Connected"));
    Serial.flush();
#endif
    digitalWrite(PIN_LED_MQTT, HIGH);
    // Subscribe to command topic (QoS 2)
    mqttClient.subscribe(topicCmd, 2);
#if LOG_HUMAN
    Serial.printf("[MQTT] Subscribed to %s\n", topicCmd);
    Serial.flush();
#endif
    JsonDocument okLog;
    addLogBase(okLog, "INFO", "mqtt_status");
    okLog["status"] = "connected";
    okLog["host"] = MQTT_HOST;
    okLog["port"] = MQTT_PORT;
    okLog["client_id"] = MQTT_CLIENT_ID;
    okLog["subscribed_topic"] = topicCmd;
    emitLog(okLog);
    // Publish initial heartbeat
    publishHeartbeat();
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[MQTT] Failed. rc=%d — will retry\n", mqttClient.state());
#endif
    JsonDocument failLog;
    addLogBase(failLog, "WARN", "mqtt_status");
    failLog["status"] = "failed";
    failLog["host"] = MQTT_HOST;
    failLog["port"] = MQTT_PORT;
    failLog["rc"] = mqttClient.state();
    failLog["retry_ms"] = MQTT_RECONNECT_MS;
    emitLog(failLog);
    digitalWrite(PIN_LED_MQTT, LOW);
#if LOG_HUMAN
    Serial.flush();
#endif
  }

  Serial.flush();
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
#if LOG_HUMAN
  Serial.printf("[MQTT] Message on %s (%u bytes)\n", topic, length);
#endif
  JsonDocument rxLog;
  addLogBase(rxLog, "INFO", "cmd_received");
  rxLog["source"] = "mqtt";
  rxLog["topic"] = topic;
  rxLog["payload_bytes"] = length;
  emitLog(rxLog);
  if (length > 512)
  {
#if LOG_HUMAN
    Serial.println(F("[MQTT] Payload too large, ignoring"));
#endif
    JsonDocument errLog;
    addLogBase(errLog, "ERROR", "cmd_received");
    errLog["source"] = "mqtt";
    errLog["status"] = "payload_too_large";
    errLog["payload_bytes"] = length;
    emitLog(errLog);
    return;
  }
  char buf[513];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err)
  {
#if LOG_HUMAN
    Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
#endif
    JsonDocument errLog;
    addLogBase(errLog, "ERROR", "cmd_received");
    errLog["source"] = "mqtt";
    errLog["status"] = "json_parse_error";
    errLog["error"] = err.c_str();
    emitLog(errLog);
    return;
  }
  handleCommand(doc);
}

// =============================================================================
// REMOTE COMMAND HANDLER
// =============================================================================
void handleCommand(JsonDocument &cmd)
{
  const char *command = cmd["command"] | "unknown";
  const char *cmdId = cmd["command_id"] | "?";
#if LOG_HUMAN
  Serial.printf("[CMD] Received command='%s' id='%s'\n", command, cmdId);
#endif
  JsonDocument cmdLog;
  addLogBase(cmdLog, "INFO", "cmd_received");
  cmdLog["source"] = "mqtt";
  cmdLog["command"] = command;
  cmdLog["command_id"] = cmdId;
  cmdLog["cmd_seq"] = ++cmdSeq;
  emitLog(cmdLog);

  if (strcmp(command, "update_role") == 0)
  {
    const char *newRole = cmd["payload"]["new_role"] | device.role;
    const char *newFacility = cmd["payload"]["new_facility_id"] | device.facilityId;
    const char *newLocName = cmd["payload"]["new_location_name"] | device.locationName;
    strlcpy(device.role, newRole, sizeof(device.role));
    strlcpy(device.facilityId, newFacility, sizeof(device.facilityId));
    strlcpy(device.locationName, newLocName, sizeof(device.locationName));
#if LOG_HUMAN
    Serial.printf("[CMD] Role updated → role=%s facility=%s\n", device.role, device.facilityId);
#endif
    logStateSnapshot("cmd_update_role");
    publishHeartbeat(); // confirm new state
  }
  else if (strcmp(command, "force_scan") == 0)
  {
#if LOG_HUMAN
    Serial.println(F("[CMD] Force scan triggered"));
#endif
    rfidCooldownMap.clear(); // bypass cooldowns
    logSimpleEvent("INFO", "cmd_received", "force_scan_triggered");
    handleRfidScan();
  }
  else if (strcmp(command, "set_cooldown") == 0)
  {
    // In firmware we use a compile-time constant, but we honour the intent
#if LOG_HUMAN
    Serial.println(F("[CMD] set_cooldown acknowledged (firmware uses compile-time constant)"));
#endif
    logSimpleEvent("INFO", "cmd_received", "set_cooldown_acknowledged");
  }
  else if (strcmp(command, "reboot") == 0)
  {
#if LOG_HUMAN
    Serial.println(F("[CMD] Reboot command received — restarting in 1s"));
#endif
    logSimpleEvent("WARN", "cmd_received", "reboot_requested");
    delay(1000);
    ESP.restart();
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[CMD] Unknown command: %s\n", command);
#endif
    logSimpleEvent("WARN", "cmd_received", "unknown_command");
  }
}

// =============================================================================
// SERIAL SCENARIO COMMANDS
// =============================================================================
void handleSerialScenarioCommands()
{
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (serialCommandBuffer.length() > 0)
      {
        processScenarioCommand(serialCommandBuffer);
        serialCommandBuffer = "";
      }
      continue;
    }

    if (serialCommandBuffer.length() < 96)
    {
      serialCommandBuffer += c;
    }
    else
    {
      serialCommandBuffer = "";
#if LOG_HUMAN
      Serial.println(F("[SCENARIO] Command too long - discarded"));
#endif
      logSimpleEvent("WARN", "scenario_command", "command_too_long");
    }
  }
}

void processScenarioCommand(String command)
{
  command.trim();
  command.toUpperCase();

  if (command.length() == 0)
    return;

  JsonDocument cmdLog;
  addLogBase(cmdLog, "INFO", "scenario_command");
  cmdLog["source"] = "serial";
  cmdLog["command"] = command;
  emitLog(cmdLog);

  if (command == "STATUS")
  {
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] STATUS requested"));
#endif
    logStateSnapshot("serial_status");
    return;
  }

  if (command == "FORCE_SCAN")
  {
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] FORCE_SCAN received - cooldown cleared and RFID scan requested"));
#endif
    rfidCooldownMap.clear();
    logSimpleEvent("INFO", "scenario_command", "force_scan");
    handleRfidScan();
    return;
  }

  if (!command.startsWith("SCENARIO "))
  {
#if LOG_HUMAN
    Serial.printf("[SCENARIO] Unknown serial command: %s\n", command.c_str());
#endif
    logSimpleEvent("WARN", "scenario_command", "unknown_serial_command");
    return;
  }

  String mode = command.substring(9);
  mode.trim();

  if (mode == "PICKUP")
  {
    strlcpy(scenarioScanContext, "pickup", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: pickup"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_pickup_armed");
  }
  else if (mode == "IN_TRANSIT")
  {
    strlcpy(scenarioScanContext, "in_transit", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: in_transit"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_in_transit_armed");
  }
  else if (mode == "DELIVERED")
  {
    strlcpy(scenarioScanContext, "delivered", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: delivered"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_delivered_armed");
  }
  else if (mode == "RESET")
  {
    resetScenarioState();
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Scenario state reset"));
#endif
    logStateSnapshot("scenario_reset");
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[SCENARIO] Unsupported mode: %s\n", mode.c_str());
#endif
    logSimpleEvent("WARN", "scenario_command", "unsupported_mode");
  }
}

// =============================================================================
// GPS TELEMETRY PUBLISH  (QoS 1, retained)
// =============================================================================
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

// =============================================================================
// HEARTBEAT PUBLISH  (QoS 0)
// =============================================================================
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

// =============================================================================
// RFID SCAN HANDLING
// =============================================================================
void handleRfidScan()
{
  uint8_t uid[16] = {0};
  uint8_t uidLen = 0;

  // Non-blocking read attempt (timeout = 50ms for responsiveness)
  bool found = rfid.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
  if (!found)
    return;

  // Build a hex EPC string from the raw UID bytes
  // In simulation the PN532 chip returns tag UIDs; we reconstruct EPC from them
  const uint8_t safeUidLen = uidLen > 7 ? 7 : uidLen;
  char uidHex[32] = {0};
  for (uint8_t i = 0; i < safeUidLen; i++)
  {
    snprintf(uidHex + (i * 2), sizeof(uidHex) - (i * 2), "%02X", uid[i]);
  }

  // Map known Wokwi virtual card UIDs to simulated logistics package EPCs.
  // Unknown tags keep the generic LOG-{UID} fallback.
  char epcStr[64];
  strlcpy(epcStr, resolvePackageEpc(uidHex), sizeof(epcStr));

  std::string epcKey(epcStr);

  // --- Cooldown check (30s per tag) ---
  if (isTagInCooldown(epcKey))
  {
#if LOG_HUMAN
    Serial.printf("[RFID] Tag %s in cooldown - suppressed\n", epcStr);
#endif
    JsonDocument coolLog;
    addLogBase(coolLog, "INFO", "rfid_cooldown");
    coolLog["epc"] = epcStr;
    coolLog["uid"] = uidHex;
    coolLog["cooldown_ms"] = RFID_COOLDOWN_MS;
    coolLog["suppressed"] = true;
    emitLog(coolLog);
    return;
  }
  markTagCooldown(epcKey);

  const char *scanContext = resolveScanContext();

  // Flash RFID LED
  digitalWrite(PIN_LED_RFID, HIGH);
  publishScanEvent(epcStr, scanContext);
  delay(100);
  digitalWrite(PIN_LED_RFID, LOW);

  device.scansToday++;
  updatePackageCounters(uidHex, scanContext);

#if LOG_HUMAN
  Serial.printf("[RFID] Tag scanned: %s | uid=%s | ctx=%s | active=%d | scans_today=%d\n",
                epcStr, uidHex, scanContext, device.activePackageCount, device.scansToday);
  Serial.flush();
#endif

  JsonDocument scanLog;
  addLogBase(scanLog, "INFO", "rfid_scan");
  scanLog["epc"] = epcStr;
  scanLog["uid"] = uidHex;
  scanLog["scan_context"] = scanContext;
  scanLog["active_package_count"] = device.activePackageCount;
  scanLog["scans_today"] = device.scansToday;
  scanLog["scenario_context_armed"] = scenarioContextArmed;
  emitLog(scanLog);

  if (scenarioContextArmed)
  {
#if LOG_HUMAN
    Serial.printf("[SCENARIO] EPC=%s\n", epcStr);
    Serial.printf("[SCENARIO] CTX=%s\n", scanContext);
    Serial.printf("[SCENARIO] ACTIVE=%d\n", device.activePackageCount);
#endif
    JsonDocument resultLog;
    addLogBase(resultLog, "INFO", "scenario_result");
    resultLog["epc"] = epcStr;
    resultLog["uid"] = uidHex;
    resultLog["scan_context"] = scanContext;
    resultLog["active_package_count"] = device.activePackageCount;
    resultLog["scans_today"] = device.scansToday;
    resultLog["consumed"] = true;
    emitLog(resultLog);
    scenarioContextArmed = false;
    scenarioScanContext[0] = '\0';
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Scan context consumed"));
#endif
  }
}

// =============================================================================
// SCAN EVENT PUBLISH  (QoS 2)
// =============================================================================
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

// =============================================================================
// PUBLISH WITH OFFLINE BUFFER
// =============================================================================
bool publishWithBuffer(const char *topic, const char *payload, uint8_t qos, bool retained)
{
  if (mqttClient.connected())
  {
    bool ok = mqttClient.publish(topic, (const uint8_t *)payload, strlen(payload), retained);
    if (!ok)
    {
      bufferEvent(topic, payload, qos);
      return false;
    }
    return true;
  }
  bufferEvent(topic, payload, qos);
  return false;
}

void bufferEvent(const char *topic, const char *payload, uint8_t qos)
{
  if (eventBuffer.size() >= EVENT_BUFFER_MAX)
  {
#if LOG_HUMAN
    Serial.println(F("[BUF] Buffer full — dropping oldest event"));
#endif
    JsonDocument dropLog;
    addLogBase(dropLog, "WARN", "buffer_event");
    dropLog["action"] = "drop_oldest";
    dropLog["size_before"] = (int)eventBuffer.size();
    dropLog["max"] = EVENT_BUFFER_MAX;
    emitLog(dropLog);
    eventBuffer.erase(eventBuffer.begin());
  }
  BufferedEvent ev;
  strlcpy(ev.topic, topic, sizeof(ev.topic));
  strlcpy(ev.payload, payload, sizeof(ev.payload));
  ev.qos = qos;
  eventBuffer.push_back(ev);
#if LOG_HUMAN
  Serial.printf("[BUF] Buffered event. Buffer size: %d\n", (int)eventBuffer.size());
#endif
  JsonDocument bufLog;
  addLogBase(bufLog, "WARN", "buffer_event");
  bufLog["action"] = "buffered";
  bufLog["topic"] = topic;
  bufLog["qos"] = qos;
  bufLog["size"] = (int)eventBuffer.size();
  emitLog(bufLog);
}

void flushEventBuffer()
{
  if (eventBuffer.empty())
    return;
#if LOG_HUMAN
  Serial.printf("[BUF] Flushing %d buffered events\n", (int)eventBuffer.size());
#endif
  JsonDocument startLog;
  addLogBase(startLog, "INFO", "buffer_event");
  startLog["action"] = "flush_start";
  startLog["size"] = (int)eventBuffer.size();
  emitLog(startLog);
  while (!eventBuffer.empty() && mqttClient.connected())
  {
    BufferedEvent &ev = eventBuffer.front();
    bool ok = mqttClient.publish(ev.topic, (const uint8_t *)ev.payload, strlen(ev.payload), false);
    if (ok)
    {
#if LOG_HUMAN
      Serial.printf("[BUF] Flushed to %s\n", ev.topic);
#endif
      JsonDocument flushLog;
      addLogBase(flushLog, "INFO", "buffer_event");
      flushLog["action"] = "flushed";
      flushLog["topic"] = ev.topic;
      flushLog["remaining_before_erase"] = (int)eventBuffer.size();
      emitLog(flushLog);
      eventBuffer.erase(eventBuffer.begin());
    }
    else
    {
      break; // MQTT busy/full — retry next loop
    }
  }
}

// =============================================================================
// RFID COOLDOWN HELPERS
// =============================================================================
bool isTagInCooldown(const std::string &epc)
{
  auto it = rfidCooldownMap.find(epc);
  if (it == rfidCooldownMap.end())
    return false;
  return (millis() - it->second) < RFID_COOLDOWN_MS;
}

void markTagCooldown(const std::string &epc)
{
  rfidCooldownMap[epc] = millis();
}

void cleanExpiredCooldowns()
{
  // Run every ~5s to prevent unbounded map growth
  static unsigned long lastClean = 0;
  if (millis() - lastClean < 5000)
    return;
  lastClean = millis();
  for (auto it = rfidCooldownMap.begin(); it != rfidCooldownMap.end();)
  {
    if ((millis() - it->second) >= RFID_COOLDOWN_MS)
    {
      it = rfidCooldownMap.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

// =============================================================================
// SIMULATED PACKAGE HELPERS
// =============================================================================
const char *resolvePackageEpc(const char *uidHex)
{
  int index = findSimulatedPackage(uidHex);
  if (index >= 0)
  {
    return simulatedPackages[index].epc;
  }

  static char fallbackEpc[64];
  snprintf(fallbackEpc, sizeof(fallbackEpc), "LOG-%s", uidHex);
  return fallbackEpc;
}

const char *resolveScanContext()
{
  if (scenarioContextArmed && scenarioScanContext[0] != '\0')
  {
    return scenarioScanContext;
  }

  if (gps.speed.isValid() && gps.speed.kmph() < 2.0)
  {
    return "pickup";
  }
  return "in_transit";
}

void updatePackageCounters(const char *uidHex, const char *scanContext)
{
  int index = findSimulatedPackage(uidHex);

  if (strcmp(scanContext, "pickup") == 0)
  {
    if (index >= 0)
    {
      if (!simulatedPackages[index].onboard)
      {
        simulatedPackages[index].onboard = true;
        device.activePackageCount++;
      }
      return;
    }

    device.activePackageCount++;
  }
  else if (strcmp(scanContext, "delivered") == 0)
  {
    if (index >= 0)
    {
      if (simulatedPackages[index].onboard)
      {
        simulatedPackages[index].onboard = false;
        if (device.activePackageCount > 0)
          device.activePackageCount--;
      }
      return;
    }

    if (device.activePackageCount > 0)
      device.activePackageCount--;
  }
}

int findSimulatedPackage(const char *uidHex)
{
  for (size_t i = 0; i < sizeof(simulatedPackages) / sizeof(simulatedPackages[0]); i++)
  {
    if (strcmp(uidHex, simulatedPackages[i].uidHex) == 0)
    {
      return (int)i;
    }
  }
  return -1;
}

void resetScenarioState()
{
  scenarioContextArmed = false;
  scenarioScanContext[0] = '\0';
  rfidCooldownMap.clear();
  device.activePackageCount = 0;
  device.scansToday = 0;
  for (size_t i = 0; i < sizeof(simulatedPackages) / sizeof(simulatedPackages[0]); i++)
  {
    simulatedPackages[i].onboard = false;
  }
}

// =============================================================================
// LED STATUS INDICATORS
// =============================================================================
void updateLeds()
{
  digitalWrite(PIN_LED_WIFI, device.wifiConnected ? HIGH : LOW);
  digitalWrite(PIN_LED_MQTT, device.mqttConnected ? HIGH : LOW);
  // GPS LED: solid = fix, blink = no fix
  static unsigned long lastGpsBlink = 0;
  static bool gpsBlink = false;
  if (device.gpsFix)
  {
    digitalWrite(PIN_LED_GPS, HIGH);
  }
  else
  {
    if (millis() - lastGpsBlink > 500)
    {
      lastGpsBlink = millis();
      gpsBlink = !gpsBlink;
      digitalWrite(PIN_LED_GPS, gpsBlink ? HIGH : LOW);
    }
  }
}

// =============================================================================
// UTILITY: Build scan event ID
// =============================================================================
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
