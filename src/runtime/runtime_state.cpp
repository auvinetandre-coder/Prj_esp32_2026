#include "runtime_state.h"
#include "../config/config_manager.h"
#include <WiFi.h>

void RuntimeState::begin(ConfigManager &config) {
  JsonObject device = config.device();
  role = roleFromString(device["role"] | "MASTER");
  moduleName = device["deviceName"] | device["name"] | "Routeur solaire";
  deviceId = WiFi.macAddress();
  isActiveMaster = role == ROLE_MASTER;
  masterAlive = role == ROLE_MASTER;
  activeMasterId = isActiveMaster ? deviceId : "";
  simulationMode = config.system()["simulationMode"] | false;
  simulationType = config.system()["simulation"]["mode"] | "manual";
  simulationScenario = config.system()["simulation"]["scenario"] | "normal";
  gridPowerSource = config.system()["router"]["gridPowerSource"] | "JSY";
  watchdogSeenMs = millis();
}

void RuntimeState::addLog(const String &line) {
  logs[logHead] = String(millis()) + " ms - " + line;
  logHead = (logHead + 1) % 24;
  logEvent("INFO", "EVENT", line, "Runtime");
}

void RuntimeState::logEvent(const String &level, const String &code, const String &message, const String &source) {
  eventTimestamps[eventHead] = millis();
  eventLevels[eventHead] = level;
  eventCodes[eventHead] = code;
  eventMessages[eventHead] = message;
  eventSources[eventHead] = source;
  eventHead = (eventHead + 1) % 32;
  Serial.print(F("["));
  Serial.print(level);
  Serial.print(F("] "));
  Serial.print(code);
  Serial.print(F(" - "));
  Serial.println(message);
}

void RuntimeState::clearEvents() {
  for (uint8_t i = 0; i < 32; i++) {
    eventTimestamps[i] = 0;
    eventLevels[i] = "";
    eventCodes[i] = "";
    eventMessages[i] = "";
    eventSources[i] = "";
  }
  eventHead = 0;
}

void RuntimeState::eventsToJson(JsonArray out) {
  for (uint8_t i = 0; i < 32; i++) {
    uint8_t idx = (eventHead + i) % 32;
    if (!eventTimestamps[idx] && !eventCodes[idx].length()) continue;
    JsonObject ev = out.add<JsonObject>();
    ev["timestampMs"] = eventTimestamps[idx];
    ev["level"] = eventLevels[idx];
    ev["code"] = eventCodes[idx];
    ev["message"] = eventMessages[idx];
    ev["source"] = eventSources[idx];
  }
}

void RuntimeState::toJson(JsonObject out) {
  toJson(out, true);
}

