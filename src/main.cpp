/**
 * main.cpp — Mobile Device Chip Tests
 * GPS Logistic Package Tracking System
 *
 * Device Type : Mobile (Truck / Delivery Van)
 * MCU         : ESP32 DevKit V1
 * Chips under test:
 *   [1] GPS NEO-6M  — custom chip, UART2 (RX=TX, TX=RX), 9600 baud
 *   [2] PN532 RFID  — custom chip, I2C (SDA=21, SCL=22), addr=0x48
 *
 * LED indicators:
 *   2  (blue)   — WiFi connected (unused in chip tests, driven LOW)
 *   4  (green)  — MQTT connected (unused in chip tests, driven LOW)
 *   5  (yellow) — RFID scan activity
 *   18 (red)    — GPS fix acquired
 *
 * Test sequence (runs once in setup(), then loops):
 *   TEST 1 — I2C bus scan: detect PN532 at 0x48
 *   TEST 2 — PN532 command exchange: write 0x30, read 1 byte response
 *   TEST 3 — GPS UART data reception: receive & parse NMEA sentences
 *   TEST 4 — GPS field validation: lat, lng, speed, date/time
 *   TEST 5 — Continuous read: 10 GPS+RFID cycles with timing report
 *
 * Serial output baud: 115200
 */

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>

// ─── Pin definitions ────────────────────────────────────────────────────────
#define PIN_LED_WIFI 2
#define PIN_LED_MQTT 4
#define PIN_LED_RFID 5
#define PIN_LED_GPS 18

#define GPS_RX_PIN 3 // TX pin GPIO1
#define GPS_TX_PIN 1 // RX pin GPIO3
#define GPS_BAUD 9600

#define PN532_I2C_ADDR 0x48
#define PN532_CMD_READ 0x30 // example command defined by custom chip

#define SERIAL_BAUD 115200
#define GPS_TIMEOUT_MS 8000 // max ms to wait for first valid GPS sentence

// ─── Globals ────────────────────────────────────────────────────────────────
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2

// Test result counters
uint8_t testsPassed = 0;
uint8_t testsFailed = 0;

// ─── Helpers ────────────────────────────────────────────────────────────────
void printSeparator(const char *title)
{
  Serial.println();
  Serial.println(F("════════════════════════════════════════════"));
  Serial.print(F("  "));
  Serial.println(title);
  Serial.println(F("════════════════════════════════════════════"));
}

void pass(const char *msg)
{
  Serial.print(F("  [PASS] "));
  Serial.println(msg);
  testsPassed++;
}

void fail(const char *msg)
{
  Serial.print(F("  [FAIL] "));
  Serial.println(msg);
  testsFailed++;
}

void info(const char *label, float value, uint8_t decimals = 6)
{
  Serial.print(F("         "));
  Serial.print(label);
  Serial.println(value, decimals);
}

void infoStr(const char *label, const char *value)
{
  Serial.print(F("         "));
  Serial.print(label);
  Serial.println(value);
}

// ─── TEST 1: I2C Bus Scan ────────────────────────────────────────────────────
// Scans the I2C bus and checks that the PN532 custom chip responds at 0x48.
void test_i2c_bus_scan()
{
  printSeparator("TEST 1 — I2C Bus Scan");

  bool pn532Found = false;
  uint8_t devicesFound = 0;

  Serial.println(F("  Scanning I2C bus (SDA=21, SCL=22)..."));

  for (uint8_t addr = 1; addr < 127; addr++)
  {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0)
    {
      Serial.print(F("  Device found at 0x"));
      if (addr < 16)
        Serial.print('0');
      Serial.println(addr, HEX);
      devicesFound++;

      if (addr == PN532_I2C_ADDR)
      {
        pn532Found = true;
      }
    }
  }

  if (devicesFound == 0)
  {
    fail("No I2C devices found — check SDA/SCL wiring");
    return;
  }

  if (pn532Found)
  {
    pass("PN532 detected at expected address 0x48");
  }
  else
  {
    fail("PN532 NOT found at 0x48 — check custom chip wiring");
  }
}

