#include "actuator_manager.h"
#include <math.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <hal/gpio_ll.h>

ActuatorManager *ActuatorManager::instance = nullptr;

void ActuatorManager::begin() {
  instance = this;
  espnow.setActuatorCommandHandler(&ActuatorManager::handleEspNowActuatorCommand, this);
  commandTimeoutMs = config.system()["router"]["commandTimeoutMs"] | 5000;
  simulationMode = state.simulationMode;
  previousSimulationMode = simulationMode;

  for (JsonObject actuator : config.actuators()) {
    if (actuator["source"].as<String>() != "local") continue;
    Actuator *channel = channelFor(actuator["id"] | "");
    if (!channel) continue;
    syncChannelFromConfig(channel, actuator);

    if (channel->gpio >= 0) {
      pinMode(channel->gpio, OUTPUT);
      digitalWrite(channel->gpio, channel->activeHigh ? LOW : HIGH);
    }
    if (channel->gpioControl >= 0) {
      pinMode(channel->gpioControl, OUTPUT);
      digitalWrite(channel->gpioControl, channel->activeHigh ? LOW : HIGH);
    }
    if (String(channel->type) == "RobotDyn Triac") setupRobotDyn(actuator);
  }

  allOff();
  state.addLog("Actuators initialized off");
}

void ActuatorManager::loop(uint32_t now) {
  commandTimeoutMs = config.system()["router"]["commandTimeoutMs"] | commandTimeoutMs;
  simulationMode = state.simulationMode;

  if (!previousSimulationMode && simulationMode) {
    // Simulation skips digitalWrite: first release any real output left ON.
    allOff();
    state.addLog("Entree en mode simulation: sorties physiques OFF");
  }
  if (previousSimulationMode && !simulationMode) {
    allOff();
    realModeHoldUntilMs = now + 2000UL;
    state.addLog("Sortie du mode simulation: sorties OFF pendant 2 s");
  }
  previousSimulationMode = simulationMode;

  if (!simulationMode && realModeHoldUntilMs && now < realModeHoldUntilMs) {
    allOff();
    return;
  }
  if (realModeHoldUntilMs && now >= realModeHoldUntilMs) realModeHoldUntilMs = 0;

  if (state.safetyTripped) lockAllForSafety(state.safetyReason);
  else if (safetyLocked) unlockSafety();

  scheduleTriacFire();
  checkSsrTiming(this);
  portENTER_CRITICAL(&ssrMux);
  const bool timingFault = ssrTimingFault;
  portEXIT_CRITICAL(&ssrMux);
  if (timingFault) {
    stopTimedSsr();
    if (!ssrFaultLogged) {
      ssrFaultLogged = true;
      if (ssrTimer) timerStop(ssrTimer);
      if (ssrWatchdog) esp_timer_stop(ssrWatchdog);
      state.logEvent("ERROR", "SSR_TIMING_FAULT", "Sorties SSR cadencees OFF", "ActuatorManager");
    }
  }

  uint32_t configuredChannels = 0;
  const bool publishState = uint32_t(now - lastStatePublishMs) >= 1000UL;
  if (publishState) lastStatePublishMs = now;

  for (JsonObject actuator : config.actuators()) {
    const bool remote = actuator["source"].as<String>() == "espnow";
    Actuator *channel = channelFor(actuator["id"] | "");
    if (!channel) continue;
    configuredChannels |= 1UL << (channel - channels);
    syncChannelFromConfig(channel, actuator);
    if (remote) stopTimedSsr(channel);

    if (!channel->enabled) {
      channel->espNowLeaseMs = 0;
      channel->sharedControl.reset();
      channel->commandPercent = 0;
      publishPower(channel->id, 0);
      if (!remote) applyLocal(actuator, now);
      if (publishState && !remote && (actuator["espNowExportEnabled"] | false))
        espnow.sendActuatorState(channel->id, 0, 0, false, false, channel->safetyLocked, "DISABLED");
      continue;
    }

    const uint32_t timeoutNow = millis();
    if (channel->espNowLeaseMs && uint32_t(timeoutNow - channel->lastCommandMs) >= channel->espNowLeaseMs) {
      channel->espNowLeaseMs = 0;
      channel->sharedControl.reset();
      channel->commandPercent = 0;
      publishPower(channel->id, 0);
      state.logEvent("WARNING", "ESPNOW_COMMAND_TIMEOUT", String(channel->id), "ActuatorManager");
    }
    if (channel->commandPercent > 0 && channel->lastCommandMs && timeoutNow >= channel->lastCommandMs &&
        timeoutNow - channel->lastCommandMs > commandTimeoutMs) {
      channel->commandPercent = 0;
      publishPower(channel->id, 0);
      state.addLog(String("Timeout commande actionneur ") + channel->id);
      if (actuator["debug"] | false) state.logEvent("WARNING", "ACTUATOR_TIMEOUT", String(channel->id), "ActuatorManager");
    }

    if (actuator["critical"] && !criticalControlAllowed()) channel->commandPercent = 0;
    if (channel->safetyLocked) channel->commandPercent = 0;

    if (remote) {
      EspNowActuatorState reported;
      if (espnow.remoteActuatorState(actuator["mac"] | "", actuator["remoteActuatorId"] | channel->id, reported)) observeSharedControl(channel, reported);
      publishPower(channel->id, channel->commandPercent);
      continue;
    }
    applyLocal(actuator, now);
    if (publishState && (actuator["espNowExportEnabled"] | false)) {
      const bool manual = state.manualCommandPersistent && state.manualCommandPending && state.manualCommandActuatorId == channel->id;
      String source = channel->safetyLocked ? "SECURITY" : (manual ? "MANUAL" : (hasEspNowCommand(channel->id) ? "ESP-NOW" : "AUTOMATE"));
      const float output = String(channel->mode) == "OFF" ? 0.0f : channel->commandPercent;
      espnow.sendActuatorState(channel->id, channel->commandPercent, output, channel->currentState, channel->enabled, channel->safetyLocked, source, channel->sharedControl.name(), channel->sharedControl.token);
    }
  }
  for (uint8_t i = 0; i < channelCount; ++i) {
    if (!(configuredChannels & (1UL << i))) stopTimedSsr(&channels[i]);
  }
}

void ActuatorManager::forceAllOff() {
  allOff();
}

