#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"

class SolarRouter {
public:
  SolarRouter(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators)
      : config(config), state(state), actuators(actuators) {}
  void loop(uint32_t now);

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  float integral = 0;
  float previousError = 0;
};