void RuntimeState::toJson(JsonObject out, bool includeLogs) {
  out["deviceId"] = deviceId;
  out["moduleName"] = moduleName;
  out["role"] = roleToString(role);
  out["networkMode"] = networkMode;
  out["localIp"] = localIp;
  out["stationIp"] = stationIp;
  out["apIp"] = apIp;
  out["wifiSsid"] = wifiSsid;
  out["rssi"] = rssi;
  out["wifiConnected"] = wifiConnected;
  out["espNowReady"] = espNowReady;
  out["isActiveMaster"] = isActiveMaster;
  out["masterAlive"] = masterAlive;
  out["activeMasterId"] = activeMasterId;
  out["epoch"] = redundancyEpoch;
  out["redundancyState"] = redundancyState;
  out["splitBrainDetected"] = splitBrainDetected;
  out["lastMasterHeartbeatAgeMs"] = lastMasterHeartbeatMs ? millis() - lastMasterHeartbeatMs : 4294967295UL;
  out["safetyTripped"] = safetyTripped;
  out["safetyLevel"] = safetyLevel;
  out["safetyReason"] = safetyReason;
  out["uptime"] = millis() / 1000;
  out["heapFree"] = ESP.getFreeHeap();
  out["gridPowerW"] = gridPowerW;
  out["gridPowerSource"] = gridPowerSource;
  out["jsyGridPowerW"] = jsyGridPowerW;
  out["ticGridPowerW"] = ticGridPowerW;
  out["gridVoltageV"] = gridVoltageV;
  out["gridCurrentA"] = gridCurrentA;
  out["gridPowerFactor"] = gridPowerFactor;
  out["gridFrequencyHz"] = gridFrequencyHz;
  out["gridEnergyDirection"] = gridEnergyDirection;
  out["voltageV1"] = voltageV1;
  out["currentA1"] = currentA1;
  out["activePowerW1"] = activePowerW1;
  out["powerFactor1"] = powerFactor1;
  out["voltageV2"] = voltageV2;
  out["currentA2"] = currentA2;
  out["activePowerW2"] = activePowerW2;
  out["powerFactor2"] = powerFactor2;
  out["energyDirection1"] = energyDirection1;
  out["energyDirection2"] = energyDirection2;
  out["jsyOnline"] = jsyOnline;
  out["lastJsyReadMs"] = lastJsyReadMs;
  out["ticAvailable"] = ticAvailable;
  out["ticStatus"] = ticStatus;
  out["ticApparentPowerVA"] = ticApparentPowerVA;
  out["ticCurrentA"] = ticCurrentA;
  out["ticEnergyWh"] = static_cast<uint32_t>(ticEnergyWh);
  out["ticTariff"] = ticTariff;
  out["ticPeriod"] = ticPeriod;
  out["lastTicReadMs"] = lastTicReadMs;
  out["ticErrorCount"] = ticErrorCount;
  out["injectionW"] = injectionW;
  out["consumptionW"] = consumptionW;
  out["productionW"] = productionW;
  out["surplusW"] = surplusW;
  out["tankTopC"] = tankTopC;
  out["tankMiddleC"] = tankMiddleC;
  out["tankBottomC"] = tankBottomC;
  JsonArray ds = out["ds18b20"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; i++) {
    JsonObject s = ds.add<JsonObject>();
    s["id"] = String("sonde") + String(i + 1);
    s["temperatureC"] = ds18b20Temps[i];
    s["available"] = ds18b20Available[i];
    s["lastReadMs"] = ds18b20LastReadMs[i];
    s["errorCount"] = ds18b20ErrorCount[i];
  }
  out["ds18b20CriticalMissing"] = ds18b20CriticalMissing;
  out["ds18b20CriticalMissingList"] = ds18b20CriticalMissingList;
  out["ssr1PowerPct"] = ssr1PowerPct;
  out["ssr2PowerPct"] = ssr2PowerPct;
  out["robotDynPowerPct"] = robotDynPowerPct;
  out["systemMode"] = systemMode;
  out["simulationMode"] = simulationMode;
  out["simulationType"] = simulationType;
  out["simulationScenario"] = simulationScenario;
  out["simulationRemainingMs"] = simulationRemainingMs;
  out["lastActuatorCommandLog"] = lastActuatorCommandLog;
  out["littleFsOk"] = littleFsOk;
  if (!includeLogs) return;
  JsonArray arr = out["logs"].to<JsonArray>();
  for (uint8_t i = 0; i < 24; i++) {
    uint8_t idx = (logHead + i) % 24;
    if (logs[idx].length()) arr.add(logs[idx]);
  }
  JsonArray events = out["events"].to<JsonArray>();
  eventsToJson(events);
}

DeviceRole RuntimeState::roleFromString(const String &value) {
  if (value == "BACKUP") return ROLE_BACKUP;
  if (value == "NODE_SENSOR") return ROLE_NODE_SENSOR;
  if (value == "NODE_ACTUATOR") return ROLE_NODE_ACTUATOR;
  if (value == "NODE_MIXED") return ROLE_NODE_MIXED;
  return ROLE_MASTER;
}

const char *RuntimeState::roleToString(DeviceRole role) {
  switch (role) {
    case ROLE_BACKUP: return "BACKUP";
    case ROLE_NODE_SENSOR: return "NODE_SENSOR";
    case ROLE_NODE_ACTUATOR: return "NODE_ACTUATOR";
    case ROLE_NODE_MIXED: return "NODE_MIXED";
    default: return "MASTER";
  }
}