void ActuatorManager::allOff() {
  stopTimedSsr();
  for (JsonObject actuator : config.actuators()) {
    int pin = actuator.containsKey("gpio") ? actuator["gpio"].as<int>() : -1;
    int control = actuator.containsKey("control") ? actuator["control"].as<int>() : -1;
    bool activeHigh = actuator["activeHigh"] | true;
    String id = actuator["id"] | "";
    bool offPinHigh = !activeHigh;
    String source = actuator["source"] | "local";
    if (source.equalsIgnoreCase("espnow")) {
      String mac = actuator["mac"] | "";
      String remoteId = actuator["remoteActuatorId"] | id.c_str();
      if (mac.length()) espnow.sendActuatorCommand(mac, remoteId, 0.0f, actuator["mode"] | "OFF", actuator["ttlMs"] | 1000, 0, "safetyShutdown");
    }
    if (pin >= 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, offPinHigh ? HIGH : LOW);
    }
    if (control >= 0) {
      pinMode(control, OUTPUT);
      digitalWrite(control, offPinHigh ? HIGH : LOW);
    }
    if (id == config.routingActuatorId()) state.ssr1PinHigh = offPinHigh;
    if (id == "ssr2_aux") state.ssr2PinHigh = offPinHigh;
    if (id == "robotdyn_triac") state.robotDynPinHigh = offPinHigh;
  }

  for (uint8_t i = 0; i < channelCount; i++) {
    channels[i].commandPercent = 0;
    channels[i].currentState = false;
    channels[i].lastCommandMs = 0;
    channels[i].espNowLeaseMs = 0;
    channels[i].sharedControl.reset();
  }

  stopTimedSsr();
  triacTargetPct = 0;
  triacPhaseEnabled = false;
  triacZeroCrossPending = false;
  stopTriacGate();
  publishPower(config.routingActuatorId(), 0);
  publishPower("ssr2_aux", 0);
  publishPower("robotdyn_triac", 0);
  state.ssr1OutputOn = false;
  state.ssr2OutputOn = false;
  state.robotDynOutputOn = false;
}

bool ActuatorManager::command(const String &actuatorId, const String &cmd, float value) {
  if (cmd == "stop" || cmd == "off" || cmd == "safetyShutdown") {
    setCommandPercent(actuatorId, 0);
    return true;
  }
  if (cmd == "on") {
    setCommandPercent(actuatorId, 100);
    return true;
  }
  if (cmd == "toggle") return false;
  if (cmd == "setPWM" || cmd == "setPower" || cmd == "setActuatorPercent") {
    setCommandPercent(actuatorId, value);
    return true;
  }
  if (cmd == "setPowerWatts" || cmd == "setPowerFromWatts" || cmd == "setPowerFromSensorWatts") {
    setPowerWatts(actuatorId, value);
    return true;
  }
  if (cmd == "setPowerFromSurplus") {
    JsonObject actuator;
    float maxPowerW = 1500.0f;
    if (findActuator(actuatorId, actuator)) maxPowerW = actuator["maxPowerW"] | maxPowerW;
    if (maxPowerW <= 1.0f) maxPowerW = 1500.0f;
    setCommandPercent(actuatorId, constrain((state.surplusW / maxPowerW) * 100.0f, 0.0f, 100.0f));
    return true;
  }
  return false;
}

void ActuatorManager::setPower(const String &actuatorId, float percent) {
  setCommandPercent(actuatorId, percent);
}

void ActuatorManager::setCommandPercent(const String &id, float percent) {
  if (!isfinite(percent)) {
    state.logEvent("WARNING", "ACTUATOR_INVALID_COMMAND", id + ": commande invalide, sortie OFF", "ActuatorManager");
    percent = 0.0f;
  }
  percent = constrain(percent, 0.0f, 100.0f);
  JsonObject actuator;
  if (!findActuator(id, actuator)) return;
  if (!actuator["enabled"]) {
    publishPower(id, 0);
    return;
  }

  String type = actuator["type"] | "";
  String mode = normalizeMode(actuator["mode"] | "OFF", type);
  if (modeIsRobotDynPhase(mode) && type != "RobotDyn Triac") {
    state.addLog(String("Mode PHASE_ANGLE refuse pour ") + id);
    percent = 0;
  }
  if (actuator["critical"] && !criticalControlAllowed()) percent = 0;

  if (actuator["source"].as<String>() == "espnow") {
    String remoteId = actuator["remoteActuatorId"] | id.c_str();
    Actuator *channel = channelFor(id);
    if (channel) {
      syncChannelFromConfig(channel, actuator);
      channel->commandPercent = percent;
      channel->lastCommandMs = millis();
      channel->espNowLeaseMs = 0;
      channel->currentState = percent > 0.5f;
    }
    if (!state.simulationMode) espnow.sendActuatorCommand(actuator["mac"] | "", remoteId, percent, actuator["mode"] | "OFF", actuator["ttlMs"] | 1000, channel ? channel->sharedControl.token : 0);
  } else {
    Actuator *channel = channelFor(id);
    if (channel) {
      syncChannelFromConfig(channel, actuator);
      channel->commandPercent = percent;
      channel->lastCommandMs = millis();
      channel->espNowLeaseMs = 0;
    }
  }

  if (percent <= 0) {
    if (Actuator *channel = channelForExisting(id)) stopTimedSsr(channel);
  }
  publishPower(id, percent);
  state.lastActuatorCommandLog = String(simulationMode ? "SIM " : "REEL ") + id + " = " + String(percent, 1) + "%";
  if (simulationMode || (actuator["debug"] | false)) state.logEvent("INFO", "ACTUATOR_COMMAND", state.lastActuatorCommandLog, "ActuatorManager");
}

void ActuatorManager::setPowerWatts(const String &actuatorId, float watts) {
  setCommandPercent(actuatorId, powerWattsToPercent(actuatorId, watts));
}

float ActuatorManager::powerWattsToPercent(const String &actuatorId, float watts, float maxPowerOverrideW) {
  if (!isfinite(watts) || !isfinite(maxPowerOverrideW) || maxPowerOverrideW < 0.0f) return 0.0f;
  JsonObject actuator;
  if (!findActuator(actuatorId, actuator)) return 0.0f;
  float maxPowerW = maxPowerOverrideW > 0.0f ? maxPowerOverrideW : (actuator["maxPowerW"] | 1000.0f);
  if (!isfinite(maxPowerW)) return 0.0f;
  if (maxPowerW <= 0) maxPowerW = 1000.0f;
  return constrain((watts / maxPowerW) * 100.0f, 0.0f, 100.0f);
}

