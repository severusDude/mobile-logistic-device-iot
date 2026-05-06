/**
 * =============================================================================
 * Connectivity And MQTT Module
 * =============================================================================
 * Purpose:
 *   Declares WiFi, MQTT, command handling, topic setup, and offline buffer helpers.
 *
 * Responsibilities:
 *   Connect/reconnect WiFi and MQTT, process remote commands, and buffer publishes.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#pragma once

#include <ArduinoJson.h>
#include "firmware_state.h"

void setupTopics();
void connectWifi();
void connectMqtt();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void handleCommand(JsonDocument &cmd);
bool publishWithBuffer(const char *topic, const char *payload, uint8_t qos, bool retained = false);
void bufferEvent(const char *topic, const char *payload, uint8_t qos);
void flushEventBuffer();
