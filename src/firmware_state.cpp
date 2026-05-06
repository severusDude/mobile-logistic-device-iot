/**
 * =============================================================================
 * Firmware Runtime State Definitions
 * =============================================================================
 * Purpose:
 *   Owns global runtime objects shared by the mobile logistics firmware modules.
 *
 * Responsibilities:
 *   Instantiate topic buffers, event buffers, device state, counters, timers, and hardware clients.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "firmware_state.h"

char topicTelemetry[80];
char topicScan[80];
char topicHeartbeat[80];
char topicCmd[80];
std::vector<BufferedEvent> eventBuffer;
std::map<std::string, unsigned long> rfidCooldownMap;
SimulatedPackage simulatedPackages[] = {
    {"DEADBEEF", "LOG-PKG-20260506-JKTWH-00001", false},
    {"CAFEBABE", "LOG-PKG-20260506-JKTWH-00002", false},
};
const size_t simulatedPackageCount = sizeof(simulatedPackages) / sizeof(simulatedPackages[0]);
char scenarioScanContext[16] = "";
bool scenarioContextArmed = false;
String serialCommandBuffer;
DeviceState device;
uint32_t telemetrySeq = 0;
uint32_t scanSeq = 0;
uint32_t cmdSeq = 0;
uint32_t logSeq = 0;
unsigned long lastTelemetryMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastRfidScanMs = 0;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
Adafruit_PN532 rfid(PN532_IRQ_PIN, PN532_RESET_PIN);