// ─── TEST 2: PN532 Command Exchange ─────────────────────────────────────────
// Writes command 0x30 to the PN532 chip and reads back 1 byte response.
// Validates that the chip ACKs the transmission and returns data.
void test_pn532_command()
{
  printSeparator("TEST 2 — PN532 Command Exchange");

  // Step 2a: Send command
  Wire.beginTransmission(PN532_I2C_ADDR);
  Wire.write(PN532_CMD_READ);
  uint8_t txErr = Wire.endTransmission();

  if (txErr != 0)
  {
    Serial.print(F("  I2C TX error code: "));
    Serial.println(txErr);
    fail("PN532 did not ACK command 0x30 — transmission failed");
    return;
  }
  pass("PN532 ACKed command 0x30 (TX error=0)");
  digitalWrite(PIN_LED_RFID, HIGH);

  // Step 2b: Read response byte
  delay(10); // small settling delay
  uint8_t bytesReceived = Wire.requestFrom((uint8_t)PN532_I2C_ADDR, (uint8_t)1);

  if (bytesReceived == 0 || !Wire.available())
  {
    fail("PN532 returned no data after requestFrom");
    digitalWrite(PIN_LED_RFID, LOW);
    return;
  }

  uint8_t response = Wire.read();
  Serial.print(F("         Response byte: 0x"));
  if (response < 16)
    Serial.print('0');
  Serial.println(response, HEX);
  pass("PN532 returned 1 byte response successfully");

  // Step 2c: Repeated read for stability (5 cycles)
  Serial.println(F("  Stability check: 5 repeated reads..."));
  uint8_t successCount = 0;
  for (uint8_t i = 0; i < 5; i++)
  {
    Wire.beginTransmission(PN532_I2C_ADDR);
    Wire.write(PN532_CMD_READ);
    uint8_t err = Wire.endTransmission();

    delay(100);
    Wire.requestFrom((uint8_t)PN532_I2C_ADDR, (uint8_t)1);
    if (err == 0 && Wire.available())
    {
      Wire.read();
      successCount++;
    }
    delay(200);
  }

  Serial.print(F("         Successful reads: "));
  Serial.print(successCount);
  Serial.println(F("/5"));

  if (successCount == 5)
  {
    pass("PN532 stable across 5 repeated command/read cycles");
  }
  else if (successCount >= 3)
  {
    pass("PN532 mostly stable (3-4/5 reads succeeded)");
  }
  else
  {
    fail("PN532 unstable — less than 3/5 reads succeeded");
  }

  digitalWrite(PIN_LED_RFID, LOW);
}

// ─── TEST 3: GPS UART Data Reception ────────────────────────────────────────
// Waits up to GPS_TIMEOUT_MS for NMEA sentences to arrive on Serial2.
// Checks that characters are being received and fed into TinyGPS++ encoder.
void test_gps_uart_reception()
{
  printSeparator("TEST 3 — GPS UART Data Reception");

  Serial.print(F("  Waiting for NMEA data on UART2 (RX=1) up to "));
  Serial.print(GPS_TIMEOUT_MS / 1000);
  Serial.println(F("s..."));

  uint32_t start = millis();
  uint32_t charsReceived = 0;
  uint32_t sentencesEncoded = 0;

  while (millis() - start < GPS_TIMEOUT_MS)
  {
    while (gpsSerial.available())
    {
      char c = gpsSerial.read();
      charsReceived++;
      if (gps.encode(c))
      {
        sentencesEncoded++;
      }
    }
    // Break early once we have enough sentences
    if (sentencesEncoded >= 3)
      break;
    delay(10);
  }

  Serial.print(F("         Characters received : "));
  Serial.println(charsReceived);
  Serial.print(F("         NMEA sentences parsed: "));
  Serial.println(sentencesEncoded);

  if (charsReceived == 0)
  {
    fail("No characters received from GPS chip — check VCC/GND/RX/TX wiring");
    return;
  }
  pass("GPS chip is transmitting data on UART2");

  if (sentencesEncoded == 0)
  {
    fail("Data received but no valid NMEA sentences decoded — possible baud mismatch");
  }
  else
  {
    pass("TinyGPS++ successfully decoded at least 1 NMEA sentence");
  }
}

// ─── TEST 4: GPS Field Validation ───────────────────────────────────────────
// Validates that the GPS chip provides valid location, speed, and datetime.
// Waits for a valid fix, then checks all critical fields.
void test_gps_fields()
{
  printSeparator("TEST 4 — GPS Field Validation");

  Serial.println(F("  Waiting for valid GPS fix..."));
  uint32_t start = millis();

  while (millis() - start < GPS_TIMEOUT_MS)
  {
    while (gpsSerial.available())
    {
      gps.encode(gpsSerial.read());
    }
    if (gps.location.isValid())
      break;
    delay(10);
  }

  // 4a: Location
  if (gps.location.isValid())
  {
    info("Latitude  : ", gps.location.lat());
    info("Longitude : ", gps.location.lng());
    pass("GPS location fields are valid");
    digitalWrite(PIN_LED_GPS, HIGH);
  }
  else
  {
    fail("GPS location invalid after timeout — chip may not have fix data");
  }

  // 4b: Altitude
  if (gps.altitude.isValid())
  {
    info("Altitude  : ", gps.altitude.meters(), 1);
    pass("GPS altitude field is valid");
  }
  else
  {
    fail("GPS altitude invalid");
  }

  // 4c: Speed
  if (gps.speed.isValid())
  {
    info("Speed km/h: ", gps.speed.kmph(), 1);
    pass("GPS speed field is valid");
  }
  else
  {
    fail("GPS speed invalid");
  }

  // 4d: Date & Time
  if (gps.date.isValid() && gps.time.isValid())
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    infoStr("DateTime  : ", buf);
    pass("GPS date and time fields are valid");
  }
  else
  {
    fail("GPS date/time invalid");
  }

  // 4e: Satellite count (bonus info)
  if (gps.satellites.isValid())
  {
    Serial.print(F("         Satellites: "));
    Serial.println(gps.satellites.value());
  }
}

