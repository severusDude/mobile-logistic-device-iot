/**
 * =============================================================================
 * LED Status Indicators
 * =============================================================================
 * Purpose:
 *   Drives ESP32 status LEDs for WiFi, MQTT, RFID activity, and GPS fix state.
 *
 * Responsibilities:
 *   Keep existing LED polarity and GPS blink behavior unchanged.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "leds.h"

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
