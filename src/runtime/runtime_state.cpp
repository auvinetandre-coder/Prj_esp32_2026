#include "runtime_state.h"
#include "../config/config_manager.h"
#include <WiFi.h>

void RuntimeState::begin(ConfigManager &config) {
  JsonObject device = config.device();
  role = roleFromString(device["role"] | "MASTER");
  moduleName = device["deviceName"] | device["name"] | "Routeur solaire";
  deviceId = WiFi.macAddress();
  uint32_t configuredNodeId = device["nodeId"] | 0UL;
  if (configuredNodeId == 0 || configuredNodeId > 254) {
    uint8_t mac[6]{};
    WiFi.macAddress(mac);
    uint16_t macHash = 0;
    for (uint8_t byte : mac) macHash = static_cast<uint16_t>((macHash * 33U) ^ byte);
    configuredNodeId = (macHash % 254U) + 1U;
  }
  nodeId = static_cast<uint8_t>(configuredNodeId);
  isActiveMaster = role == ROLE_MASTER;
  masterAlive = role == ROLE_MASTER;
  activeMasterId = isActiveMaster ? deviceId : "";
  simulationMode = config.system()["simulation"]["enabled"] | config.system()["simulationMode"] | false;
  simulationType = config.system()["simulation"]["mode"] | "manual";
  simulationScenario = config.system()["simulation"]["scenario"] | "normal";
  ntpEnabled = config.system()["ntp"]["enabled"] | true;
  ntpStatus = ntpEnabled ? "NTP_WAIT_WIFI" : "NTP_DISABLED";
  mqttEnabled = config.system()["mqtt"]["enabled"] | false;
  mqttStatus = mqttEnabled ? "MQTT_WAIT_WIFI" : "MQTT_DISABLED";
  gridPowerSource = config.system()["router"]["gridPowerSource"] | "JSY";
  pidEnabled = config.system()["router"]["pidEnabled"] | true;
  systemMode = config.system()["router"]["mode"] | "AUTO";
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
  out["nodeId"] = nodeId;
  out["moduleName"] = moduleName;
  out["role"] = roleToString(role);
  out["redundancyRole"] = redundancyRoleToString(role);
  out["functionalRole"] = functionalRoleToString(role);
  out["networkMode"] = networkMode;
  out["localIp"] = localIp;
  out["stationIp"] = stationIp;
  out["apIp"] = apIp;
  out["wifiSsid"] = wifiSsid;
  out["rssi"] = rssi;
  out["wifiConnected"] = wifiConnected;
  out["ntpEnabled"] = ntpEnabled;
  out["ntpSynced"] = ntpSynced;
  out["ntpStatus"] = ntpStatus;
  out["lastNtpSyncAgeMs"] = lastNtpSyncMs ? millis() - lastNtpSyncMs : 4294967295UL;
  out["espNowReady"] = espNowReady;
  out["lastEspNowSensorReceiveMs"] = lastEspNowSensorReceiveMs;
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
  out["gridPowerRawW"] = gridPowerRawW;
  out["gridPowerFilteredW"] = gridPowerFilteredW;
  out["gridPowerSource"] = gridPowerSource;
  out["jsyGridPowerW"] = jsyGridPowerW;
  out["ticGridPowerW"] = ticGridPowerW;
  out["gridVoltageV"] = gridVoltageV;
  out["gridCurrentA"] = gridCurrentA;
  out["gridPowerFactor"] = gridPowerFactor;
  out["gridFrequencyHz"] = gridFrequencyHz;
  out["gridEnergyDirection"] = gridEnergyDirection;
  out["jsyImportEnergyWh1"] = jsyImportEnergyWh1;
  out["jsyExportEnergyWh1"] = jsyExportEnergyWh1;
  out["jsyImportEnergyWh2"] = jsyImportEnergyWh2;
  out["jsyExportEnergyWh2"] = jsyExportEnergyWh2;
  out["jsyPollingMs"] = jsyPollingMs;
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
  out["jsyBaudrate"] = jsyBaudrate;
  out["jsyLastError"] = jsyLastError;
  out["jsyTimeoutCount"] = jsyTimeoutCount;
  out["jsyCrcErrorCount"] = jsyCrcErrorCount;
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
  out["batteryVoltageV"] = batteryVoltageV;
  out["batteryCurrentA"] = batteryCurrentA;
  out["batteryPowerW"] = batteryPowerW;
  out["batterySocPct"] = batterySocPct;
  out["batteryOnline"] = batteryOnline;
  out["lastBatteryReadMs"] = lastBatteryReadMs;
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
  out["ssr1OutputOn"] = ssr1OutputOn;
  out["ssr2OutputOn"] = ssr2OutputOn;
  out["robotDynOutputOn"] = robotDynOutputOn;
  out["ssr1PinHigh"] = ssr1PinHigh;
  out["ssr2PinHigh"] = ssr2PinHigh;
  out["robotDynPinHigh"] = robotDynPinHigh;
  out["heaterPowerW"] = heaterPowerW;
  out["commandPercent"] = commandPercent;
  out["automateState"] = automateState;
  out["previousAutomateState"] = previousAutomateState;
  out["automateReason"] = automateReason;
  out["activePriority"] = activePriority;
  out["commandSource"] = commandSource;
  out["pidAllowed"] = pidAllowed;
  out["routingAllowed"] = routingAllowed;
  out["safetyActive"] = safetyActive;
  out["errorActive"] = errorActive;
  out["automateLastTransitionMs"] = automateLastTransitionMs;
  out["ruleCount"] = ruleCount;
  out["activeRule"] = activeRule;
  out["manualCommandPending"] = manualCommandPending;
  out["manualCommandSource"] = manualCommandSource;
  out["manualCommandReason"] = manualCommandReason;
  out["manualAllOffPending"] = manualAllOffPending;
  JsonObject automate = out["automate"].to<JsonObject>();
  automate["state"] = automateState;
  automate["previousState"] = previousAutomateState;
  automate["reason"] = automateReason;
  automate["activePriority"] = activePriority;
  automate["commandSource"] = commandSource;
  automate["commandPercent"] = commandPercent;
  automate["pidAllowed"] = pidAllowed;
  automate["routingAllowed"] = routingAllowed;
  automate["safetyActive"] = safetyActive;
  automate["errorActive"] = errorActive;
  automate["lastTransitionMs"] = automateLastTransitionMs;
  automate["ruleCount"] = ruleCount;
  automate["activeRule"] = activeRule;
  automate["manualCommandPending"] = manualCommandPending;
  automate["manualCommandSource"] = manualCommandSource;
  automate["manualCommandReason"] = manualCommandReason;
  automate["manualAllOffPending"] = manualAllOffPending;
  out["pidOutputPercent"] = pidOutputPercent;
  out["pidEnabled"] = pidEnabled;
  out["pidErrorW"] = pidErrorW;
  out["pidStatus"] = pidStatus;
  out["systemMode"] = systemMode;
  out["simulationMode"] = simulationMode;
  out["simulationType"] = simulationType;
  out["simulationScenario"] = simulationScenario;
  out["simulationRemainingMs"] = simulationRemainingMs;
  out["mqttEnabled"] = mqttEnabled;
  out["mqttConnected"] = mqttConnected;
  out["mqttStatus"] = mqttStatus;
  out["lastMqttPublishAgeMs"] = lastMqttPublishMs ? millis() - lastMqttPublishMs : 4294967295UL;
  out["lastActuatorCommandLog"] = lastActuatorCommandLog;
  out["littleFsOk"] = littleFsOk;
  performanceToJson(out["performance"].to<JsonObject>());
  if (!includeLogs) return;
  JsonArray arr = out["logs"].to<JsonArray>();
  for (uint8_t i = 0; i < 24; i++) {
    uint8_t idx = (logHead + i) % 24;
    if (logs[idx].length()) arr.add(logs[idx]);
  }
  JsonArray events = out["events"].to<JsonArray>();
  eventsToJson(events);
}

