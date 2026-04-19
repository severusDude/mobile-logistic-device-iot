#include "tests.h"

uint8_t testsPassed = 0;
uint8_t testsFailed = 0;

// ─── Test registry (easy to add new tests here) ─────────────────────────────
static const Test testSuite[] = {
    {"TEST 1 — I2C Bus Scan", test_i2c_bus_scan},
    {"TEST 2 — PN532 Command Exchange", test_pn532_command},
    {"TEST 3 — GPS UART Data Reception", test_gps_uart_reception},
    {"TEST 4 — GPS Field Validation", test_gps_fields},
    {"TEST 5 — Continuous Cycle Test", test_continuous_cycles}};

// ─── Run all tests ──────────────────────────────────────────────────────────
void runAllTests()
{
    for (const auto &t : testSuite)
    {
        printSeparator(t.name);
        t.run();
    }
    printSummary();
}

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

void info(const char *label, float value, uint8_t decimals)
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

void printSummary()
{
    printSeparator("TEST SUMMARY — Mobile Device");
    Serial.print(F("  Tests passed : "));
    Serial.println(testsPassed);
    Serial.print(F("  Tests failed : "));
    Serial.println(testsFailed);
    if (testsFailed == 0)
        Serial.println(F("  Result       : ALL TESTS PASSED ✓"));
    else
        Serial.println(F("  Result       : SOME TESTS FAILED — review output above"));
    Serial.println();
}

// ─── Individual test implementations (full logic) ───────────────────────────
void test_i2c_bus_scan()
{
    bool pn532Found = false;
    uint8_t devicesFound = 0;
    Serial.println(F("  Scanning I2C bus (SDA=21, SCL=22)..."));

    for (uint8_t addr = 1; addr < 127; addr++)
    {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
        {
            Serial.print(F("  Device found at 0x"));
            if (addr < 16)
                Serial.print('0');
            Serial.println(addr, HEX);
            devicesFound++;
            if (addr == PN532_I2C_ADDR)
                pn532Found = true;
        }
    }

    if (devicesFound == 0)
    {
        fail("No I2C devices found");
        return;
    }
    pn532Found ? pass("PN532 detected at 0x48") : fail("PN532 NOT found at 0x48");
}

void test_pn532_command()
{
    Wire.beginTransmission(PN532_I2C_ADDR);
    Wire.write(PN532_CMD_READ);
    uint8_t txErr = Wire.endTransmission();

    if (txErr != 0)
    {
        fail("PN532 did not ACK command 0x30");
        return;
    }
    pass("PN532 ACKed command 0x30");
    digitalWrite(PIN_LED_RFID, HIGH);

    delay(10);
    uint8_t bytes = Wire.requestFrom((uint8_t)PN532_I2C_ADDR, (uint8_t)1);
    if (bytes == 0 || !Wire.available())
    {
        fail("PN532 returned no data");
        digitalWrite(PIN_LED_RFID, LOW);
        return;
    }

    uint8_t response = Wire.read();
    Serial.print(F("         Response byte: 0x"));
    if (response < 16)
        Serial.print('0');
    Serial.println(response, HEX);
    pass("PN532 returned 1 byte response successfully");

    Serial.println(F("  Stability check: 5 repeated reads..."));
    uint8_t success = 0;
    for (uint8_t i = 0; i < 5; i++)
    {
        Wire.beginTransmission(PN532_I2C_ADDR);
        Wire.write(PN532_CMD_READ);
        if (Wire.endTransmission() == 0)
        {
            delay(100);
            if (Wire.requestFrom((uint8_t)PN532_I2C_ADDR, (uint8_t)1) && Wire.available())
            {
                Wire.read();
                success++;
            }
        }
        delay(200);
    }
    Serial.print(F("         Successful reads: "));
    Serial.print(success);
    Serial.println(F("/5"));
    (success == 5) ? pass("PN532 stable") : (success >= 3 ? pass("PN532 mostly stable") : fail("PN532 unstable"));

    digitalWrite(PIN_LED_RFID, LOW);
}