bool ActuatorManager::hasEspNowCommand(const String &id) {
  Actuator *channel = channelForExisting(id);
  return channel && channel->espNowLeaseMs && uint32_t(millis() - channel->lastCommandMs) < channel->espNowLeaseMs;
}

float ActuatorManager::acceptedPercent(const String &id) {
  JsonObject cfg;
  Actuator *channel = channelForExisting(id);
  if (!channel || !findActuator(id, cfg) || !(cfg["enabled"] | false) || safetyLocked ||
      channel->safetyLocked || channel->remoteNodeLost ||
      ((cfg["critical"] | false) && !criticalControlAllowed())) return 0;
  const String mode = normalizeMode(cfg["mode"] | "OFF", cfg["type"] | "");
  if (mode == "OFF") return 0;
  return channel->commandPercent;
}

bool ActuatorManager::requestSharedControl(const String &id, const String &mode, float value) {
  JsonObject actuator;
  if (!findActuator(id, actuator) || (mode != "manual" && mode != "auto") || !isfinite(value)) return false;
  Actuator *channel = channelFor(id);
  if (!channel) return false;
  if (state.simulationMode) return true; // No real control intents from simulation.
  uint32_t token = esp_random();
  if (!token) token = 1;
  const auto next = value <= 0 ? SharedActuatorControl::OFF : SharedActuatorControl::MANUAL;
  if (actuator["source"].as<String>() == "espnow") {
    if (!espnow.sendActuatorCommand(actuator["mac"] | "", actuator["remoteActuatorId"] | id.c_str(), value,
        actuator["mode"] | "OFF", actuator["ttlMs"] | 1000, token, mode == "auto" ? "controlAuto" : "controlManual")) return false;
  }
  if (mode == "auto") channel->sharedControl.reset();
  else channel->sharedControl.claim(next, token, "");
  channel->controlIssuedMs = millis();
  channel->espNowLeaseMs = 0;
  if (mode == "auto") {
    setCommandPercent(id, 0);
    if (actuator["source"].as<String>() != "espnow") applyLocal(actuator, millis());
  }
  return true;
}

void ActuatorManager::observeSharedControl(Actuator *channel, const EspNowActuatorState &reported) {
  if (!channel || !channel->sharedControl.token || !reported.controlMode[0] ||
      channel->sharedControl.token == reported.controlToken ||
      uint32_t(millis() - channel->controlIssuedMs) < 2000UL) return;
  // The physical owner has accepted AUTO, OFF or another explicit controller.
  // Stop renewing this ESP's superseded manual session, only for this actuator.
  if (state.manualCommandActuatorId == channel->id) {
    state.manualCommandPersistent = false;
    state.manualCommandPending = false;
    state.manualCommandPercent = 0;
    state.manualCommandReason = "Commande partagee remplacee depuis un autre ESP";
  }
  channel->sharedControl.reset();
  channel->commandPercent = 0;
  state.logEvent("INFO", "SHARED_CONTROL_CHANGED", String(channel->id) + " " + reported.controlMode, "ActuatorManager");
}

bool ActuatorManager::handleEspNowActuatorCommand(void *context, const String &actuatorId, const String &cmd, float value, const String &mode, uint32_t ttlMs, const String &peer, uint32_t token) {
  ActuatorManager *self = static_cast<ActuatorManager *>(context);
  if (!self) return false;
  JsonObject actuator;
  if (!self->findActuator(actuatorId, actuator)) return false;
  if (actuator["source"].as<String>() == "espnow") return false;
  if (!(actuator["enabled"] | false) || self->state.simulationMode || self->state.safetyTripped ||
      self->state.safetyActive || self->safetyLocked) return false;
  // A local manual override remains authoritative, including a latched OFF.
  String routerMode = self->config.system()["router"]["mode"] | "AUTO";
  routerMode.toUpperCase();
  if ((self->state.manualCommandPending && self->state.manualCommandActuatorId == actuatorId) ||
      routerMode == "FORCED" || routerMode == "FORCE" || routerMode == "FORCE_") {
    if (cmd != "controlManual" && cmd != "controlAuto" && cmd != "safetyShutdown") return false;
    if (routerMode == "FORCED" || routerMode == "FORCE" || routerMode == "FORCE_") return false;
  }
  Actuator *channel = self->channelFor(actuatorId);
  if (!channel || !isfinite(value)) return false;
  const bool intent = cmd == "controlManual" || cmd == "controlAuto";
  if (intent && !token) return false;
  if (intent || cmd == "safetyShutdown") {
    if (self->state.manualCommandActuatorId == actuatorId) {
      self->state.manualCommandPersistent = false;
      self->state.manualCommandPending = false;
      self->state.manualCommandPercent = 0;
    }
    channel->espNowLeaseMs = 0;
    channel->sharedControl.reset();
    if (cmd == "controlManual") {
      channel->sharedControl.claim(value <= 0 ? SharedActuatorControl::OFF : SharedActuatorControl::MANUAL, token, peer.c_str());
    } else {
      self->setCommandPercent(actuatorId, 0);
      self->applyLocal(actuator, millis());
      return true;
    }
  } else if (!channel->sharedControl.accepts(peer.c_str(), token)) return false;

  String normalizedMode = mode;
  normalizedMode.trim();
  if (normalizedMode.length()) {
    String current = actuator["mode"] | "";
    String normalized = self->normalizeMode(normalizedMode, actuator["type"] | "");
    if (normalized.length() && normalized != "OFF" && normalized != current) {
      actuator["mode"] = normalized;
    }
  }
  if (!self->command(actuatorId, cmd == "controlManual" ? "setPower" : (cmd.length() ? cmd : "setPower"), value)) return false;
  if (!channel) return false;
  // Zero TTL never creates an unlimited hold; cap the lease at the local watchdog.
  const uint32_t watchdogMs = self->config.system()["router"]["commandTimeoutMs"] | 5000UL;
  channel->espNowLeaseMs = ttlMs && ttlMs < watchdogMs ? ttlMs : watchdogMs;
  if (!channel->espNowLeaseMs) channel->espNowLeaseMs = 1000UL;
  self->limitTimedSsrLease(channel);
  return true;
}

