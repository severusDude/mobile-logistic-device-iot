/**
 * =============================================================================
 * Firmware Build Configuration
 * =============================================================================
 * Purpose:
 *   Centralizes compile-time identity, credentials, pins, timing, and logging
 *   flags for the ESP32 mobile logistics firmware.
 *
 * Responsibilities:
 *   Preserve all PlatformIO build-flag override behavior from the original
 *   monolithic src/main.cpp.
 *
 * Shared state:
 *   Header-only constants/macros consumed by every firmware module.
 * =============================================================================
 */
#pragma once

#include <Arduino.h>

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

#define EVENT_BUFFER_MAX 50
