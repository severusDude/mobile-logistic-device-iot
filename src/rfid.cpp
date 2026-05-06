/**
 * =============================================================================
 * RFID Scanning And Simulated Package State
 * =============================================================================
 * Purpose:
 *   Owns PN532 passive scans, per-tag cooldown, simulated EPC mapping, and package counters.
 *
 * Responsibilities:
 *   Preserve Wokwi UID mapping, duplicate suppression, scan contexts, and active counts.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "rfid.h"
#include "logging.h"
#include "telemetry.h"

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
  for (size_t i = 0; i < simulatedPackageCount; i++)
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
  for (size_t i = 0; i < simulatedPackageCount; i++)
  {
    simulatedPackages[i].onboard = false;
  }
}