void ActuatorManager::setMode(const String &id, const String &mode) {
  JsonObject actuator;
  if (!findActuator(id, actuator)) return;
  String normalized = normalizeMode(mode, actuator["type"] | "");
  if (Actuator *channel = channelForExisting(id)) stopTimedSsr(channel);
  actuator["mode"] = normalized;
  config.saveActuators();
  state.addLog(String("Mode actionneur ") + id + " = " + normalized);
}

void ActuatorManager::lockAllForSafety(const String &reason) {
  safetyLocked = true;
  stopTimedSsr();
  safetyLockReason = reason;
  for (uint8_t i = 0; i < channelCount; i++) {
    channels[i].safetyLocked = true;
    channels[i].commandPercent = 0;
  }
  triacTargetPct = 0;
  triacPhaseEnabled = false;
  stopTriacGate();
  publishPower(config.routingActuatorId(), 0);
  publishPower("ssr2_aux", 0);
  publishPower("robotdyn_triac", 0);
}

void ActuatorManager::unlockSafety() {
  safetyLocked = false;
  safetyLockReason = "";
  for (uint8_t i = 0; i < channelCount; i++) channels[i].safetyLocked = false;
}

void ActuatorManager::stopCritical() {
  lockAllForSafety("SafetyManager critical");
  allOff();
}

