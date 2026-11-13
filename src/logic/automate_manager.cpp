#include "automate_manager.h"
#include <math.h>

void AutomateManager::begin(uint32_t now) {
  currentState = BOOT;
  state.automateState = stateName(BOOT);
  state.previousAutomateState = stateName(BOOT);
  state.automateReason = "Boot";
  state.automateLastTransitionMs = now ? now : millis();
  transitionTo(INIT, "Initialisation modules", state.automateLastTransitionMs);
  transitionTo(IDLE, "Systeme pret", state.automateLastTransitionMs);
  state.addLog("AutomateManager pret");
}

void AutomateManager::loop(uint32_t now) {
  rules.beginPIDCycle();
  Priority priority = PRIORITY_IDLE;
  AutomateState nextState = IDLE;
  FinalCommand command = arbitrate(now, priority, nextState);
  transitionTo(nextState, command.reason, now);

  state.activePriority = priorityName(priority);
  state.commandSource = command.source;
  state.commandActuatorId = command.actuatorId;
  state.commandPercent = command.commandPercent;
  state.pidAllowed = priority == PRIORITY_RULE_ENGINE || priority == PRIORITY_AUTO_ROUTING;
  state.routingAllowed = state.pidAllowed && !state.safetyActive && !state.errorActive;

  if (heldManualActuatorId.length() &&
      (priority != PRIORITY_MANUAL_FORCE || !state.manualCommandPersistent || command.actuatorId != heldManualActuatorId)) {
    if (!actuators.hasEspNowCommand(heldManualActuatorId)) actuators.command(heldManualActuatorId, "stop", 0.0f);
    heldManualActuatorId = "";
  }
  if (priority == PRIORITY_MANUAL_FORCE && state.manualCommandPersistent) heldManualActuatorId = command.actuatorId;
  // Release every output no longer selected, including deleted/disabled rules.
  for (uint8_t n = 0; n < ruleActuatorCount; ++n) {
    bool retained = false;
    if (priority == PRIORITY_RULE_ENGINE) {
      for (uint8_t k = 0; k < rules.decisionCount(); ++k)
        if (ruleActuatorIds[n] == rules.selectedDecision(k).actuatorId) retained = true;
    }
    if (!retained && !actuators.hasEspNowCommand(ruleActuatorIds[n])) actuators.command(ruleActuatorIds[n], "stop", 0);
  }
  ruleActuatorCount = 0;
  if (priority == PRIORITY_RULE_ENGINE) {
    for (uint8_t n = 0; n < rules.decisionCount(); ++n) {
      auto &selected = rules.selectedDecision(n);
      ruleActuatorIds[ruleActuatorCount++] = selected.actuatorId;
      if (actuators.hasEspNowCommand(selected.actuatorId)) continue;
      rules.resolvePID(selected, now);
      const FinalCommand effective = ruleCommand(selected);
      applyFinalCommand(effective);
      rules.pidApplied(selected.actuatorId);
      state.commandPercent = actuators.acceptedPercent(effective.actuatorId);
      state.commandActuatorId = effective.actuatorId;
    }
  } else applyFinalCommand(command);
  rules.endPIDCycle();
}

void AutomateManager::requestRecovery(const String &reason) {
  recoveryRequested = true;
  recoveryReason = reason.length() ? reason : "Recovery requested";
}

void AutomateManager::clearRecoveryRequest() {
  recoveryRequested = false;
  recoveryReason = "";
}

AutomateManager::FinalCommand AutomateManager::arbitrate(uint32_t now, Priority &priority, AutomateState &nextState) {
  (void)now;
  FinalCommand command;
  command.actuatorId = config.routingActuatorId();
  state.safetyActive = state.safetyTripped || state.safetyLevel == "CRITICAL";
  state.errorActive = errorDetected();

  if (state.manualCommandPersistent && (state.safetyActive || state.errorActive || recoveryRequested)) {
    state.manualCommandPending = false;
    state.manualCommandPersistent = false;
  }

  if (state.safetyActive) {
    priority = PRIORITY_SAFETY;
    nextState = SAFETY;
    command.enabled = true;
    command.commandPercent = 0.0f;
    command.source = "SAFETY";
    command.reason = state.safetyReason.length() ? state.safetyReason : "Safety active";
    command.safetyLimited = true;
    return command;
  }

  if (state.errorActive) {
    priority = PRIORITY_ERROR;
    nextState = ERROR;
    command.enabled = true;
    command.commandPercent = 0.0f;
    command.command = "allOff";
    command.source = "ERROR";
    command.reason = "Erreur systeme active";
    return command;
  }

  if (recoveryRequested) {
    priority = PRIORITY_RECOVERY_REQUEST;
    nextState = RECOVERY_REQUEST;
    command.enabled = true;
    command.commandPercent = 0.0f;
    command.command = "allOff";
    command.source = "RECOVERY";
    command.reason = recoveryReason;
    return command;
  }

  JsonObject router = config.system()["router"];
  String mode = router["mode"] | "AUTO";
  mode.toUpperCase();
  state.systemMode = mode;

  if (mode == "OFF") {
    state.manualCommandPending = false; state.manualCommandPersistent = false;
    priority = PRIORITY_IDLE; nextState = IDLE;
    command.enabled = true; command.command = "allOff";
    command.source = "IDLE"; command.reason = "Mode OFF";
    return command;
  }


  if (state.manualAllOffPending) {
    priority = PRIORITY_MANUAL_FORCE;
    nextState = MANUAL_FORCE;
    command.enabled = true;
    command.actuatorId = "*";
    command.command = "allOff";
    command.commandPercent = 0.0f;
    command.source = "MANUAL";
    command.reason = "Arret manuel toutes sorties";
    state.manualAllOffPending = false;
    state.manualCommandPending = false;
    state.manualCommandPersistent = false;
    return command;
  }

  if (state.manualCommandPending) {
    bool available = false;
    for (JsonObject actuator : config.actuators()) {
      if (state.manualCommandActuatorId == (actuator["id"] | "") && (actuator["enabled"] | false)) available = true;
    }
    if (!available || !isfinite(state.manualCommandPercent)) {
      state.manualCommandPercent = 0.0f;
      state.manualCommand = "stop";
      state.manualCommandPersistent = false;
    }
    priority = PRIORITY_MANUAL_FORCE;
    nextState = MANUAL_FORCE;
    command.enabled = true;
    command.actuatorId = state.manualCommandActuatorId;
    command.command = state.manualCommand.length() ? state.manualCommand : "setPower";
    command.commandPercent = constrain(state.manualCommandPercent, 0.0f, 100.0f);
    command.source = state.manualCommandSource.length() ? state.manualCommandSource : "MANUAL";
    command.reason = state.manualCommandReason.length() ? state.manualCommandReason : "Commande manuelle";
    state.manualCommandPending = state.manualCommandPersistent;
    return command;
  }

  if (mode == "FORCED" || mode == "FORCE" || mode == "FORCE_") {
    priority = PRIORITY_MANUAL_FORCE;
    nextState = MANUAL_FORCE;
    return manualCommand(router);
  }

  const RuleEngine::RuleDecision &decision = rules.decision();
  state.ruleCount = rules.ruleCount();
  state.activeRule = decision.ruleName;
  if (decision.hasCommand) {
    priority = PRIORITY_RULE_ENGINE;
    nextState = decision.requestedState.length() ? AUTO_ROUTING : AUTO_ROUTING;
    return ruleCommand(decision);
  }

  if (mode == "AUTO" && state.pidEnabled) {
    priority = PRIORITY_AUTO_ROUTING;
    nextState = AUTO_ROUTING;
    command.enabled = true;
    command.commandPercent = 0.0f;
    command.source = "AUTO_ROUTING";
    command.reason = "Aucune regle active";
    return command;
  }

  priority = PRIORITY_IDLE;
  nextState = IDLE;
  command.enabled = true;
  command.commandPercent = 0.0f;
  command.source = "IDLE";
  command.reason = mode == "OFF" ? "Mode OFF" : "Idle";
  return command;
}

