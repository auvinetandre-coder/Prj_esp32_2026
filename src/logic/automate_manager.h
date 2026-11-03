#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"
#include "rule_engine.h"

class AutomateManager {
public:
  enum AutomateState {
    BOOT,
    INIT,
    IDLE,
    AUTO_ROUTING,
    MANUAL_FORCE,
    SAFETY,
    ERROR,
    RECOVERY_REQUEST
  };

  enum Priority {
    PRIORITY_IDLE = 0,
    PRIORITY_AUTO_ROUTING = 1,
    PRIORITY_RULE_ENGINE = 2,
    PRIORITY_MANUAL_FORCE = 4,
    PRIORITY_RECOVERY_REQUEST = 5,
    PRIORITY_ERROR = 6,
    PRIORITY_SAFETY = 7
  };

  struct FinalCommand {
    bool enabled = false;
    String actuatorId = "";
    String command = "setPower";
    String mode = "";
    float commandPercent = 0.0f;
    String source = "NONE";
    String reason = "Idle";
    bool safetyLimited = false;
  };

  AutomateManager(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators, RuleEngine &rules)
      : config(config), state(state), actuators(actuators), rules(rules) {}

  void begin(uint32_t now = 0);
  void loop(uint32_t now);
  void requestRecovery(const String &reason);
  void clearRecoveryRequest();

  static const char *stateName(AutomateState value);
  static const char *priorityName(Priority value);

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  RuleEngine &rules;
  AutomateState currentState = BOOT;
  bool recoveryRequested = false;
  String recoveryReason = "";
  String heldManualActuatorId = "";
  String ruleActuatorIds[16];
  uint8_t ruleActuatorCount = 0;

  void transitionTo(AutomateState next, const String &reason, uint32_t now);
  FinalCommand arbitrate(uint32_t now, Priority &priority, AutomateState &nextState);
  FinalCommand manualCommand(JsonObject router);
  FinalCommand ruleCommand(const RuleEngine::RuleDecision &decision);
  void applyFinalCommand(const FinalCommand &command);
  bool errorDetected() const;
};