void ActuatorManager::runtimeToJson(JsonArray out) {
  const uint32_t now = millis();
  const uint32_t timeoutMs = config.system()["router"]["commandTimeoutMs"] | commandTimeoutMs;

  for (JsonObject actuator : config.actuators()) {
    String id = actuator["id"] | "";
    if (!id.length()) continue;

    String source = actuator["source"] | "local";
    source.toLowerCase();
    const bool remote = source == "espnow";
    const bool enabled = actuator["enabled"] | false;
    const bool critical = actuator["critical"] | false;
    const float maxPowerW = actuator["maxPowerW"] | 0.0f;
    Actuator *channel = channelForExisting(id);
    const uint32_t lastCommandMs = channel ? channel->lastCommandMs : 0;
    const float commandPercent = channel ? channel->commandPercent : 0.0f;
    float outputPercent = channel && String(channel->mode) == "OFF" ? 0.0f : commandPercent;
    bool outputOn = channel ? channel->currentState : false;
    const String activeMode = channel ? channel->mode : "";
    const bool timed = !remote && (activeMode == "MULTI_SINUS" || activeMode == "DEMI_SINUS");
    bool timingFault = false;
    uint32_t missedSteps = 0;
    uint32_t maxIntervalUs = 0;
    if (timed) {
      portENTER_CRITICAL(&ssrMux);
      const TimedSsr &ssr = timedSsr[channel - channels];
      const bool armed = ssr.pin >= 0 && !ssrTimingFault && esp_timer_get_time() < ssr.expiresUs;
      outputPercent = 0.0f;
      if (armed) outputPercent = ssr.demiSinus ? ssr.halfWavePercent : 100.0f * ssr.frame.on / ssr.frame.total;
      outputOn = ssr.on;
      timingFault = ssrTimingFault;
      missedSteps = ssrMissedSteps;
      maxIntervalUs = ssrMaxIntervalUs;
      portEXIT_CRITICAL(&ssrMux);
    }
    const bool channelSafetyLocked = safetyLocked || (channel && channel->safetyLocked) || (critical && state.safetyTripped);
    const bool commandTimedOut = enabled && commandPercent > 0.0f && lastCommandMs &&
      now >= lastCommandMs && now - lastCommandMs > timeoutMs;
    const String remoteMac = actuator["mac"] | "";
    const bool remoteIncomplete = remote && remoteMac.length() == 0;
    const bool remoteNodeKnown = !remote || (!remoteIncomplete && espnow.isNodeKnown(remoteMac));
    const bool remoteNodeOk = !remote || (!remoteIncomplete && espnow.isNodeOk(remoteMac));
    const bool remoteNodeLost = remote && !remoteNodeOk;
    if (remote && channel) {
      if (remoteNodeLost && !channel->remoteNodeLost) {
        state.addLog(String("ACTUATOR LOST remoteMac=") + remoteMac + " actuatorId=" + String(actuator["remoteActuatorId"] | id.c_str()));
      } else if (!remoteNodeLost && channel->remoteNodeLost) {
        state.addLog(String("ACTUATOR RESTORED remoteMac=") + remoteMac + " actuatorId=" + String(actuator["remoteActuatorId"] | id.c_str()));
      }
      channel->remoteNodeLost = remoteNodeLost;
    }
    const bool ok = enabled && !timingFault && !channelSafetyLocked && !commandTimedOut && !remoteIncomplete && !remoteNodeLost;

    String type = actuator["type"] | "SSR";
    String normalizedType = type;
    normalizedType.toUpperCase();
    if (normalizedType.indexOf("ROBOTDYN") >= 0 || normalizedType.indexOf("TRIAC") >= 0) normalizedType = "TRIAC";
    else if (normalizedType.indexOf("RELAY") >= 0 || normalizedType.indexOf("RELAIS") >= 0) normalizedType = "RELAY";
    else if (normalizedType.indexOf("PWM") >= 0) normalizedType = "PWM";
    else if (remote && normalizedType.length() == 0) normalizedType = "REMOTE";
    else if (normalizedType != "SSR" && normalizedType != "TRIAC" && normalizedType != "RELAY" && normalizedType != "PWM") normalizedType = remote ? "REMOTE" : normalizedType;

    String mode = normalizeMode(actuator["mode"] | "OFF", type);
    String electricalMode = mode;
    if (mode == "MANUAL_SAFE") electricalMode = "LOW_FREQ_PWM";

    JsonObject item = out.add<JsonObject>();
    item["id"] = id;
    item["name"] = actuator["name"] | id.c_str();
    item["type"] = normalizedType;
    item["role"] = actuator["role"] | actuator["usage"] | "";
    item["origin"] = remote ? "ESP-NOW" : "LOCAL";
    item["enabled"] = enabled;
    item["debug"] = actuator["debug"] | false;
    item["ok"] = ok;
    if (lastCommandMs) item["ageMs"] = now >= lastCommandMs ? now - lastCommandMs : 0;
    else item["ageMs"] = nullptr;
    item["timedOut"] = commandTimedOut || remoteIncomplete || remoteNodeLost;
    item["state"] = remoteNodeLost ? "LOST" : (commandTimedOut ? "STALE" : (ok ? "OK" : "DEGRADED"));
    item["critical"] = critical;
    item["safetyLocked"] = channelSafetyLocked;
    item["mode"] = mode;
    item["electricalMode"] = electricalMode;
    item["commandPercent"] = commandPercent;
    item["outputPercent"] = outputPercent;
    item["outputOn"] = outputOn;
    if (timed) {
      item["theoreticalDutyPercent"] = outputPercent;
      item["timingFault"] = timingFault;
      item["missedSteps"] = missedSteps;
      item["maxTickIntervalUs"] = maxIntervalUs;
    }
    if (maxPowerW > 0.0f) item["estimatedPowerW"] = maxPowerW * outputPercent / 100.0f;
    else item["estimatedPowerW"] = nullptr;
    item["maxPowerW"] = maxPowerW > 0.0f ? maxPowerW : 0.0f;
    const bool manualOverride = state.manualCommandPersistent && state.manualCommandPending && state.manualCommandActuatorId == id;
    item["manualOverride"] = manualOverride;
    item["manualPercent"] = manualOverride ? state.manualCommandPercent : 0.0f;
    item["source"] = channelSafetyLocked ? "SECURITY" : (manualOverride ? "MANUAL" : (hasEspNowCommand(id) ? "ESP-NOW" : "AUTOMATE"));
    if (!remote) item["gpio"] = actuator.containsKey("gpio") ? actuator["gpio"].as<int>() : -1;
    else item["gpio"] = nullptr;
    String remoteNodeId = actuator["remoteNodeId"] | "";
    if (!remoteNodeId.length()) remoteNodeId = actuator["nodeId"] | "";
    item["remoteNodeId"] = remote ? remoteNodeId : "";
    item["remoteActuatorId"] = remote ? String(actuator["remoteActuatorId"] | id.c_str()) : "";
    item["remoteMac"] = remote ? String(actuator["mac"] | "") : "";
    if (lastCommandMs) item["lastCommandMs"] = lastCommandMs;
    else item["lastCommandMs"] = nullptr;
    item["lastAckMs"] = nullptr;
    item["ackStatus"] = remote ? "NOT_SUPPORTED" : "LOCAL";
    item["nodeOk"] = remote ? remoteNodeOk : true;
    item["nodeLost"] = remoteNodeLost;
    item["discovered"] = !remote || remoteNodeKnown;
    item["commandSent"] = remote && lastCommandMs > 0;
    item["commandAcked"] = false;
    item["lastError"] = remoteIncomplete ? "REMOTE_MAC_MISSING" : (remoteNodeLost ? "NODE_LOST" : (commandTimedOut ? "COMMAND_TIMEOUT" : ""));
    item["requestedPercent"] = commandPercent;
    item["controlMode"] = channel ? channel->sharedControl.name() : "AUTO";
    if (remote) {
      EspNowActuatorState reported;
      const bool fresh = espnow.remoteActuatorState(remoteMac, actuator["remoteActuatorId"] | id.c_str(), reported);
      item["remoteStateAvailable"] = fresh;
      item["ackStatus"] = fresh ? "STATE_RECEIVED" : "STATE_UNAVAILABLE";
      if (fresh) {
        item["commandPercent"] = reported.commandPercent;
        item["outputPercent"] = reported.percent;
        item["outputOn"] = reported.outputOn;
        item["source"] = reported.source;
        item["safetyLocked"] = channelSafetyLocked || reported.safetyLocked;
        item["remoteSimulationMode"] = reported.simulated;
        if (reported.controlMode[0]) item["controlMode"] = reported.controlMode;
        else item["controlMode"] = nullptr;
        item["stateAgeMs"] = uint32_t(millis() - reported.receivedMs);
        item["ok"] = enabled && reported.enabled && !channelSafetyLocked && !reported.safetyLocked && !reported.simulated && !remoteNodeLost;
        item["timedOut"] = false;
        item["state"] = item["ok"].as<bool>() ? "OK" : "DEGRADED";
        item["lastError"] = reported.simulated ? "REMOTE_SIMULATION" : (reported.safetyLocked ? "REMOTE_SAFETY" : "");
        if (maxPowerW > 0) item["estimatedPowerW"] = maxPowerW * reported.percent / 100.0f;
      } else {
        item["commandPercent"] = nullptr;
        item["outputPercent"] = nullptr;
        item["outputOn"] = nullptr;
        item["estimatedPowerW"] = nullptr;
        item["source"] = "UNKNOWN";
        item["controlMode"] = nullptr;
        item["ok"] = false;
        item["timedOut"] = true;
        item["state"] = remoteNodeLost ? "LOST" : "STALE";
        item["lastError"] = remoteIncomplete ? "REMOTE_MAC_MISSING" : (remoteNodeLost ? "NODE_LOST" : "REMOTE_STATE_UNAVAILABLE");
      }
    }
    JsonObject values = item["values"].to<JsonObject>();
    values["commandPercent"] = item["commandPercent"];
    values["outputPercent"] = item["outputPercent"];
    values["outputOn"] = item["outputOn"];
    values["estimatedPowerW"] = item["estimatedPowerW"];
  }
}

bool ActuatorManager::criticalControlAllowed() {
  if (state.safetyTripped || safetyLocked) return false;
  return true;
}

void ActuatorManager::updateSSR(JsonObject actuator, uint32_t now) {
  Actuator *channel = channelFor(actuator["id"] | "");
  if (!channel) return;
  int pin = channel->gpio;
  String mode = normalizeMode(channel->mode, channel->type);
  float percent = constrain(channel->commandPercent, 0.0f, 100.0f);
  if (mode == "MULTI_SINUS" || mode == "DEMI_SINUS") {
    const bool on = updateTimedSsr(channel, mode == "DEMI_SINUS");
    setOutput(channel, pin, on, false);
    return;
  }
  stopTimedSsr(channel);
  if (pin < 0) return;

  if (!isfinite(percent) || mode == "OFF" || percent <= 0) {
    setOutput(channel, pin, false);
    return;
  }
  if (mode == "ON_OFF") {
    setOutput(channel, pin, percent >= 50.0f);
    return;
  }

  if (modeIsSSR(mode)) {
    const uint32_t cycle = channel->cycleMs;
    if (now - channel->cycleStartMs >= cycle) channel->cycleStartMs = now;
    const uint32_t elapsed = now - channel->cycleStartMs;
    const uint32_t onTime = static_cast<uint32_t>((cycle * percent) / 100.0f);
    setOutput(channel, pin, percent >= 100.0f || elapsed < onTime);
    return;
  }

  setOutput(channel, pin, percent > 0);
}

