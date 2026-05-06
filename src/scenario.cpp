/**
 * =============================================================================
 * Serial Wokwi Scenario Commands
 * =============================================================================
 * Purpose:
 *   Processes serial-only commands used by deterministic Wokwi scan scenarios.
 *
 * Responsibilities:
 *   Handle STATUS, FORCE_SCAN, context arming, and scenario reset commands.
 *
 * Shared state:
 *   Uses declarations from firmware_state.h. Behavior is intentionally preserved
 *   from the original monolithic src/main.cpp refactor.
 * =============================================================================
 */
#include "scenario.h"
#include "logging.h"
#include "rfid.h"

void handleSerialScenarioCommands()
{
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (serialCommandBuffer.length() > 0)
      {
        processScenarioCommand(serialCommandBuffer);
        serialCommandBuffer = "";
      }
      continue;
    }

    if (serialCommandBuffer.length() < 96)
    {
      serialCommandBuffer += c;
    }
    else
    {
      serialCommandBuffer = "";
#if LOG_HUMAN
      Serial.println(F("[SCENARIO] Command too long - discarded"));
#endif
      logSimpleEvent("WARN", "scenario_command", "command_too_long");
    }
  }
}

void processScenarioCommand(String command)
{
  command.trim();
  command.toUpperCase();

  if (command.length() == 0)
    return;

  JsonDocument cmdLog;
  addLogBase(cmdLog, "INFO", "scenario_command");
  cmdLog["source"] = "serial";
  cmdLog["command"] = command;
  emitLog(cmdLog);

  if (command == "STATUS")
  {
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] STATUS requested"));
#endif
    logStateSnapshot("serial_status");
    return;
  }

  if (command == "FORCE_SCAN")
  {
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] FORCE_SCAN received - cooldown cleared and RFID scan requested"));
#endif
    rfidCooldownMap.clear();
    logSimpleEvent("INFO", "scenario_command", "force_scan");
    handleRfidScan();
    return;
  }

  if (!command.startsWith("SCENARIO "))
  {
#if LOG_HUMAN
    Serial.printf("[SCENARIO] Unknown serial command: %s\n", command.c_str());
#endif
    logSimpleEvent("WARN", "scenario_command", "unknown_serial_command");
    return;
  }

  String mode = command.substring(9);
  mode.trim();

  if (mode == "PICKUP")
  {
    strlcpy(scenarioScanContext, "pickup", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: pickup"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_pickup_armed");
  }
  else if (mode == "IN_TRANSIT")
  {
    strlcpy(scenarioScanContext, "in_transit", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: in_transit"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_in_transit_armed");
  }
  else if (mode == "DELIVERED")
  {
    strlcpy(scenarioScanContext, "delivered", sizeof(scenarioScanContext));
    scenarioContextArmed = true;
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Next RFID scan context armed: delivered"));
#endif
    logSimpleEvent("INFO", "scenario_command", "context_delivered_armed");
  }
  else if (mode == "RESET")
  {
    resetScenarioState();
#if LOG_HUMAN
    Serial.println(F("[SCENARIO] Scenario state reset"));
#endif
    logStateSnapshot("scenario_reset");
  }
  else
  {
#if LOG_HUMAN
    Serial.printf("[SCENARIO] Unsupported mode: %s\n", mode.c_str());
#endif
    logSimpleEvent("WARN", "scenario_command", "unsupported_mode");
  }
}
