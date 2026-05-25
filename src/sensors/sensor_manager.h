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
  String detectedDs18b20Json();
  String ds18b20StatusJson();
  bool assignDs18b20(const String &sensorId, const String &address);
  void reloadConfiguration();
  bool updateRemoteSensor(const String &sourceMac, const EspNowSensorPacket &packet);
  bool updateRemoteSensorFast(const String &sourceMac, const EspNowFastSensorPacket &packet);
  bool updateRemoteSensorDiscovery(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet);
  void updateRemoteDiagnostic(const String &sourceMac, const EspNowDiagnosticPacket &packet);
  void checkRemoteSensorTimeouts(uint32_t now);
  void remoteSensorsToJson(JsonArray out);

  struct RemoteSensorRuntime {
    bool used = false;
    String sourceMac;
    uint8_t nodeId = 0;
    char nodeName[20]{};
    uint8_t sensorId = 0;
    char sensorName[ESPNOW_SENSOR_NAME_LEN]{};
    char sensorRole[ESPNOW_SENSOR_ROLE_LEN]{};
    uint8_t sensorType = SENSOR_UNKNOWN;
    SensorOrigin origin = SENSOR_ORIGIN_ESPNOW;
    bool ok = false;
    bool timedOut = false;
    uint32_t lastUpdateMs = 0;
    uint32_t lastDiscoveryMs = 0;
    uint32_t lastDiagnosticMs = 0;
    uint32_t lastSequence = 0;
    uint32_t lostPackets = 0;
    uint32_t receivedPackets = 0;
    uint8_t lastError = 0;
    uint8_t valueCount = 0;
    EspNowSensorValue values[ESPNOW_MAX_SENSOR_VALUES]{};
  };

private:
  static const uint8_t MAX_REMOTE_SENSORS = 16;
  static const uint32_t REMOTE_SENSOR_TIMEOUT_MS = 5000UL;
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
  void applyRemoteValueToState(const RemoteSensorRuntime &sensor, const EspNowSensorValue &value);
  JsonObject configuredEspNowSensorFor(const RemoteSensorRuntime &sensor);
  uint32_t remoteTimeoutForType(uint8_t sensorType);
  void updateRemotePacketLoss(RemoteSensorRuntime &sensor, uint32_t sequence);
  void setDefaultValueMetadata(EspNowSensorValue &value);
};