void ActuatorManager::limitTimedSsrLease(Actuator *channel) {
  portENTER_CRITICAL(&ssrMux);
  const int index = channel - channels;
  ++ssrRevision[index];
  const uint32_t age = millis() - channel->lastCommandMs;
  const uint32_t remaining = age < channel->espNowLeaseMs ? channel->espNowLeaseMs - age : 0;
  const int64_t deadline = esp_timer_get_time() + int64_t(remaining) * 1000;
  if (timedSsr[index].expiresUs > deadline) timedSsr[index].expiresUs = deadline;
  portEXIT_CRITICAL(&ssrMux);
}

void ActuatorManager::startSsrTimer() {
  if (ssrTimer || ssrTimingFault) return;
  ssrTimer = timerBegin(1000000);
  if (!ssrTimer) {
    ssrTimingFault = true;
    return;
  }
  timerStop(ssrTimer);
  timerAttachInterruptArg(ssrTimer, &ActuatorManager::onSsrTick, this);
  timerWrite(ssrTimer, 0);
  esp_timer_create_args_t watchdogArgs{};
  watchdogArgs.callback = &ActuatorManager::checkSsrTiming;
  watchdogArgs.arg = this;
  watchdogArgs.name = "ssr-watchdog";
  if (esp_timer_create(&watchdogArgs, &ssrWatchdog) != ESP_OK ||
      esp_timer_start_periodic(ssrWatchdog, 10000) != ESP_OK) {
    ssrTimingFault = true;
    return;
  }
  portENTER_CRITICAL(&ssrMux);
  ssrLastTickUs = esp_timer_get_time();
  portEXIT_CRITICAL(&ssrMux);
  timerAlarm(ssrTimer, 10000, true, 0);
  timerStart(ssrTimer);
}

void ActuatorManager::stopTimedSsr(Actuator *channel) {
  portENTER_CRITICAL(&ssrMux);
  const int first = channel ? channel - channels : 0;
  const int end = channel ? first + 1 : MAX_CHANNELS;
  for (int i = first; i < end; ++i) {
    TimedSsr &ssr = timedSsr[i];
    ++ssrRevision[i];
    if (ssr.pin < 0) continue;
    gpio_ll_set_level(&GPIO, ssr.pin, !ssr.activeHigh);
    ssr = TimedSsr{};
  }
  portEXIT_CRITICAL(&ssrMux);
}

bool ActuatorManager::updateTimedSsr(Actuator *channel, bool demiSinus) {
  const int index = channel - channels;
  portENTER_CRITICAL(&ssrMux);
  const uint32_t revision = ssrRevision[index];
  portEXIT_CRITICAL(&ssrMux);
  const float percent = channel->commandPercent;
  const uint32_t ttl = channel->espNowLeaseMs ? channel->espNowLeaseMs : commandTimeoutMs;
  const uint32_t age = millis() - channel->lastCommandMs;
  if (!channel->enabled || !channel->lastCommandMs || age >= ttl ||
      safetyLocked || channel->safetyLocked || state.safetyTripped || state.simulationMode ||
      realModeHoldUntilMs || !isfinite(percent) || percent <= 0 ||
      channel->gpio < 0 || channel->gpio >= GPIO_NUM_MAX ||
      !GPIO_IS_VALID_OUTPUT_GPIO(channel->gpio)) {
    stopTimedSsr(channel);
    if (channel->gpio >= 0 && channel->gpio < GPIO_NUM_MAX && GPIO_IS_VALID_OUTPUT_GPIO(channel->gpio)) {
      setOutput(channel, channel->gpio, false);
    }
    return false;
  }

  const uint8_t wholePercent = percent >= 100 ? 100 : constrain(int(roundf(percent)), 1, 99);
  const SsrFrame frame = demiSinus ? SsrFrame{0, 1} : ssrFrameFor(wholePercent);
  startSsrTimer();
  TimedSsr &ssr = timedSsr[index];
  portENTER_CRITICAL(&ssrMux);
  if (revision != ssrRevision[index]) {
    portEXIT_CRITICAL(&ssrMux);
    return false;
  }
  const bool needsPin = ssr.pin != channel->gpio || ssr.activeHigh != channel->activeHigh;
  if (needsPin && ssr.pin >= 0) {
    gpio_ll_set_level(&GPIO, ssr.pin, !ssr.activeHigh);
    ssr = TimedSsr{};
  }
  portEXIT_CRITICAL(&ssrMux);
  if (needsPin) {
    gpio_ll_set_level(&GPIO, channel->gpio, !channel->activeHigh);
    pinMode(channel->gpio, OUTPUT);
    gpio_ll_set_level(&GPIO, channel->gpio, !channel->activeHigh);
  }
  portENTER_CRITICAL(&ssrMux);
  const bool traceFrame = state.automateDebug && !ssr.demiSinus &&
                          ssr.frameAppliedMs != ssr.frameLoggedMs;
  const uint32_t frameMs = ssr.frameAppliedMs;
  const SsrFrame appliedFrame = ssr.frame;
  ssr.frameLoggedMs = frameMs;
  if (!ssrTimingFault && revision == ssrRevision[index]) {
    ssr.pin = channel->gpio;
    ssr.activeHigh = channel->activeHigh;
    if (ssr.demiSinus != demiSinus) {
      gpio_ll_set_level(&GPIO, ssr.pin, !ssr.activeHigh);
      ssr.on = false;
      ssr.step = 0;
      ssr.halfWave = SsrHalfWave{};
    }
    ssr.demiSinus = demiSinus;
    ssr.halfWavePercent = wholePercent;
    ssr.requested = frame;
    ssr.expiresUs = esp_timer_get_time() + int64_t(ttl - age) * 1000;
  }
  const bool on = ssr.on && !ssrTimingFault && revision == ssrRevision[index];
  portEXIT_CRITICAL(&ssrMux);
  if (traceFrame) {
    state.logAutomateDebug(frameMs, "SSR_TRAME",
        String(channel->id) + " ON_ms=" + String(appliedFrame.on * 10) +
        " total_ms=" + String(appliedFrame.total * 10));
  }
  return on;
}

