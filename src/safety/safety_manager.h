#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"

enum SafetyLevel {
  SAFETY_OK,
  SAFETY_WARNING,
  SAFETY_DEGRADED,
  SAFETY_CRITICAL
};

class SafetyManager {
public:
  SafetyManager(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators)
      : config(config), state(state), actuators(actuators) {}

  void begin();
  void loop();
  void loop(uint32_t now);
  void evaluate(uint32_t now);
  void softwareWatchdog(uint32_t now);
  String getSafetyLevel() const { return levelText(level); }
  String getReason() const { return reason; }
  bool isCritical() const { return level == SAFETY_CRITICAL; }
  void clearManualStop();
  void triggerManualStop();
  void printStatus();

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  SafetyLevel level = SAFETY_OK;
  String reason = "";
  bool manualStop = false;
  String lastLoggedDecision = "";
  uint32_t lastCriticalCutMs = 0;

  void setLevel(SafetyLevel next, const String &cause, const String &details, uint32_t now);
  void applyCriticalCut(uint32_t now);
  bool topTemperatureHigh(float safetyC);
  bool topSensorMissing();
  bool criticalDs18b20Missing();
  bool jsyTimedOut(uint32_t now, uint32_t timeoutMs);
  bool ticTimedOut(uint32_t now, uint32_t timeoutMs);
  bool jsyConfigured();
  bool ticConfigured();
  bool doubleMasterRisk();
  bool masterLost(uint32_t now, uint32_t takeoverTimeoutMs);
  bool configError();
  static const char *levelText(SafetyLevel value);
};
