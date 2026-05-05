#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class ConfigManager;

enum DeviceRole {
  ROLE_MASTER,
  ROLE_BACKUP,
  ROLE_NODE_SENSOR,
  ROLE_NODE_ACTUATOR,
  ROLE_NODE_MIXED
};

struct RuntimeState {
  DeviceRole role = ROLE_MASTER;
  String deviceId;
  String moduleName;
  String networkMode = "BOOT";
  String localIp = "0.0.0.0";
  String stationIp = "0.0.0.0";
  String apIp = "0.0.0.0";
  String wifiSsid = "";
  int32_t rssi = 0;
  bool wifiConnected = false;
  bool espNowReady = false;
  bool isActiveMaster = false;
  bool masterAlive = false;
  String activeMasterId = "";
  uint32_t redundancyEpoch = 0;
  String redundancyState = "PASSIVE";
  bool splitBrainDetected = false;
  bool safetyTripped = false;
  String safetyLevel = "OK";
  String safetyReason = "";
  uint32_t lastMasterHeartbeatMs = 0;
  uint32_t watchdogSeenMs = 0;

  float gridPowerW = 0;
  String gridPowerSource = "JSY";
  float jsyGridPowerW = NAN;
  float ticGridPowerW = NAN;
  float gridVoltageV = NAN;
  float gridCurrentA = NAN;
  float gridPowerFactor = NAN;
  float gridFrequencyHz = NAN;
  String gridEnergyDirection = "unknown";
  float voltageV1 = NAN;
  float currentA1 = NAN;
  float activePowerW1 = NAN;
  float powerFactor1 = NAN;
  float voltageV2 = NAN;
  float currentA2 = NAN;
  float activePowerW2 = NAN;
  float powerFactor2 = NAN;
  String energyDirection1 = "unknown";
  String energyDirection2 = "unknown";
  bool jsyOnline = false;
  uint32_t lastJsyReadMs = 0;
  bool ticAvailable = false;
  String ticStatus = "TIC_NOT_CONFIGURED";
  float ticApparentPowerVA = NAN;
  float ticCurrentA = NAN;
  uint64_t ticEnergyWh = 0;
  String ticTariff = "";
  String ticPeriod = "";
  uint32_t lastTicReadMs = 0;
  uint16_t ticErrorCount = 0;
  float injectionW = 0;
  float consumptionW = 0;
  float productionW = 0;
  float surplusW = 0;
  float tankTopC = NAN;
  float tankMiddleC = NAN;
  float tankBottomC = NAN;
  float ds18b20Temps[3] = {NAN, NAN, NAN};
  bool ds18b20Available[3] = {false, false, false};
  uint32_t ds18b20LastReadMs[3] = {0, 0, 0};
  uint16_t ds18b20ErrorCount[3] = {0, 0, 0};
  bool ds18b20CriticalMissing = false;
  String ds18b20CriticalMissingList = "";
  float ssr1PowerPct = 0;
  float ssr2PowerPct = 0;
  float robotDynPowerPct = 0;
  String systemMode = "AUTO";
  bool simulationMode = false;
  String simulationType = "manual";
  String simulationScenario = "normal";
  uint32_t simulationRemainingMs = 0;
  String lastActuatorCommandLog = "";
  bool littleFsOk = false;

  String logs[24];
  uint8_t logHead = 0;
  String eventLevels[32];
  String eventCodes[32];
  String eventMessages[32];
  String eventSources[32];
  uint32_t eventTimestamps[32] = {0};
  uint8_t eventHead = 0;

  void begin(ConfigManager &config);
  void addLog(const String &line);
  void logEvent(const String &level, const String &code, const String &message, const String &source);
  void clearEvents();
  void eventsToJson(JsonArray out);
  void toJson(JsonObject out);
  void toJson(JsonObject out, bool includeLogs);
  static DeviceRole roleFromString(const String &value);
  static const char *roleToString(DeviceRole role);
};