void ActuatorManager::checkSsrTiming(void *context) {
  auto &manager = *static_cast<ActuatorManager *>(context);
  portENTER_CRITICAL(&manager.ssrMux);
  const int64_t elapsed = esp_timer_get_time() - manager.ssrLastTickUs;
  if (manager.ssrLastTickUs && !manager.ssrTimingFault && elapsed >= 20000) {
    manager.ssrTimingFault = true;
    manager.ssrMissedSteps += uint32_t(elapsed / 10000 - 1);
    if (elapsed > manager.ssrMaxIntervalUs) manager.ssrMaxIntervalUs = uint32_t(elapsed);
    for (auto &ssr : manager.timedSsr) {
      if (ssr.pin >= 0 && ssr.on) gpio_ll_set_level(&GPIO, ssr.pin, !ssr.activeHigh);
      ssr.on = false;
    }
  }
  portEXIT_CRITICAL(&manager.ssrMux);
}

void IRAM_ATTR ActuatorManager::onSsrTick(void *context) {
  auto &manager = *static_cast<ActuatorManager *>(context);
  portENTER_CRITICAL_ISR(&manager.ssrMux);
  const int64_t now = esp_timer_get_time();
  const uint32_t elapsed = uint32_t(now - manager.ssrLastTickUs);
  manager.ssrLastTickUs = now;
  if (elapsed > manager.ssrMaxIntervalUs) manager.ssrMaxIntervalUs = elapsed;
  if (!manager.ssrTimingFault && elapsed >= 20000) {
    manager.ssrMissedSteps += elapsed / 10000 - 1;
    manager.ssrTimingFault = true;
  }
  for (auto &ssr : manager.timedSsr) {
    if (ssr.pin < 0) continue;
    if (manager.ssrTimingFault || now >= ssr.expiresUs) {
      if (ssr.on) gpio_ll_set_level(&GPIO, ssr.pin, !ssr.activeHigh);
      ssr.on = false;
      ssr.step = 0;
      ssr.halfWave.credit = 0;
      ssr.halfWave.phase = false;
      ssr.halfWave.lastOnPhase = false;
      continue;
    }
    if (!ssr.demiSinus && (ssr.step == 0 || ssr.requested.total == 1) &&
        (ssr.frame.on != ssr.requested.on || ssr.frame.total != ssr.requested.total)) {
      ssr.frameAppliedMs = uint32_t(now / 1000);
    }
    const bool on = ssr.demiSinus ? ssrHalfWaveAdvance(ssr.halfWave, ssr.halfWavePercent)
                                 : ssrAdvance(ssr.frame, ssr.requested, ssr.step);
    if (ssr.on != on) gpio_ll_set_level(&GPIO, ssr.pin, on == ssr.activeHigh);
    ssr.on = on;
  }
  portEXIT_CRITICAL_ISR(&manager.ssrMux);
}

void ActuatorManager::updateRobotDyn(JsonObject actuator, uint32_t now) {
  Actuator *channel = channelFor(actuator["id"] | "");
  if (!channel) return;
  String mode = normalizeMode(channel->mode, channel->type);

  if (mode == "PHASE_ANGLE") {
    triacControlPin = channel->gpioControl >= 0 ? channel->gpioControl : channel->gpio;
    triacTargetPct = constrain(channel->commandPercent, 0.0f, 100.0f);
    triacPhaseEnabled = triacTargetPct > 0 && !channel->safetyLocked;
    if (triacTargetPct <= 0) stopTriacGate();
    return;
  }

  triacPhaseEnabled = false;
  stopTriacGate();
  updateSSR(actuator, now);
}

void ActuatorManager::printStatus() {
  Serial.println(F("=== ActuatorManager status ==="));
  Serial.print(F("simulation=")); Serial.println(simulationMode ? F("true") : F("false"));
  Serial.print(F("safetyLocked=")); Serial.println(safetyLocked ? F("true") : F("false"));
  for (uint8_t i = 0; i < channelCount; i++) {
    Serial.print(channels[i].id);
    Serial.print(F(" mode=")); Serial.print(channels[i].mode);
    Serial.print(F(" command=")); Serial.print(channels[i].commandPercent);
    Serial.print(F("% state=")); Serial.println(channels[i].currentState ? F("ON") : F("OFF"));
  }
}

bool ActuatorManager::findActuator(const String &id, JsonObject &out) {
  for (JsonObject actuator : config.actuators()) {
    if (actuator["id"].as<String>() == id) {
      out = actuator;
      return true;
    }
  }
  return false;
}

ActuatorManager::Actuator *ActuatorManager::channelFor(const String &id) {
  if (!id.length()) return nullptr;
  for (uint8_t i = 0; i < channelCount; i++) {
    if (id == channels[i].id) return &channels[i];
  }
  if (channelCount >= MAX_CHANNELS) return nullptr;
  strlcpy(channels[channelCount].id, id.c_str(), sizeof(channels[channelCount].id));
  channels[channelCount].cycleStartMs = millis();
  return &channels[channelCount++];
}

ActuatorManager::Actuator *ActuatorManager::channelForExisting(const String &id) {
  if (!id.length()) return nullptr;
  for (uint8_t i = 0; i < channelCount; i++) {
    if (id == channels[i].id) return &channels[i];
  }
  return nullptr;
}

void ActuatorManager::syncChannelFromConfig(Actuator *channel, JsonObject actuator) {
  if (!channel) return;
  strlcpy(channel->name, actuator["name"] | "", sizeof(channel->name));
  strlcpy(channel->type, actuator["type"] | "", sizeof(channel->type));
  String mode = normalizeMode(actuator["mode"] | "OFF", channel->type);
  strlcpy(channel->mode, mode.c_str(), sizeof(channel->mode));
  channel->gpio = actuator.containsKey("gpio") ? actuator["gpio"].as<int>() : -1;
  channel->gpioZeroCross = actuator["zeroCross"] | -1;
  channel->gpioControl = actuator.containsKey("control") ? actuator["control"].as<int>() : channel->gpio;
  channel->enabled = actuator["enabled"] | false;
  channel->activeHigh = actuator["activeHigh"] | true;
  channel->maxPowerW = actuator["maxPowerW"] | 1000.0f;
  channel->cycleMs = configuredCycleMs(actuator);
  channel->safetyLocked = safetyLocked || (actuator["critical"] && state.safetyTripped);
}

