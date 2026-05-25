#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"

class PIDController {
public:
  PIDController(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators)
      : config(config), state(state), actuators(actuators) {}

  void begin();
  void update(uint32_t now);
  float computeSurplusPercent(JsonObject action, uint32_t now);
  void reset();

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  float integral = 0.0f;
  float previousError = 0.0f;
  uint32_t lastUpdateMs = 0;
  float outputPercent = 0.0f;

  float rampLimit(float target, float maxRampPercentPerSecond, uint32_t elapsedMs);
  float computeAutoPercent(float heaterMaxW, uint32_t elapsedMs, JsonObject router);
};