AutomateManager::FinalCommand AutomateManager::manualCommand(JsonObject router) {
  FinalCommand command;
  command.enabled = true;
  command.actuatorId = router["forcedActuatorId"] | router["actuatorId"] | "";
  command.command = "setPower";
  command.commandPercent = constrain(router["forcedPercent"] | 0.0f, 0.0f, 100.0f);
  command.source = "MANUAL";
  command.reason = "Forcage manuel";
  return command;
}

AutomateManager::FinalCommand AutomateManager::ruleCommand(const RuleEngine::RuleDecision &decision) {
  FinalCommand command;
  command.enabled = true;
  command.actuatorId = decision.actuatorId;
  command.command = decision.command.length() ? decision.command : "setPower";
  command.mode = decision.mode;
  command.commandPercent = isfinite(decision.commandPercent) ? constrain(decision.commandPercent, 0.0f, 100.0f) : 0.0f;
  command.source = "RULE_ENGINE";
  command.reason = decision.reason;
  return command;
}

void AutomateManager::applyFinalCommand(const FinalCommand &command) {
  if (!command.enabled) return;
  if (state.safetyActive || command.safetyLimited) {
    actuators.allOff();
    return;
  }
  if (command.command == "allOff") {
    actuators.allOff();
    return;
  }
  if (!command.actuatorId.length()) return;
  // A received command owns only its target until its local watchdog expires.
  // Safety/recovery/allOff above and explicit local manual control stay prioritary.
  if (state.activePriority != "MANUAL_FORCE" && actuators.hasEspNowCommand(command.actuatorId)) return;
  if (command.command == "setMode") {
    actuators.setMode(command.actuatorId, command.mode);
    return;
  }
  actuators.command(command.actuatorId, command.command, command.commandPercent);
}

bool AutomateManager::errorDetected() const {
  return false;
}

void AutomateManager::transitionTo(AutomateState next, const String &reason, uint32_t now) {
  if (next == currentState) {
    state.automateReason = reason;
    return;
  }
  state.previousAutomateState = stateName(currentState);
  currentState = next;
  state.automateState = stateName(currentState);
  state.automateReason = reason;
  state.automateLastTransitionMs = now;
  state.addLog(String("Automate ") + state.previousAutomateState + " -> " + state.automateState + " : " + reason);
}

const char *AutomateManager::stateName(AutomateState value) {
  switch (value) {
    case BOOT: return "BOOT";
    case INIT: return "INIT";
    case IDLE: return "IDLE";
    case AUTO_ROUTING: return "AUTO_ROUTING";
    case MANUAL_FORCE: return "MANUAL_FORCE";
    case SAFETY: return "SAFETY";
    case ERROR: return "ERROR";
    case RECOVERY_REQUEST: return "RECOVERY_REQUEST";
    default: return "IDLE";
  }
}

const char *AutomateManager::priorityName(Priority value) {
  switch (value) {
    case PRIORITY_SAFETY: return "SAFETY";
    case PRIORITY_ERROR: return "ERROR";
    case PRIORITY_RECOVERY_REQUEST: return "RECOVERY_REQUEST";
    case PRIORITY_MANUAL_FORCE: return "MANUAL_FORCE";
    case PRIORITY_RULE_ENGINE: return "RULE_ENGINE";
    case PRIORITY_AUTO_ROUTING: return "AUTO_ROUTING";
    case PRIORITY_IDLE:
    default: return "IDLE";
  }
}