void RuntimeState::performanceToJson(JsonObject out) {
  out["loopDurationMs"] = loopDurationMs;
  out["maxLoopDurationMs"] = maxLoopDurationMs;
  out["loopCount"] = loopCount;
  out["apiStateBuildMs"] = apiStateBuildMs;
  out["apiStateJsonBytes"] = apiStateJsonBytes;
  out["apiStateRequestCount"] = apiStateRequestCount;
  out["espNowProcessMs"] = espNowProcessMs;
  out["webRequestCount"] = webRequestCount;
  out["heapFree"] = ESP.getFreeHeap();
  out["heapMin"] = ESP.getMinFreeHeap();
  out["heapMaxAlloc"] = ESP.getMaxAllocHeap();
  out["lastSlowTask"] = lastSlowTask;
  out["lastSlowTaskMs"] = lastSlowTaskMs;
  out["lastSlowModule"] = lastSlowModule;
  out["lastSlowModuleMs"] = lastSlowModuleMs;
  out["slowTaskThresholdMs"] = slowTaskThresholdMs;
  JsonArray modules = out["modules"].to<JsonArray>();
  for (uint8_t i = 0; i < PERF_COUNT; i++) {
    JsonObject item = modules.add<JsonObject>();
    item["name"] = loopPerf[i].name;
    item["avgMs"] = loopPerf[i].avgMs;
    item["maxMs"] = loopPerf[i].maxMs;
    item["count"] = loopPerf[i].count;
  }
}

void RuntimeState::recordLoopPerf(LoopPerfModule module, uint32_t elapsedMs) {
  if (module >= PERF_COUNT) return;
  LoopPerfStat &stat = loopPerf[module];
  stat.count++;
  stat.avgMs = stat.avgMs == 0 ? elapsedMs : ((stat.avgMs * 7UL) + elapsedMs) / 8UL;
  if (elapsedMs > stat.maxMs) stat.maxMs = elapsedMs;
  if (elapsedMs >= slowTaskThresholdMs) {
    lastSlowModule = stat.name;
    lastSlowModuleMs = elapsedMs;
  }
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

const char *RuntimeState::redundancyRoleToString(DeviceRole role) {
  if (role == ROLE_MASTER) return "MASTER";
  if (role == ROLE_BACKUP) return "BACKUP";
  return "STANDALONE";
}

const char *RuntimeState::functionalRoleToString(DeviceRole role) {
  if (role == ROLE_NODE_SENSOR) return "SENSOR";
  if (role == ROLE_NODE_ACTUATOR) return "ACTUATOR";
  return "MIXED";
}
