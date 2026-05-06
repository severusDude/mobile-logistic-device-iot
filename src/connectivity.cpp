/**
 * =============================================================================
 * WiFi, MQTT, Commands, And Offline Buffer
 * =============================================================================
 * Purpose:
 *   Manages network connectivity and MQTT command/publish resilience.
 *
 * Responsibilities:
 *   Build topics, connect WiFi/MQTT, handle remote commands, and buffer/flush events.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "connectivity.h"
#include "logging.h"
#include "rfid.h"
#include "telemetry.h"

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
