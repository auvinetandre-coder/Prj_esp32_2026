#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class ConfigManager;

enum LoopPerfModule : uint8_t {
  PERF_SENSOR = 0,
  PERF_ACTUATOR,
  PERF_RULE_ENGINE,
  PERF_PID,
  PERF_ESPNOW,
  PERF_WEBUI,
  PERF_MQTT,
  PERF_SAFETY,
  PERF_WIFI,
  PERF_STATUS_LED,
  PERF_DISPLAY,
  PERF_SIMULATION,
  PERF_COUNT
};

struct LoopPerfStat {
  const char *name = "";
  uint32_t avgMs = 0;
  uint32_t maxMs = 0;
  uint32_t count = 0;
};

struct RuntimeState {
  uint8_t nodeId = 0;
  String deviceId;
  String moduleName;
  String networkMode = "BOOT";
  String localIp = "0.0.0.0";
  String stationIp = "0.0.0.0";
  String apIp = "0.0.0.0";
  String wifiSsid = "";
  int32_t rssi = 0;
  bool wifiConnected = false;
  bool ntpEnabled = true;
  bool ntpSynced = false;
  String ntpStatus = "NTP_WAIT_WIFI";
  uint32_t lastNtpSyncMs = 0;
  bool espNowReady = false;
  uint32_t lastEspNowSensorReceiveMs = 0;
  bool safetyTripped = false;
  String safetyLevel = "OK";
  String safetyReason = "";
  uint32_t watchdogSeenMs = 0;

  float gridPowerW = 0;
  float gridPowerRawW = NAN;
  float gridPowerFilteredW = NAN;
  String gridPowerSource = "JSY";
  float jsyGridPowerW = NAN;
  float ticGridPowerW = NAN;
  float gridVoltageV = NAN;
  float gridCurrentA = NAN;
  float gridPowerFactor = NAN;
  float gridFrequencyHz = NAN;
  String gridEnergyDirection = "unknown";
  float jsyImportEnergyWh1 = NAN;
  float jsyMeasuredPowerW1 = NAN;
  float jsyMeasuredPowerW2 = NAN;
  float jsyExportEnergyWh1 = NAN;
  float jsyImportEnergyWh2 = NAN;
  float jsyExportEnergyWh2 = NAN;
  uint32_t jsyPollingMs = 0;
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
  uint32_t jsyBaudrate = 0;
  String jsyLastError = "";
  uint16_t jsyTimeoutCount = 0;
  uint16_t jsyCrcErrorCount = 0;
  bool ticAvailable = false;
  String ticStatus = "TIC_NOT_CONFIGURED";
  float ticApparentPowerVA = NAN;
  float ticCurrentA = NAN;
  uint64_t ticEnergyWh = 0;
  String ticTariff = "";
  String ticPeriod = "";
  uint32_t lastTicReadMs = 0;
  uint32_t lastTicPowerReadMs = 0;
  uint16_t ticErrorCount = 0;
  float injectionW = 0;
  float consumptionW = 0;
  float productionW = 0;
  bool mqttProductionEnabled = false;
  bool mqttProductionAvailable = false;
  uint32_t lastMqttProductionMs = 0;
  uint32_t mqttProductionTimeoutMs = 900000;
  String mqttProductionStatus = "DISABLED";
  float surplusW = 0;
  float temperature1C = NAN;
  float temperature2C = NAN;
  float temperature3C = NAN;
  float ds18b20Temps[3] = {NAN, NAN, NAN};
  bool ds18b20Available[3] = {false, false, false};
  uint32_t ds18b20LastReadMs[3] = {0, 0, 0};
  uint16_t ds18b20ErrorCount[3] = {0, 0, 0};
  bool ds18b20CriticalMissing = false;
  String ds18b20CriticalMissingList = "";
  float ssr1PowerPct = 0;
  float ssr2PowerPct = 0;
  float robotDynPowerPct = 0;
  bool ssr1OutputOn = false;
  bool ssr2OutputOn = false;
  bool robotDynOutputOn = false;
  bool ssr1PinHigh = false;
  bool ssr2PinHigh = false;
  bool robotDynPinHigh = false;
  float outputPowerW = 0;
  float commandPercent = 0;
  String automateState = "BOOT";
  String previousAutomateState = "BOOT";
  String automateReason = "Boot";
  String activePriority = "IDLE";
  String commandSource = "NONE";
  String commandActuatorId = "";
  bool pidAllowed = false;
  bool routingAllowed = false;
  bool safetyActive = false;
  bool errorActive = false;
  uint32_t automateLastTransitionMs = 0;
  uint8_t ruleCount = 0;
  String activeRule = "";
  bool manualCommandPending = false;
  bool manualCommandPersistent = false;
  bool manualCommandSimulationMode = false;
  String manualCommandActuatorId = "";
  String manualCommand = "setPower";
  float manualCommandPercent = 0.0f;
  String manualCommandSource = "";
  String manualCommandReason = "";
  bool manualAllOffPending = false;
  float pidOutputPercent = 0;
  bool pidActive = false;
  bool pidEnabled = true;
  float pidErrorW = 0;
  float pidGridSetpointW = NAN;
  float pidMeasuredW = NAN, pidDeadbandW = 30;
  float pidKp = 0, pidKi = 0, pidKd = 0, pidRamp = 5, pidDrop = 100;
  String pidSensorSource;
  String pidStatus = "IDLE";
  String pidContextId;
  String systemMode = "AUTO";
  bool simulationMode = false;
  String simulationType = "manual";
  String simulationScenario = "normal";
  uint32_t simulationRemainingMs = 0;
  bool mqttEnabled = false;
  bool mqttConnected = false;
  String mqttStatus = "MQTT_DISABLED";
  uint32_t lastMqttPublishMs = 0;
  String lastActuatorCommandLog = "";
  bool littleFsOk = false;
  uint32_t loopDurationMs = 0;
  uint32_t maxLoopDurationMs = 0;
  uint32_t loopCount = 0;
  uint32_t apiStateBuildMs = 0;
  uint32_t apiStateJsonBytes = 0;
  uint32_t apiStateRequestCount = 0;
  uint32_t espNowProcessMs = 0;
  uint32_t webRequestCount = 0;
  uint32_t slowTaskThresholdMs = 50;
  uint32_t lastSlowTaskMs = 0;
  String lastSlowTask = "";
  String lastSlowModule = "";
  uint32_t lastSlowModuleMs = 0;
  LoopPerfStat loopPerf[PERF_COUNT] = {
    {"sensorManager.loop", 0, 0, 0},
    {"actuatorManager.loop", 0, 0, 0},
    {"ruleEngine.loop", 0, 0, 0},
    {"pid", 0, 0, 0},
    {"espNow.loop", 0, 0, 0},
    {"webUi.loop", 0, 0, 0},
    {"mqtt.loop", 0, 0, 0},
    {"safety.loop", 0, 0, 0},
    {"wifi.loop", 0, 0, 0},
    {"statusLed.loop", 0, 0, 0},
    {"display.loop", 0, 0, 0},
    {"simulation.loop", 0, 0, 0}
  };

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
  void performanceToJson(JsonObject out);
  void recordLoopPerf(LoopPerfModule module, uint32_t elapsedMs);
};
