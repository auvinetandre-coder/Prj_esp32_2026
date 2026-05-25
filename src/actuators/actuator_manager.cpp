#include "actuator_manager.h"

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

  for (JsonObject actuator : config.actuators()) {
    if (actuator["source"].as<String>() != "local") continue;
    Actuator *channel = channelFor(actuator["id"] | "");
    if (!channel) continue;
    syncChannelFromConfig(channel, actuator);

    if (!channel->enabled) {
      channel->commandPercent = 0;
      publishPower(channel->id, 0);
      applyLocal(actuator, now);
      continue;
    }

    const uint32_t timeoutNow = millis();
    if (channel->commandPercent > 0 && channel->lastCommandMs && timeoutNow >= channel->lastCommandMs &&
        timeoutNow - channel->lastCommandMs > commandTimeoutMs) {
      channel->commandPercent = 0;
      publishPower(channel->id, 0);
      state.addLog(String("Timeout commande actionneur ") + channel->id);
    }

    if (actuator["critical"] && !criticalControlAllowed()) channel->commandPercent = 0;
    if (channel->safetyLocked) channel->commandPercent = 0;

    applyLocal(actuator, now);
  }
}

void ActuatorManager::forceAllOff() {
  allOff();
}

void ActuatorManager::allOff() {
  for (JsonObject actuator : config.actuators()) {
    int pin = actuator.containsKey("gpio") ? actuator["gpio"].as<int>() : -1;
    int control = actuator.containsKey("control") ? actuator["control"].as<int>() : -1;
    bool activeHigh = actuator["activeHigh"] | true;
    String id = actuator["id"] | "";
    bool offPinHigh = !activeHigh;
    if (pin >= 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, offPinHigh ? HIGH : LOW);
    }
    if (control >= 0) {
      pinMode(control, OUTPUT);
      digitalWrite(control, offPinHigh ? HIGH : LOW);
    }
    if (id == "ssr1_water_heater") state.ssr1PinHigh = offPinHigh;
    if (id == "ssr2_aux") state.ssr2PinHigh = offPinHigh;
    if (id == "robotdyn_triac") state.robotDynPinHigh = offPinHigh;
  }

  for (uint8_t i = 0; i < channelCount; i++) {
    channels[i].commandPercent = 0;
    channels[i].currentState = false;
    channels[i].lastCommandMs = 0;
  }

  triacTargetPct = 0;
  triacPhaseEnabled = false;
  triacZeroCrossPending = false;
  stopTriacGate();
  publishPower("ssr1_water_heater", 0);
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
    espnow.sendActuatorCommand(actuator["mac"] | "", id, percent, actuator["mode"] | "OFF", actuator["ttlMs"] | 1000);
  } else {
    Actuator *channel = channelFor(id);
    if (channel) {
      syncChannelFromConfig(channel, actuator);
      channel->commandPercent = percent;
      channel->lastCommandMs = millis();
    }
  }

  publishPower(id, percent);
  state.lastActuatorCommandLog = String(simulationMode ? "SIM " : "REEL ") + id + " = " + String(percent, 1) + "%";
  if (simulationMode) state.logEvent("INFO", "ACTUATOR_COMMAND", state.lastActuatorCommandLog, "ActuatorManager");
}

void ActuatorManager::setPowerWatts(const String &actuatorId, float watts) {
  JsonObject actuator;
  if (!findActuator(actuatorId, actuator)) return;
  float maxPowerW = actuator["maxPowerW"] | 1000.0f;
  if (maxPowerW <= 0) maxPowerW = 1000.0f;
  setCommandPercent(actuatorId, constrain((watts / maxPowerW) * 100.0f, 0.0f, 100.0f));
}

bool ActuatorManager::handleEspNowActuatorCommand(void *context, const String &actuatorId, const String &cmd, float value, const String &mode) {
  ActuatorManager *self = static_cast<ActuatorManager *>(context);
  if (!self) return false;
  JsonObject actuator;
  if (!self->findActuator(actuatorId, actuator)) return false;
  if (actuator["source"].as<String>() == "espnow") return false;

  String normalizedMode = mode;
  normalizedMode.trim();
  if (normalizedMode.length()) {
    String current = actuator["mode"] | "";
    String normalized = self->normalizeMode(normalizedMode, actuator["type"] | "");
    if (normalized.length() && normalized != "OFF" && normalized != current) {
      actuator["mode"] = normalized;
    }
  }
  return self->command(actuatorId, cmd.length() ? cmd : "setPower", value);
}

