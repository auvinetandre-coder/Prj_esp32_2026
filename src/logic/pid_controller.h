#pragma once
#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"
#include "../sensors/sensor_manager.h"
#include "positional_pid.h"

class PIDController {
public:
  PIDController(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators)
      : config(config), state(state), actuators(actuators) {}
  void begin();
  void reset();
  void beginCycle();
  void endCycle();
  float compute(JsonObject action, const SensorManager::PowerSample &sample, uint32_t now, const String &context);
  void applied(const String &id, float percent);
  void toJson(JsonArray out);
  static PIDSettings settings(JsonObject action);

private:
  struct Instance {
    String id, context, signature, source, status, regulation;
    PIDState pid;
    PIDSettings settings;
    float rawMeasuredW = NAN;
    uint64_t resetToken = 0;
    bool used = false;
  };
  static const uint8_t MAX_OUTPUTS = 16;
  Instance instances[MAX_OUTPUTS];
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  Instance *instanceFor(const String &id);
  void publish(const Instance &instance);
};
