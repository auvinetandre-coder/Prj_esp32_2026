#include "../config/legacy_config_migration.h"
#include "rule_engine.h"
#include "../config/pid_config_migration.h"

// Catalogue serveur des sources et mesures autorisees pour les conditions.
// Toute condition sauvegardee en JSON doit correspondre a une entree de cette table.
static const RuleEngine::MeasureDef RULE_MEASURES[] = {
  {"JSY-MK-194T", "jsyGridPowerW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "jsyInjectionW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "jsyPurchaseW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "loadPowerW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "loadCurrentA", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "loadEnergyWh", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "jsyProductionW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "jsyGridAvailable", RuleEngine::MEASURE_BOOL, "", ""},
  {"JSY-MK-194T", "jsyLoadAvailable", RuleEngine::MEASURE_BOOL, "", ""},
  {"JSY-MK-194T", "gridPowerW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "injectionW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "consumptionW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "surplusW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "voltageV", RuleEngine::MEASURE_NUMBER, "V", ""},
  {"JSY-MK-194T", "currentA", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "activePowerW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "voltageV1", RuleEngine::MEASURE_NUMBER, "V", ""},
  {"JSY-MK-194T", "currentA1", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "activePowerW1", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "powerFactor1", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "voltageV2", RuleEngine::MEASURE_NUMBER, "V", ""},
  {"JSY-MK-194T", "currentA2", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "activePowerW2", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "powerFactor2", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "powerFactor", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "frequencyHz", RuleEngine::MEASURE_NUMBER, "Hz", ""},
  {"JSY-MK-194T", "CH1_CURR", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "CH1_POWER", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "CH1_PF", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "CH1_ENERGY_POS", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH1_ENERGY_NEG", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH1_EPOS", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH1_ENEG", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH1_DIR", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "CH2_CURR", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"JSY-MK-194T", "CH2_POWER", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"JSY-MK-194T", "CH2_PF", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "CH2_ENERGY_POS", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH2_ENERGY_NEG", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH2_EPOS", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH2_ENEG", RuleEngine::MEASURE_NUMBER, "Wh", ""},
  {"JSY-MK-194T", "CH2_DIR", RuleEngine::MEASURE_NUMBER, "", ""},
  {"JSY-MK-194T", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"TIC Linky", "apparentPowerVA", RuleEngine::MEASURE_NUMBER, "VA", ""},
  {"TIC Linky", "gridPowerW", RuleEngine::MEASURE_NUMBER, "W", ""},
  {"TIC Linky", "currentA", RuleEngine::MEASURE_NUMBER, "A", ""},
  {"TIC Linky", "tariff", RuleEngine::MEASURE_TEXT, "", ""},
  {"TIC Linky", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"TIC Linky", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"sonde1", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"sonde1", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"sonde1", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"sonde2", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"sonde2", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"sonde2", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"sonde3", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"sonde3", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"sonde3", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"DS18B20_TOP", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"DS18B20_TOP", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"DS18B20_TOP", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"DS18B20_MIDDLE", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"DS18B20_MIDDLE", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"DS18B20_MIDDLE", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"DS18B20_BOTTOM", "temperatureC", RuleEngine::MEASURE_NUMBER, "C", ""},
  {"DS18B20_BOTTOM", "available", RuleEngine::MEASURE_BOOL, "", ""},
  {"DS18B20_BOTTOM", "lastValidReadAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"Systeme", "simulationMode", RuleEngine::MEASURE_BOOL, "", ""},
  {"Systeme", "wifiStatus", RuleEngine::MEASURE_ENUM, "", "CONNECTED,AP_FALLBACK,DISCONNECTED"},
  {"Systeme", "uptimeMs", RuleEngine::MEASURE_NUMBER, "ms", ""},
  {"Systeme", "freeHeap", RuleEngine::MEASURE_NUMBER, "B", ""},
  {"Securite", "safetyLevel", RuleEngine::MEASURE_ENUM, "", "OK,WARNING,DEGRADED,CRITICAL"},
  {"Securite", "safetyReason", RuleEngine::MEASURE_TEXT, "", ""},
  {"Securite", "isCritical", RuleEngine::MEASURE_BOOL, "", ""},
};

void RuleEngine::begin() {
  loadRules();
  state.addLog("RuleEngine pret");
}

void RuleEngine::loop() {
  loop(millis());
}

void RuleEngine::loop(uint32_t now) {
  lastDecision = evaluateDecision(now);
}

void RuleEngine::loadRules() {
  lastCommandCount = 0;
  for (uint8_t i = 0; i < MAX_RULES; ++i) {
    eventBranch[i] = 0;
    eventPending[i] = false;
    eventLogged[i] = false;
    lastEventMs[i] = 0;
  }
}

bool RuleEngine::shouldLogBranch(uint8_t index, uint8_t branch, uint32_t now) {
  if (index >= MAX_RULES) return false;
  if (eventBranch[index] != branch) {
    eventBranch[index] = branch;
    eventPending[index] = true;
  }
  if (!eventPending[index]) return false;
  // Keep only the latest branch pending during chatter. No waiting in the loop.
  if (eventLogged[index] && static_cast<uint32_t>(now - lastEventMs[index]) < 5000UL) return false;
  eventPending[index] = false;
  eventLogged[index] = true;
  lastEventMs[index] = now;
  return true;
}

void RuleEngine::evaluate(uint32_t now) {
  evaluateRules(now);
}

void RuleEngine::evaluateRules(uint32_t now) {
  lastDecision = evaluateDecision(now);
}

RuleEngine::RuleDecision RuleEngine::evaluateDecision(uint32_t now) {
  RuleDecision decision;
  selectedCount = 0;
  if (state.safetyTripped) {
    for (uint8_t i = 0; i < MAX_RULES; ++i) { eventBranch[i] = 0; eventPending[i] = false; }
    pid.reset();
    decision.reason = "Safety active";
    lastRuleCount = 0;
    return decision;
  }
  uint8_t count = 0;
  uint8_t order[MAX_RULES];
  for (JsonObject rule : config.rules()) {
    if (count < MAX_RULES) {
      order[count] = count;
      count++;
    }
  }
  lastRuleCount = count;

  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = i + 1; j < count; j++) {
      JsonObject a = ruleAt(order[i]);
      JsonObject b = ruleAt(order[j]);
      if ((b["priority"] | 0) > (a["priority"] | 0)) {
        uint8_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  bool visited[MAX_RULES] = {};
  for (uint8_t idx = 0; idx < count; idx++) {
    JsonObject rule = ruleAt(order[idx]);
    if (!rule["enabled"]) continue;
    String errors;
    DynamicJsonDocument oneRule(4096);
    oneRule["rules"].to<JsonArray>().add(rule);
    if (oneRule.overflowed() || !validateRulesDocument(oneRule, errors)) continue;
    String logic = rule["logic"] | "AND";
    bool matched = logic == "AND";
    for (JsonObject condition : rule["conditions"].as<JsonArray>()) {
      bool ok = evaluateCondition(condition);
      if (logic == "OR") matched = matched || ok;
      else matched = matched && ok;
    }
    JsonArray actions = matched ? rule["actions"].as<JsonArray>() : rule["elseActions"].as<JsonArray>();
    if (actions.isNull()) actions = matched ? rule["then"].as<JsonArray>() : rule["else"].as<JsonArray>();
    if (actions.isNull()) continue;
    visited[order[idx]] = true;
    const bool emitEvent = shouldLogBranch(order[idx], matched ? 1 : 2, now);
    uint8_t actionIndex = 0;
    for (JsonObject action : actions) {
      const String command = action["command"] | "";
      const String target = action["actuatorId"] | "";
      bool claimed = false;
      for (uint8_t n = 0; n < selectedCount; ++n) if (selected[n].actuatorId == target) claimed = true;
      if (!claimed || command == "logEvent" || command == "setSafetyWarning") {
        RuleDecision candidate;
        candidate.matched = matched; candidate.elseBranch = !matched;
        candidate.ruleId = rule["id"] | "";
        candidate.ruleName = rule["name"] | rule["id"] | "regle";
        candidate.priority = rule["priority"] | 0;
        candidate.reason = String("Regle ") + candidate.ruleName + (matched ? " ALORS" : " SINON");
        candidate.pidContext = candidate.ruleId + (matched ? ":then:" : ":else:") + String(actionIndex) + ":" + target;
        if (actionToDecision(action, candidate.ruleName, candidate, emitEvent) && selectedCount < 16) {
          selected[selectedCount++] = candidate;
        }
      }
      ++actionIndex;
    }
  }
  for (uint8_t i = 0; i < MAX_RULES; ++i) {
    if (!visited[i]) { eventBranch[i] = 0; eventPending[i] = false; }
  }
  if (selectedCount) decision = selected[0];
  if (!decision.hasCommand && !decision.ruleName.length()) decision.reason = "Aucune regle active";
  return decision;
}

bool RuleEngine::evaluateCondition(JsonObject condition) {
  return conditionMatches(condition);
}

void RuleEngine::executeAction(JsonObject action, const String &ruleName) {
  RuleDecision ignored;
  actionToDecision(action, ruleName, ignored);
}

bool RuleEngine::actionToDecision(JsonObject action, const String &ruleName, RuleDecision &decision, bool emitEvent) {
  if (state.safetyTripped) return false;
  String command = action["command"] | "";
  String actuatorId = action["actuatorId"] | "";

  if (command == "logEvent") {
    if (emitEvent) state.addLog(String("Regle ") + ruleName + ": " + String(action["message"] | "evenement"));
    return false;
  }

  if (command == "setSafetyWarning") {
    state.safetyLevel = "WARNING";
    state.safetyReason = action["message"] | "Alerte regle";
    if (emitEvent) state.addLog(String("Regle ") + ruleName + ": safety warning");
    return false;
  }

  if (command == "setMode") {
    decision.hasCommand = true;
    decision.actuatorId = actuatorId;
    decision.command = "setMode";
    decision.mode = action["mode"] | action["value"] | "OFF";
    decision.reason = decision.mode;
    return true;
  }

  float value = action["value"] | 0.0f;
  const char *sourceSensorId = action["sourceSensorId"] | "";
  const char *sourceVariable = action["sourceVariable"] | "";
  if (strlen(sourceSensorId) && strlen(sourceVariable)) {
    float sourced = sensorValue(sourceSensorId, sourceVariable);
    if (isfinite(sourced)) value = sourced;
    else {
      value = 0.0f;
      decision.reason = "Mesure source indisponible";
    }
  }

  if (command == "setPowerFromSurplus" || command == "setActuatorPercentFromSurplus") {
    String regulation = action["regulation"] | "PID";
    regulation.toUpperCase();
    decision.pidRegulation = regulation != "PROPORTIONAL";
    if (decision.pidRegulation) decision.pidAction = action;
    value = decision.pidRegulation ? 0.0f : surplusRegulationPercent(action);
    const float limit = action["maxPercent"] | 100.0f;
    value = isfinite(limit) ? min(value, constrain(limit, 0.0f, 100.0f)) : 0.0f;
    command = "setPower";
  } else if (command == "setPowerWatts" || command == "setPowerFromWatts" || command == "setPowerFromSensorWatts") {
    // Convert the requested watts before RuleDecision/AutomateManager clamp
    // their percentage fields. Sending watts through those fields capped at
    // 100 W and converted again in ActuatorManager.
    value = actuators.powerWattsToPercent(actuatorId, value, action["maxOutputPowerW"] | 0.0f);
    command = "setPower";
  }

  // Reflect the effective fixed command, including legacy actions whose value
  // is zero even though 'on' means full output.
  if (command == "on") value = 100.0f;
  else if (command == "stop" || command == "off" || command == "safetyShutdown") value = 0.0f;
  decision.hasCommand = actuatorId.length() && command.length();
  decision.actuatorId = actuatorId;
  decision.command = command;
  decision.commandPercent = constrain(value, 0.0f, 100.0f);
  if (decision.hasCommand && !decision.pidRegulation) logCommandChange(actuatorId + "." + command, value, ruleName);
  return decision.hasCommand;
}

void RuleEngine::resolvePID(RuleDecision &decision, uint32_t now) {
  if (!decision.pidRegulation) return;
  // Acquisition may have completed after the scheduler took its loop timestamp.
  now = millis();
  JsonObject a = decision.pidAction;
  const auto sample = sensors.gridPowerSample(a["pidSensorId"] | "grid", a["pidMeasurement"] | "gridPowerW", now);
  decision.commandPercent = pid.compute(a, sample, now, decision.pidContext);
  logCommandChange(decision.actuatorId + ".setPower", decision.commandPercent, decision.ruleName);
}

void RuleEngine::printRulesStatus() {
  Serial.println(F("=== RuleEngine status ==="));
  uint8_t count = 0;
  for (JsonObject rule : config.rules()) {
    Serial.print(rule["enabled"] ? F("[ON] ") : F("[OFF] "));
    Serial.print(rule["id"] | "");
    Serial.print(F(" priority="));
    Serial.println(rule["priority"] | 0);
    count++;
  }
  Serial.print(F("rules="));
  Serial.println(count);
}

String RuleEngine::validateRulesJson() {
  String errors;
  validateRulesDocument(config.rulesDoc(), errors);
  return errors;
}

bool RuleEngine::validateRulesPayload(const String &payload, String &errorsJson) {
  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    DynamicJsonDocument out(256);
    out.to<JsonArray>().add(String("JSON invalide: ") + err.c_str());
    serializeJson(out, errorsJson);
    return false;
  }
  migratePIDActions(doc.as<JsonObject>(), config.system()["router"], config.sensors());
  if (doc.overflowed()) { errorsJson = "[\"Migration PID trop volumineuse\"]"; return false; }
  return validateRulesDocument(doc, errorsJson);
}

bool RuleEngine::validateRulesDocument(JsonDocument &doc, String &errorsJson) {
  DynamicJsonDocument out(4096);
  JsonArray errors = out.to<JsonArray>();
  if (!doc["rules"].is<JsonArray>()) errors.add("Racine rules manquante");
  for (JsonObject rule : doc["rules"].as<JsonArray>()) {
    String ruleId = rule["id"] | "?";
    if (!rule["id"].is<const char *>()) errors.add("Regle sans id");
    if (!rule["conditions"].is<JsonArray>()) errors.add(ruleId + ": conditions manquantes");
    if (!rule["actions"].is<JsonArray>()) errors.add(ruleId + ": actions manquantes");
    for (JsonObject condition : rule["conditions"].as<JsonArray>()) validateCondition(condition, errors, ruleId);
    for (const char *branch : {"actions", "elseActions", "then", "else"}) {
    for (JsonObject action : rule[branch].as<JsonArray>()) {
      String cmd = action["command"] | "";
      if (!cmd.length()) errors.add(ruleId + ": action sans commande");
      bool knownCommand = false;
      for (const char *allowed : {"setActuatorPercent", "setPower", "setPWM", "setPowerFromSurplus", "setActuatorPercentFromSurplus", "setPowerWatts", "setPowerFromWatts", "setPowerFromSensorWatts", "stop", "off", "on", "toggle", "setMode", "safetyShutdown", "logEvent", "setSafetyWarning"}) {
        if (cmd == allowed) knownCommand = true;
      }
      if (!knownCommand) errors.add(ruleId + ": commande inconnue");
      if (cmd != "logEvent" && cmd != "setSafetyWarning" && !action["actuatorId"].is<const char *>()) errors.add(ruleId + ": action sans actuatorId");
      if (cmd == "setMode" && !action["mode"].is<const char *>() && !action["value"].is<const char *>()) errors.add(ruleId + ": mode actionneur manquant");
      if (cmd == "setPowerFromSurplus" || cmd == "setActuatorPercentFromSurplus") {
        const String algorithm = action["regulation"] | "PID";
        if (algorithm != "PID" && algorithm != "I" && algorithm != "PROPORTIONAL") errors.add(ruleId + ": algorithme inconnu");
        if (algorithm == "I") {
          const float reactivity = action["reactivity"] | 10.0f;
          if ((action.containsKey("reactivity") && !action["reactivity"].is<float>() && !action["reactivity"].is<int>()) ||
              !isfinite(reactivity) || reactivity < 0 || reactivity > 200 ||
              (action.containsKey("kp") && (action["kp"] | -1.0f) != 0) ||
              (action.containsKey("kd") && (action["kd"] | -1.0f) != 0) ||
              (action.containsKey("ki") && (!isfinite(action["ki"] | NAN) || fabsf((action["ki"] | -1.0f) - reactivity / 2000.0f) > 0.000001f)))
            errors.add(ruleId + ": reactivite ou coefficients du mode I invalides");
        }
      }
      if ((cmd == "setPowerFromSurplus" || cmd == "setActuatorPercentFromSurplus") && String(action["regulation"] | "PID") != "PROPORTIONAL") {
        if ((action["pidSchema"] | 0) < 1) errors.add(ruleId + ": migration PID requise");
        for (const char *key : {"pidEnabled", "requireProduction"}) {
          if (action.containsKey(key) && !action[key].is<bool>()) errors.add(ruleId + ": option PID non booleenne: " + key);
        }
        if (!validPIDSettings(PIDController::settings(action))) errors.add(ruleId + ": parametres PID invalides");
        for (const char *key : {"kp", "ki", "kd", "gridSetpointW", "deadbandW", "biasPercent", "maxPercent", "maxOutputRampPercentPerSecond", "maxOutputDropPercentPerSecond", "measurementFilterMs", "derivativeFilterMs", "pidIntervalMs", "measurementTimeoutMs"}) {
          if (action.containsKey(key) && !action[key].is<float>() && !action[key].is<int>()) errors.add(ruleId + ": valeur PID non numerique: " + key);
        }
        const String variable = action["pidMeasurement"] | "gridPowerW";
        if (variable != "gridPowerW" && variable != "jsyGridPowerW") errors.add(ruleId + ": PID exige une puissance reseau signee");
        bool outputFound = false;
        for (JsonObject output : config.actuators()) {
          if (String(output["id"] | "") != String(action["actuatorId"] | "")) continue;
          const String type = output["type"] | "";
          outputFound = type == "SSR" || type == "RobotDyn Triac" || type == "PWM";
        }
        if (!outputFound) errors.add(ruleId + ": sortie PID incompatible ou absente");
      }
    }
  }
    }
  serializeJson(out, errorsJson);
  return errors.size() == 0;
}

float RuleEngine::sensorValue(const String &sensorId, const String &variable) {
  if (sensorId == "jsy_grid") {
    if (variable == "jsyGridPowerW") return sensors.jsyRoleValue("grid", "POWER");
    if (variable == "loadPowerW") return sensors.jsyRoleValue("load", "POWER");
  }
  if (sensorId == "jsy_grid" && variable == "activePower") return state.gridPowerW;
  if (sensorId == "jsy_grid" && variable == "voltageV") return state.gridVoltageV;
  if (sensorId == "jsy_grid" && variable == "frequencyHz") return state.gridFrequencyHz;
  if (sensorId == "jsy_grid" && variable == "CH1_CURR") return state.currentA1;
  if (sensorId == "jsy_grid" && variable == "CH1_POWER") return state.activePowerW1;
  if (sensorId == "jsy_grid" && variable == "CH1_PF") return state.powerFactor1;
  if (sensorId == "jsy_grid" && (variable == "CH1_EPOS" || variable == "CH1_ENERGY_POS")) return state.jsyImportEnergyWh1;
  if (sensorId == "jsy_grid" && (variable == "CH1_ENEG" || variable == "CH1_ENERGY_NEG")) return state.jsyExportEnergyWh1;
  if (sensorId == "jsy_grid" && variable == "CH1_DIR") return state.energyDirection1 == "injection" ? -1.0f : 1.0f;
  if (sensorId == "jsy_grid" && variable == "CH2_CURR") return state.currentA2;
  if (sensorId == "jsy_grid" && variable == "CH2_POWER") return state.activePowerW2;
  if (sensorId == "jsy_grid" && variable == "CH2_PF") return state.powerFactor2;
  if (sensorId == "jsy_grid" && (variable == "CH2_EPOS" || variable == "CH2_ENERGY_POS")) return state.jsyImportEnergyWh2;
  if (sensorId == "jsy_grid" && (variable == "CH2_ENEG" || variable == "CH2_ENERGY_NEG")) return state.jsyExportEnergyWh2;
  if (sensorId == "jsy_grid" && variable == "CH2_DIR") return state.energyDirection2 == "injection" ? -1.0f : 1.0f;
  if (sensorId == "jsy_grid" && variable == "activePowerW1") return state.activePowerW1;
  if (sensorId == "jsy_grid" && variable == "activePowerW2") return state.activePowerW2;
  if (sensorId == "jsy_grid" && variable == "voltageV1") return state.voltageV1;
  if (sensorId == "jsy_grid" && variable == "voltageV2") return state.voltageV2;
  if (sensorId == "jsy_grid" && variable == "currentA1") return state.currentA1;
  if (sensorId == "jsy_grid" && variable == "currentA2") return state.currentA2;
  if (sensorId == "jsy_grid" && variable == "powerFactor1") return state.powerFactor1;
  if (sensorId == "jsy_grid" && variable == "powerFactor2") return state.powerFactor2;
  if (genericTemperatureId(sensorId) == "temperature_1" && variable == "temperature") return state.temperature1C;
  if (genericTemperatureId(sensorId) == "temperature_2" && variable == "temperature") return state.temperature2C;
  if (genericTemperatureId(sensorId) == "temperature_3" && variable == "temperature") return state.temperature3C;
  if (sensorId == "virtual_surplus" && variable == "surplus") return state.surplusW;
  return NAN;
}

bool RuleEngine::conditionMatches(JsonObject condition) {
  if (String(condition["source"] | "") == "Time" || (condition["measure"].is<const char *>() && String(condition["measure"] | "") == "timeWindow")) {
    return timeWindowMatches(condition);
  }
  String source = condition["source"] | "";
  String measure = condition["measure"] | "";
  const MeasureDef *def = findMeasure(source, measure);
  if (!def) return false;
  String op = condition["operator"] | "==";
  if (!operatorAllowed(def->type, op)) return false;
  if (def->type == MEASURE_NUMBER || def->type == MEASURE_BOOL) {
    float left = numericMeasureValue(source, measure);
    if (isnan(left)) return false;
    float right = def->type == MEASURE_BOOL ? (condition["value"] ? 1.0f : 0.0f) : (condition["value"] | 0.0f);
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == "!=") return left != right;
    return left == right;
  }
  String left = textMeasureValue(source, measure);
  String right = condition["value"] | "";
  if (op == "!=") return left != right;
  return left == right;
}

bool RuleEngine::timeWindowMatches(JsonObject condition) {
  uint32_t nowMs = millis();
  uint32_t dayMs = nowMs % 86400000UL;
  uint16_t minutes = dayMs / 60000UL;
  const char *startStr = condition["start"] | "00:00";
  const char *endStr = condition["end"] | "23:59";
  int sh = atoi(startStr);
  const char *smPtr = strchr(startStr, ':');
  int sm = smPtr ? atoi(smPtr + 1) : 0;
  int eh = atoi(endStr);
  const char *emPtr = strchr(endStr, ':');
  int em = emPtr ? atoi(emPtr + 1) : 0;
  uint16_t start = constrain(sh, 0, 23) * 60 + constrain(sm, 0, 59);
  uint16_t end = constrain(eh, 0, 23) * 60 + constrain(em, 0, 59);
  bool inside = start <= end ? (minutes >= start && minutes <= end) : (minutes >= start || minutes <= end);
  String op = condition["operator"] | "==";
  bool wanted = condition["value"] | true;
  return op == "!=" ? inside != wanted : inside == wanted;
}

const RuleEngine::MeasureDef *RuleEngine::findMeasure(const String &source, const String &measure) {
  for (const MeasureDef &def : RULE_MEASURES) {
    if (source == def.source && measure == def.measure) return &def;
  }
  return nullptr;
}

bool RuleEngine::operatorAllowed(MeasureType type, const String &op) {
  if (type == MEASURE_NUMBER) return op == ">" || op == ">=" || op == "<" || op == "<=" || op == "==" || op == "!=";
  return op == "==" || op == "!=";
}

bool RuleEngine::enumValueAllowed(const char *values, const String &value) {
  String list = values;
  int start = 0;
  while (start >= 0) {
    int comma = list.indexOf(',', start);
    String item = comma >= 0 ? list.substring(start, comma) : list.substring(start);
    if (item == value) return true;
    if (comma < 0) break;
    start = comma + 1;
  }
  return false;
}

bool RuleEngine::validateCondition(JsonObject condition, JsonArray errors, const String &ruleId) {
  String source = condition["source"] | "";
  String measure = condition["measure"] | "";
  const MeasureDef *def = findMeasure(source, measure);
  if (!def) {
    errors.add(ruleId + ": condition source/mesure invalide (" + source + "." + measure + ")");
    return false;
  }
  String op = condition["operator"] | "";
  if (!operatorAllowed(def->type, op)) errors.add(ruleId + ": operateur invalide pour " + source + "." + measure);
  String type = condition["type"] | "";
  const char *expected = def->type == MEASURE_NUMBER ? "number" : def->type == MEASURE_BOOL ? "boolean" : def->type == MEASURE_ENUM ? "enum" : "text";
  if (type != expected) errors.add(ruleId + ": type invalide pour " + source + "." + measure);
  if (def->type == MEASURE_ENUM && !enumValueAllowed(def->enumValues, condition["value"] | "")) {
    errors.add(ruleId + ": valeur enum invalide pour " + source + "." + measure);
  }
  if (def->type == MEASURE_BOOL && !condition["value"].is<bool>()) errors.add(ruleId + ": valeur booleenne attendue");
  if (def->type == MEASURE_NUMBER && !condition["value"].is<float>() && !condition["value"].is<int>()) errors.add(ruleId + ": valeur numerique attendue");
  return true;
}

float RuleEngine::numericMeasureValue(const String &source, const String &measure) {
  if (source == "JSY-MK-194T") {
    if (measure == "jsyGridPowerW" || measure == "jsyInjectionW" || measure == "jsyPurchaseW" || measure == "jsyGridAvailable") {
      const float power = sensors.jsyRoleValue("grid", "POWER");
      if (measure == "jsyGridAvailable") return isfinite(power) ? 1.0f : 0.0f;
      if (!isfinite(power)) return NAN;
      if (measure == "jsyInjectionW") return max(0.0f, -power);
      if (measure == "jsyPurchaseW") return max(0.0f, power);
      return power;
    }
    if (measure == "loadPowerW" || measure == "jsyLoadAvailable") {
      const float power = sensors.jsyRoleValue("load", "POWER");
      return measure == "jsyLoadAvailable" ? (isfinite(power) ? 1.0f : 0.0f) : power;
    }
    if (measure == "loadCurrentA") return sensors.jsyRoleValue("load", "CURR");
    if (measure == "loadEnergyWh") return sensors.jsyRoleValue("load", "ENERGY_POS");
    if (measure == "jsyProductionW") return sensors.jsyRoleValue("production", "POWER");
    if (measure == "gridPowerW") return state.gridPowerW;
    if (measure == "activePowerW" || measure == "activePowerW1") return state.activePowerW1;
    if (measure == "activePowerW2") return state.activePowerW2;
    if (measure == "injectionW") return state.injectionW;
    if (measure == "consumptionW") return state.consumptionW;
    if (measure == "surplusW") return state.surplusW;
    if (measure == "voltageV" || measure == "voltageV1") return state.voltageV1;
    if (measure == "voltageV2") return state.voltageV2;
    if (measure == "currentA" || measure == "currentA1") return state.currentA1;
    if (measure == "currentA2") return state.currentA2;
    if (measure == "powerFactor" || measure == "powerFactor1") return state.powerFactor1;
    if (measure == "powerFactor2") return state.powerFactor2;
    if (measure == "frequencyHz") return state.gridFrequencyHz;
    if (measure == "CH1_CURR") return state.currentA1;
    if (measure == "CH1_POWER") return state.activePowerW1;
    if (measure == "CH1_PF") return state.powerFactor1;
    if (measure == "CH1_EPOS" || measure == "CH1_ENERGY_POS") return state.jsyImportEnergyWh1;
    if (measure == "CH1_ENEG" || measure == "CH1_ENERGY_NEG") return state.jsyExportEnergyWh1;
    if (measure == "CH1_DIR") return state.energyDirection1 == "injection" ? -1.0f : 1.0f;
    if (measure == "CH2_CURR") return state.currentA2;
    if (measure == "CH2_POWER") return state.activePowerW2;
    if (measure == "CH2_PF") return state.powerFactor2;
    if (measure == "CH2_EPOS" || measure == "CH2_ENERGY_POS") return state.jsyImportEnergyWh2;
    if (measure == "CH2_ENEG" || measure == "CH2_ENERGY_NEG") return state.jsyExportEnergyWh2;
    if (measure == "CH2_DIR") return state.energyDirection2 == "injection" ? -1.0f : 1.0f;
    if (measure == "available") return state.jsyOnline ? 1 : 0;
  }
  if (source == "TIC Linky") {
    if (measure == "apparentPowerVA") return state.ticApparentPowerVA;
    if (measure == "gridPowerW") return state.ticGridPowerW;
    if (measure == "currentA") return state.ticCurrentA;
    if (measure == "available") return state.ticAvailable ? 1 : 0;
    if (measure == "lastValidReadAgeMs") return state.lastTicReadMs ? millis() - state.lastTicReadMs : 4294967295.0f;
  }
  if (source == "sonde1" || source == "sonde2" || source == "sonde3") {
    uint8_t index = source == "sonde2" ? 1 : (source == "sonde3" ? 2 : 0);
    if (measure == "temperatureC") return state.ds18b20Temps[index];
    if (measure == "available") return state.ds18b20Available[index] ? 1 : 0;
    if (measure == "lastValidReadAgeMs") return state.ds18b20LastReadMs[index] ? millis() - state.ds18b20LastReadMs[index] : 4294967295.0f;
  }
  if (source == "DS18B20_TOP") {
    if (measure == "temperatureC") return state.temperature1C;
    if (measure == "available") return isnan(state.temperature1C) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.temperature1C) ? 4294967295.0f : 0;
  }
  if (source == "DS18B20_MIDDLE") {
    if (measure == "temperatureC") return state.temperature2C;
    if (measure == "available") return isnan(state.temperature2C) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.temperature2C) ? 4294967295.0f : 0;
  }
  if (source == "DS18B20_BOTTOM") {
    if (measure == "temperatureC") return state.temperature3C;
    if (measure == "available") return isnan(state.temperature3C) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.temperature3C) ? 4294967295.0f : 0;
  }

  if (source == "solar") {
    if (measure == "powerW") return state.productionW;
    if (measure == "available") return isnan(state.productionW) ? 0 : 1;
  }
  if (source == "Systeme") {
    if (measure == "simulationMode") return state.simulationMode ? 1 : 0;
    if (measure == "uptimeMs") return millis();
    if (measure == "freeHeap") return ESP.getFreeHeap();
  }
  if (source == "Securite" && measure == "isCritical") return state.safetyTripped ? 1 : 0;
  return NAN;
}

