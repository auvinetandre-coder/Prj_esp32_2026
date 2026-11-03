#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../communication/espnow_protocol.h"
#include "ds18b20_manager.h"
#include "jsy_mk194t_manager.h"
#include "linky_tic_manager.h"

class SensorManager {
public:
  SensorManager(ConfigManager &config, RuntimeState &state) : config(config), state(state), ds18b20(config, state), jsy(config, state), tic(config, state) {}
  void begin();
  void loop(uint32_t now);
  float valueFor(const String &sensorId, const String &variable);
  struct PowerSample {
    float watts = NAN;
    uint32_t timestampMs = 0;
    String source;
    bool valid = false;
  };
  // Only a declared signed grid measurement may feed the PID, never arbitrary watts.
  PowerSample gridPowerSample(const String &sensorId, const String &variable, uint32_t now);
  // Resolve one configured JSY channel by usage, never by physical channel number.
  // Missing, stale or ambiguous assignments return NAN; a real zero stays valid.
  float jsyRoleValue(const String &role, const String &metric, const String &actuatorId = "");
  String detectedDs18b20Json();
  String ds18b20StatusJson();
  bool assignDs18b20(const String &sensorId, const String &address);
  bool reconfigureJsyTo19200();
  void reloadConfiguration();
  void refreshMqttProduction();
  bool receiveMqttProduction(const String &payload);
  void mqttProductionToJson(JsonObject out);
  void mqttProductionSensorToJson(JsonArray out);
  bool updateRemoteSensor(const String &sourceMac, const EspNowSensorPacket &packet);
  bool updateRemoteSensorFast(const String &sourceMac, const EspNowFastSensorPacket &packet);
  bool updateRemoteSensorDiscovery(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet);
  void updateRemoteDiagnostic(const String &sourceMac, const EspNowDiagnosticPacket &packet);
  void checkRemoteSensorTimeouts(uint32_t now);
  void setRemoteNodeLost(const String &sourceMac, bool lost);
  void remoteSensorsToJson(JsonArray out, bool configuredOnly = false);

  struct RemoteSensorRuntime {
    bool used = false;
    String sourceMac;
    uint8_t nodeId = 0;
    char nodeName[20]{};
    char firmwareVersion[12]{};
    char littlefsVersion[12]{};
    uint8_t sensorId = 0;
    char sensorName[ESPNOW_SENSOR_NAME_LEN]{};
    char sensorRole[ESPNOW_SENSOR_ROLE_LEN]{};
    uint8_t sensorType = SENSOR_UNKNOWN;
    SensorOrigin origin = SENSOR_ORIGIN_ESPNOW;
    bool ok = false;
    bool timedOut = false;
    bool nodeLost = false;
    uint32_t lastUpdateMs = 0;
    uint32_t lastDiscoveryMs = 0;
    uint32_t lastDiagnosticMs = 0;
    uint32_t lastSequence = 0;
    uint32_t lostPackets = 0;
    uint32_t receivedPackets = 0;
    uint8_t lastError = 0;
    int8_t rssiDbm = 0;
    uint8_t valueCount = 0;
    EspNowSensorValue values[16]{};
    uint32_t valueUpdatedMs[16]{};
  };

private:
  String mqttProductionTopic;
  float mqttProductionW = NAN;
  bool mqttProductionReceived = false;
  void applyMqttProduction();
  static const uint8_t MAX_REMOTE_SENSORS = 16;
  static const uint8_t MAX_REMOTE_SENSOR_VALUES = 16;
  static const uint32_t REMOTE_SENSOR_TIMEOUT_MS = 30000UL;
  ConfigManager &config;
  RuntimeState &state;
  DS18B20Manager ds18b20;
  JSYMK194TManager jsy;
  LinkyTICManager tic;
  bool jsyStarted = false;
  bool ticStarted = false;
  RemoteSensorRuntime remoteSensors[MAX_REMOTE_SENSORS]{};
  uint32_t lastRemoteTimeoutLogMs = 0;

  String configuredGridPowerSource();
  bool meterPinsConflict();
  void startMetersForCurrentSource();
  void applyGridPowerSource();
  RemoteSensorRuntime *findOrCreateRemoteSensor(uint8_t nodeId, uint8_t sensorId, const String &sourceMac);
  void applyRemoteSensorToState(const RemoteSensorRuntime &sensor);
  void applyRemoteSensorLostToState(const RemoteSensorRuntime &sensor);
  void applyRemoteValueToState(const RemoteSensorRuntime &sensor, const EspNowSensorValue &value);
  void mergeRemoteValue(RemoteSensorRuntime &sensor, const EspNowSensorValue &value, bool sample = true);
  JsonObject configuredEspNowSensorFor(const RemoteSensorRuntime &sensor);
  bool syncConfiguredRemoteSensorMetadata(const RemoteSensorRuntime &sensor);
  uint32_t getEspNowSensorTimeoutMs(uint8_t sensorType);
  uint32_t remoteTimeoutForSensor(const RemoteSensorRuntime &sensor);
  void updateRemotePacketLoss(RemoteSensorRuntime &sensor, uint32_t sequence);
  void setDefaultValueMetadata(EspNowSensorValue &value);
  String remoteMacSuffix(const String &mac) const;
  String remoteNodeLabel(const RemoteSensorRuntime &sensor) const;
};
