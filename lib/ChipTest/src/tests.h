#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>

// ─── Hardware config (shared with tests) ───────────────────────────────────
#define PIN_LED_WIFI 2
#define PIN_LED_MQTT 4
#define PIN_LED_RFID 5
#define PIN_LED_GPS 18

#define GPS_RX_PIN 3
#define GPS_TX_PIN 1
#define GPS_BAUD 9600

#define PN532_I2C_ADDR 0x48
#define PN532_CMD_READ 0x30

#define GPS_TIMEOUT_MS 8000
#define SERIAL_BAUD 115200

// ─── Globals (defined in main.cpp) ──────────────────────────────────────────
extern TinyGPSPlus gps;
extern HardwareSerial gpsSerial;

// ─── Test framework ─────────────────────────────────────────────────────────
typedef void (*TestFunction)();

struct Test
{
    const char *name;
    TestFunction run;
};

void runAllTests();
void printSummary();

void test_i2c_bus_scan();
void test_pn532_command();
void test_gps_uart_reception();
void test_gps_fields();
void test_continuous_cycles();

// Helpers used by tests
void printSeparator(const char *title);
void pass(const char *msg);
void fail(const char *msg);
void info(const char *label, float value, uint8_t decimals = 6);
void infoStr(const char *label, const char *value);