String RuleEngine::textMeasureValue(const String &source, const String &measure) {
  if (source == "Systeme") {
    if (measure == "wifiStatus") return state.wifiConnected ? "CONNECTED" : (state.networkMode == "AP_FALLBACK" ? "AP_FALLBACK" : "DISCONNECTED");
  }
  if (source == "Securite") {
    if (measure == "safetyLevel") return state.safetyLevel;
    if (measure == "safetyReason") return state.safetyReason;
  }
  if (source == "TIC Linky" && measure == "tariff") return state.ticTariff;
  return "";
}

float RuleEngine::surplusRegulationPercent(JsonObject action) {
  String regulation = action["regulation"] | "PID";
  regulation.toUpperCase();
  if (regulation == "PROPORTIONAL") {
    float maxPowerW = action["maxOutputPowerW"] | action["maxPowerW"] | config.system()["router"]["outputMaxPowerW"] | config.system()["router"]["ssr1MaxW"] | 1500.0f;
    if (!isfinite(maxPowerW) || !isfinite(state.surplusW)) return 0.0f;
    if (maxPowerW <= 1.0f) maxPowerW = 1500.0f;
    return constrain((state.surplusW / maxPowerW) * 100.0f, 0.0f, 100.0f);
  }
  return 0.0f; // PID evaluation is deferred until this output wins arbitration.
}

void RuleEngine::logCommandChange(const String &key, float value, const String &ruleName) {
  for (uint8_t i = 0; i < lastCommandCount; i++) {
    if (lastCommandKey[i] == key) {
      if (fabs(lastCommandValue[i] - value) >= 0.5f) {
        lastCommandValue[i] = value;
        state.addLog(String("Regle ") + ruleName + " -> " + key + " = " + String(value, 1));
      }
      return;
    }
  }
  if (lastCommandCount < MAX_RULES) {
    lastCommandKey[lastCommandCount] = key;
    lastCommandValue[lastCommandCount] = value;
    lastCommandCount++;
  }
  state.addLog(String("Regle ") + ruleName + " -> " + key + " = " + String(value, 1));
}

JsonObject RuleEngine::ruleAt(uint8_t index) {
  uint8_t i = 0;
  for (JsonObject rule : config.rules()) {
    if (i++ == index) return rule;
  }
  return JsonObject();
}
