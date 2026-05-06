#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
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

private:
  ConfigManager &config;
  RuntimeState &state;
  DS18B20Manager ds18b20;
  JSYMK194TManager jsy;
  LinkyTICManager tic;
  void applyGridPowerSource();
};