// ─── TEST 5: Continuous Cycle Test ──────────────────────────────────────────
// Runs 10 interleaved GPS read + PN532 command cycles and reports timing.
// Validates that both chips can be used concurrently without conflict.
void test_continuous_cycles()
{
  printSeparator("TEST 5 — Continuous Cycle Test (GPS + RFID interleaved)");

  const uint8_t CYCLES = 10;
  uint8_t gpsUpdates = 0;
  uint8_t rfidSuccess = 0;
  uint32_t totalCycleMs = 0;

  Serial.print(F("  Running "));
  Serial.print(CYCLES);
  Serial.println(F(" interleaved GPS+RFID cycles..."));

  for (uint8_t i = 0; i < CYCLES; i++)
  {
    uint32_t cycleStart = millis();

    // --- GPS read window (1000ms) ---
    uint32_t gpsStart = millis();
    bool gpsUpdatedThisCycle = false;
    while (millis() - gpsStart < 1000)
    {
      while (gpsSerial.available())
      {
        if (gps.encode(gpsSerial.read()))
        {
          gpsUpdatedThisCycle = true;
        }
      }
    }
    if (gpsUpdatedThisCycle)
      gpsUpdates++;

    // --- PN532 command/read ---
    Wire.beginTransmission(PN532_I2C_ADDR);
    Wire.write(PN532_CMD_READ);
    uint8_t err = Wire.endTransmission();
    delay(10);
    Wire.requestFrom((uint8_t)PN532_I2C_ADDR, (uint8_t)1);

    bool rfidOk = (err == 0 && Wire.available());
    if (rfidOk)
    {
      Wire.read();
      rfidSuccess++;
      digitalWrite(PIN_LED_RFID, HIGH);
      delay(30);
      digitalWrite(PIN_LED_RFID, LOW);
    }

    uint32_t cycleMs = millis() - cycleStart;
    totalCycleMs += cycleMs;

    Serial.print(F("  Cycle "));
    Serial.print(i + 1);
    Serial.print(F(": GPS="));
    Serial.print(gpsUpdatedThisCycle ? "OK" : "--");
    Serial.print(F("  RFID="));
    Serial.print(rfidOk ? "OK" : "FAIL");
    Serial.print(F("  ("));
    Serial.print(cycleMs);
    Serial.println(F("ms)"));

    delay(200);
  }

  Serial.println();
  Serial.print(F("  GPS  updates : "));
  Serial.print(gpsUpdates);
  Serial.print(F("/"));
  Serial.println(CYCLES);
  Serial.print(F("  RFID success : "));
  Serial.print(rfidSuccess);
  Serial.print(F("/"));
  Serial.println(CYCLES);
  Serial.print(F("  Avg cycle ms : "));
  Serial.println(totalCycleMs / CYCLES);

  if (gpsUpdates >= CYCLES * 0.8)
  {
    pass("GPS data flow stable during concurrent RFID operations (>=80%)");
  }
  else
  {
    fail("GPS data flow degraded during concurrent RFID ops (<80%)");
  }

  if (rfidSuccess >= CYCLES * 0.8)
  {
    pass("PN532 I2C stable during concurrent GPS UART operations (>=80%)");
  }
  else
  {
    fail("PN532 I2C degraded during concurrent GPS UART ops (<80%)");
  }
}

// ─── Summary ────────────────────────────────────────────────────────────────
void printSummary()
{
  printSeparator("TEST SUMMARY — Mobile Device");
  Serial.print(F("  Tests passed : "));
  Serial.println(testsPassed);
  Serial.print(F("  Tests failed : "));
  Serial.println(testsFailed);
  if (testsFailed == 0)
  {
    Serial.println(F("  Result       : ALL TESTS PASSED ✓"));
  }
  else
  {
    Serial.println(F("  Result       : SOME TESTS FAILED — review output above"));
  }
  Serial.println();
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  delay(500);

  // LED pins
  pinMode(PIN_LED_WIFI, OUTPUT);
  pinMode(PIN_LED_MQTT, OUTPUT);
  pinMode(PIN_LED_RFID, OUTPUT);
  pinMode(PIN_LED_GPS, OUTPUT);
  digitalWrite(PIN_LED_WIFI, LOW);
  digitalWrite(PIN_LED_MQTT, LOW);
  digitalWrite(PIN_LED_RFID, LOW);
  digitalWrite(PIN_LED_GPS, LOW);

  // I2C — PN532
  Wire.begin(21, 22); // SDA=21, SCL=22

  // UART2 — GPS NEO-6M
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println(F("\n\n"));
  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║  MOBILE DEVICE — CHIP INTEGRATION TESTS      ║"));
  Serial.println(F("║  GPS NEO-6M (UART2) + PN532 RFID (I2C 0x48)  ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));

  test_i2c_bus_scan();
  test_pn532_command();
  test_gps_uart_reception();
  test_gps_fields();
  test_continuous_cycles();
  printSummary();
}

// ─── Loop ───────────────────────────────────────────────────────────────────
// After tests complete, loop maintains live GPS reading and blinks GPS LED
// to confirm the chip is still active.
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