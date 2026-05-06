#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

class SimulationManager {
public:
  SimulationManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}

  void begin();
  void loop(uint32_t now);
  bool enabled();
  void enable();
  void disable();
  void setMode(const String &mode);
  void setScenario(const String &scenario);
  void randomize();
  void applyScenario(const String &scenario);
  bool setValuesFromJson(const String &json, String &error);
  void toJson(JsonObject out);

private:
  ConfigManager &config;
  RuntimeState &state;
  uint32_t lastUpdateMs = 0;
  uint32_t enabledAtMs = 0;
  uint32_t expiresAtMs = 0;
  bool runtimeEnabled = false;
  static const uint32_t SIMULATION_TIMEOUT_MS = 300000UL;

  JsonObject simulationConfig();
  uint32_t updateIntervalMs();
  void markEnabled();
  float randomFloat(float minValue, float maxValue);
  void applyPower(float gridPowerW);
  void applyNormalAvailability();
  void applyTemperatures(float t1, float t2, float t3, bool a1 = true, bool a2 = true, bool a3 = true);
  void applyJsy(bool available, float gridPowerW, float voltageV, float currentA, float powerFactor, float frequencyHz, float activePowerW2 = NAN, float currentA2 = NAN);
  void applyTic(bool available, float apparentPowerVA, float currentA, const String &tariff);
  void saveConfig();
};