void ActuatorManager::setMode(const String &id, const String &mode) {
  JsonObject actuator;
  if (!findActuator(id, actuator)) return;
  String normalized = normalizeMode(mode, actuator["type"] | "");
  actuator["mode"] = normalized;
  config.saveActuators();
  state.addLog(String("Mode actionneur ") + id + " = " + normalized);
}

void ActuatorManager::lockAllForSafety(const String &reason) {
  safetyLocked = true;
  safetyLockReason = reason;
  for (uint8_t i = 0; i < channelCount; i++) {
    channels[i].safetyLocked = true;
    channels[i].commandPercent = 0;
  }
  triacTargetPct = 0;
  triacPhaseEnabled = false;
  stopTriacGate();
  publishPower("ssr1_water_heater", 0);
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

bool ActuatorManager::criticalControlAllowed() {
  if (state.safetyTripped || safetyLocked) return false;
  if (state.role == ROLE_MASTER) return true;
  if (state.role == ROLE_BACKUP) return state.isActiveMaster;
  return state.role == ROLE_NODE_ACTUATOR || state.role == ROLE_NODE_MIXED;
}

void ActuatorManager::updateSSR(JsonObject actuator, uint32_t now) {
  Actuator *channel = channelFor(actuator["id"] | "");
  if (!channel) return;
  int pin = channel->gpio;
  if (pin < 0) return;

  String mode = normalizeMode(channel->mode, channel->type);
  float percent = constrain(channel->commandPercent, 0.0f, 100.0f);

  if (mode == "OFF" || percent <= 0) {
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
  if (String(channel->type) == "RobotDyn Triac") updateRobotDyn(actuator, now);
  else updateSSR(actuator, now);
}

void ActuatorManager::setOutput(Actuator *channel, int pin, bool on) {
  if (!channel) return;
  channel->currentState = on;
  String id = channel->id;
  bool pinHigh = on ? channel->activeHigh : !channel->activeHigh;
  if (id == "ssr1_water_heater") state.ssr1OutputOn = on;
  if (id == "ssr2_aux") state.ssr2OutputOn = on;
  if (id == "robotdyn_triac") state.robotDynOutputOn = on;
  if (id == "ssr1_water_heater") state.ssr1PinHigh = pinHigh;
  if (id == "ssr2_aux") state.ssr2PinHigh = pinHigh;
  if (id == "robotdyn_triac") state.robotDynPinHigh = pinHigh;
  if (!simulationMode) digitalWrite(pin, pinHigh ? HIGH : LOW);
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
  if (m == "BURST_FIRING") return "BURST_FIRE";
  if (m == "TRAIN_ONDES_ENTIERES") return "TRAIN_ONDES_ENTIERES";
  if (m == "ZERO_CROSS_BURST") return "ZERO_CROSS_BURST";
  if (m == "PWM_BASSE_FREQUENCE") return "LOW_FREQ_PWM";
  if (m == "MANUEL_SECURise") return "MANUAL_SAFE";
  if (m == "MANUEL_SECURISE") return "MANUAL_SAFE";
  if (m == "ANGLE_DE_PHASE") return type == "RobotDyn Triac" ? "PHASE_ANGLE" : "OFF";
  if (m == "OFF" || m == "ON_OFF" || m == "BURST_FIRE" || m == "TRAIN_ONDES_ENTIERES" ||
      m == "ZERO_CROSS_BURST" || m == "LOW_FREQ_PWM" || m == "PHASE_ANGLE" || m == "MANUAL_SAFE") {
    if (m == "PHASE_ANGLE" && type != "RobotDyn Triac") return "OFF";
    return m;
  }
  return "OFF";
}

bool ActuatorManager::modeIsSSR(const String &mode) {
  return mode == "BURST_FIRE" || mode == "TRAIN_ONDES_ENTIERES" ||
         mode == "ZERO_CROSS_BURST" || mode == "LOW_FREQ_PWM" ||
         mode == "MANUAL_SAFE";
}

bool ActuatorManager::modeIsRobotDynPhase(const String &mode) {
  return mode == "PHASE_ANGLE";
}

void ActuatorManager::publishPower(const String &id, float percent) {
  percent = constrain(percent, 0.0f, 100.0f);
  if (id == "ssr1_water_heater") state.ssr1PowerPct = percent;
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
