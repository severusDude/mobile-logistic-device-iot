#include <Arduino.h>
#include <tests.h>

// ─── Hardware config ───────────────────────────────────
#define PIN_LED_WIFI 2
#define PIN_LED_MQTT 4
#define PIN_LED_RFID 5
#define PIN_LED_GPS 18

#define GPS_RX_PIN 3
#define GPS_TX_PIN 1
#define GPS_BAUD 9600

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

void setup()
{
  Serial.begin(115200);
  delay(500);

  // LEDs
  pinMode(PIN_LED_WIFI, OUTPUT);
  digitalWrite(PIN_LED_WIFI, LOW);
  pinMode(PIN_LED_MQTT, OUTPUT);
  digitalWrite(PIN_LED_MQTT, LOW);
  pinMode(PIN_LED_RFID, OUTPUT);
  digitalWrite(PIN_LED_RFID, LOW);
  pinMode(PIN_LED_GPS, OUTPUT);
  digitalWrite(PIN_LED_GPS, LOW);

  // I2C (PN532) + UART2 (GPS)
  Wire.begin(21, 22);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println(F("\n\n"));
  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║  MOBILE DEVICE — MODULAR CHIP TESTS          ║"));
  Serial.println(F("║  GPS NEO-6M (UART2) + PN532 RFID (I2C)      ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));

  runAllTests();
}

void loop()
{
  while (gpsSerial.available())
  {
    gps.encode(gpsSerial.read());
  }
  if (gps.location.isUpdated())
  {
    digitalWrite(PIN_LED_GPS, HIGH);
    delay(80);
    digitalWrite(PIN_LED_GPS, LOW);
  }
}