void test_gps_uart_reception()
{
    Serial.print(F("  Waiting for NMEA data on UART2 (RX="));
    Serial.print(GPS_RX_PIN);
    Serial.print(F(") up to "));
    Serial.print(GPS_TIMEOUT_MS / 1000);
    Serial.println(F("s..."));

    uint32_t start = millis(), chars = 0, sentences = 0;
    while (millis() - start < GPS_TIMEOUT_MS)
    {
        while (gpsSerial.available())
        {
            char c = gpsSerial.read();
            chars++;
            if (gps.encode(c))
                sentences++;
        }
        if (sentences >= 3)
            break;
        delay(10);
    }

    Serial.print(F("         Characters received : "));
    Serial.println(chars);
    Serial.print(F("         NMEA sentences parsed: "));
    Serial.println(sentences);

    (chars > 0) ? pass("GPS chip is transmitting data on UART2") : fail("No characters from GPS");
    (sentences > 0) ? pass("TinyGPS++ decoded NMEA sentences") : fail("No valid NMEA sentences");
}

void test_gps_fields()
{
    Serial.println(F("  Waiting for valid GPS fix..."));
    uint32_t start = millis();
    while (millis() - start < GPS_TIMEOUT_MS)
    {
        while (gpsSerial.available())
            gps.encode(gpsSerial.read());
        if (gps.location.isValid())
            break;
        delay(10);
    }

    if (gps.location.isValid())
    {
        info("Latitude  : ", gps.location.lat());
        info("Longitude : ", gps.location.lng());
        pass("GPS location fields are valid");
        digitalWrite(PIN_LED_GPS, HIGH);
    }
    else
        fail("GPS location invalid");

    if (gps.altitude.isValid())
    {
        info("Altitude  : ", gps.altitude.meters(), 1);
        pass("GPS altitude valid");
    }
    else
        fail("GPS altitude invalid");

    if (gps.speed.isValid())
    {
        info("Speed km/h: ", gps.speed.kmph(), 1);
        pass("GPS speed valid");
    }
    else
        fail("GPS speed invalid");

    if (gps.date.isValid() && gps.time.isValid())
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
                 gps.date.year(), gps.date.month(), gps.date.day(),
                 gps.time.hour(), gps.time.minute(), gps.time.second());
        infoStr("DateTime  : ", buf);
        pass("GPS date/time valid");
    }
    else
        fail("GPS date/time invalid");

    if (gps.satellites.isValid())
    {
        Serial.print(F("         Satellites: "));
        Serial.println(gps.satellites.value());
    }
}

void test_continuous_cycles()
{
    const uint8_t CYCLES = 10;
    uint8_t gpsUpdates = 0, rfidSuccess = 0;
    uint32_t totalMs = 0;

    Serial.print(F("  Running "));
    Serial.print(CYCLES);
    Serial.println(F(" interleaved GPS+RFID cycles..."));

    for (uint8_t i = 0; i < CYCLES; i++)
    {
        uint32_t cycleStart = millis();

        // GPS - 1000 ms window (matches 1 Hz chip burst)
        bool gpsOk = false;
        uint32_t gpsStart = millis();
        while (millis() - gpsStart < 1000)
        {
            while (gpsSerial.available())
            {
                if (gps.encode(gpsSerial.read()))
                    gpsOk = true;
            }
        }
        if (gpsOk)
            gpsUpdates++;

        // PN532
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
        totalMs += cycleMs;

        Serial.print(F("  Cycle "));
        Serial.print(i + 1);
        Serial.print(F(": GPS="));
        Serial.print(gpsOk ? "OK" : "--");
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
    Serial.println(totalMs / CYCLES);

    (gpsUpdates >= CYCLES * 0.8) ? pass("GPS data flow stable during concurrent RFID ops")
                                 : fail("GPS data flow degraded during concurrent RFID ops");
    (rfidSuccess >= CYCLES * 0.8) ? pass("PN532 I2C stable during concurrent GPS ops")
                                  : fail("PN532 I2C degraded during concurrent GPS ops");
}