void ActuatorManager::applyLocal(JsonObject actuator, uint32_t now) {
  Actuator *channel = channelFor(actuator["id"] | "");
  if (!channel) return;
  if (String(channel->type) == "RobotDyn Triac") {
    stopTimedSsr(channel);
    updateRobotDyn(actuator, now);
  } else updateSSR(actuator, now);
}

void ActuatorManager::setOutput(Actuator *channel, int pin, bool on, bool writePin) {
  if (!channel) return;
  channel->currentState = on;
  String id = channel->id;
  bool pinHigh = on ? channel->activeHigh : !channel->activeHigh;
  if (id == config.routingActuatorId()) state.ssr1OutputOn = on;
  if (id == "ssr2_aux") state.ssr2OutputOn = on;
  if (id == "robotdyn_triac") state.robotDynOutputOn = on;
  if (id == config.routingActuatorId()) state.ssr1PinHigh = pinHigh;
  if (id == "ssr2_aux") state.ssr2PinHigh = pinHigh;
  if (id == "robotdyn_triac") state.robotDynPinHigh = pinHigh;
  if (writePin && !simulationMode) digitalWrite(pin, pinHigh ? HIGH : LOW);
}

uint32_t ActuatorManager::configuredCycleMs(JsonObject actuator) {
  uint32_t value = actuator["cycleMs"] | config.system()["router"]["ssrCycleMs"] | 1000;
  return constrain(value, 200UL, 10000UL);
}

String ActuatorManager::normalizeMode(const String &mode, const String &type) {
  String m = mode;
  m.trim();
  m.toUpperCase();
  m.replace(" ", "_");
  m.replace("-", "_");
  if (m == "TOUT_OU_RIEN") return "ON_OFF";
  if (m == "MULTI_SINUS" || m == "DEMI_SINUS") return type.equalsIgnoreCase("SSR") ? m : String("OFF");
  if (m == "BURST_FIRING") return "BURST_FIRE";
  if (m == "TRAIN_ONDES_ENTIERES" || m == "ZERO_CROSS_BURST") return "BURST_FIRE";
  if (m == "PWM_BASSE_FREQUENCE") return "LOW_FREQ_PWM";
  if (m == "MANUEL_SECURise") return "MANUAL_SAFE";
  if (m == "MANUEL_SECURISE") return "MANUAL_SAFE";
  if (m == "ANGLE_DE_PHASE") return type == "RobotDyn Triac" ? "PHASE_ANGLE" : "OFF";
  if (m == "OFF" || m == "ON_OFF" || m == "BURST_FIRE" ||
      m == "LOW_FREQ_PWM" || m == "PHASE_ANGLE" || m == "MANUAL_SAFE") {
    if (m == "PHASE_ANGLE" && type != "RobotDyn Triac") return "OFF";
    return m;
  }
  return "OFF";
}

bool ActuatorManager::modeIsSSR(const String &mode) {
  return mode == "BURST_FIRE" || mode == "LOW_FREQ_PWM" ||
         mode == "MANUAL_SAFE";
}

bool ActuatorManager::modeIsRobotDynPhase(const String &mode) {
  return mode == "PHASE_ANGLE";
}

void ActuatorManager::publishPower(const String &id, float percent) {
  percent = constrain(percent, 0.0f, 100.0f);
  if (id == config.routingActuatorId()) state.ssr1PowerPct = percent;
  if (id == "ssr2_aux") state.ssr2PowerPct = percent;
  if (id == "robotdyn_triac") state.robotDynPowerPct = percent;
}

void ActuatorManager::setupRobotDyn(JsonObject actuator) {
  triacZeroCrossPin = actuator["zeroCross"] | -1;
  triacControlPin = actuator["control"] | -1;
  if (triacControlPin >= 0) {
    pinMode(triacControlPin, OUTPUT);
    digitalWrite(triacControlPin, LOW);
  }
  if (!triacFireTimer) {
    esp_timer_create_args_t fireArgs{};
    fireArgs.callback = &ActuatorManager::onTriacFireTimer;
    fireArgs.arg = this;
    fireArgs.name = "triac_fire";
    esp_timer_create(&fireArgs, &triacFireTimer);
  }
  if (!triacOffTimer) {
    esp_timer_create_args_t offArgs{};
    offArgs.callback = &ActuatorManager::onTriacOffTimer;
    offArgs.arg = this;
    offArgs.name = "triac_off";
    esp_timer_create(&offArgs, &triacOffTimer);
  }
  if (triacZeroCrossPin >= 0) {
    pinMode(triacZeroCrossPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(triacZeroCrossPin), ActuatorManager::onZeroCrossIsr, RISING);
  }
}

void IRAM_ATTR ActuatorManager::onZeroCrossIsr() {
  ActuatorManager *self = instance;
  if (!self || !self->triacPhaseEnabled || self->triacTargetPct <= 0) return;
  float pct = self->triacTargetPct;
  if (pct > 100.0f) pct = 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  self->triacDelayUs = static_cast<uint64_t>(300.0f + ((100.0f - pct) * 85.0f));
  self->triacZeroCrossPending = true;
}

void ActuatorManager::onTriacFireTimer(void *arg) {
  static_cast<ActuatorManager *>(arg)->fireTriacGate();
}

void ActuatorManager::onTriacOffTimer(void *arg) {
  static_cast<ActuatorManager *>(arg)->stopTriacGate();
}

void ActuatorManager::scheduleTriacFire() {
  if (!triacZeroCrossPending || !triacFireTimer) return;
  noInterrupts();
  uint64_t delayUs = triacDelayUs;
  triacZeroCrossPending = false;
  interrupts();
  esp_timer_stop(triacFireTimer);
  esp_timer_start_once(triacFireTimer, delayUs);
}

void ActuatorManager::fireTriacGate() {
  if (!triacPhaseEnabled || triacControlPin < 0) return;
  if (!simulationMode) digitalWrite(triacControlPin, HIGH);
  if (triacOffTimer) {
    esp_timer_stop(triacOffTimer);
    esp_timer_start_once(triacOffTimer, 120);
  }
}

void ActuatorManager::stopTriacGate() {
  if (triacControlPin >= 0 && !simulationMode) digitalWrite(triacControlPin, LOW);
}
