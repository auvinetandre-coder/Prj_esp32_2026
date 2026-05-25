#include "rule_engine.h"

// Catalogue serveur des sources et mesures autorisees pour les conditions.
// Toute condition sauvegardee en JSON doit correspondre a une entree de cette table.
static const RuleEngine::MeasureDef RULE_MEASURES[] = {
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
  {"Systeme", "role", RuleEngine::MEASURE_ENUM, "", "MASTER,BACKUP,NODE_SENSOR,NODE_ACTUATOR,NODE_MIXED"},
  {"Securite", "safetyLevel", RuleEngine::MEASURE_ENUM, "", "OK,WARNING,DEGRADED,CRITICAL"},
  {"Securite", "safetyReason", RuleEngine::MEASURE_TEXT, "", ""},
  {"Securite", "isCritical", RuleEngine::MEASURE_BOOL, "", ""},
  {"Redondance", "activeRole", RuleEngine::MEASURE_ENUM, "", "MASTER,BACKUP,NODE_SENSOR,NODE_ACTUATOR,NODE_MIXED"},
  {"Redondance", "isActiveMaster", RuleEngine::MEASURE_BOOL, "", ""},
  {"Redondance", "activeMasterId", RuleEngine::MEASURE_TEXT, "", ""},
  {"Redondance", "epoch", RuleEngine::MEASURE_NUMBER, "", ""},
  {"Redondance", "lastHeartbeatAgeMs", RuleEngine::MEASURE_NUMBER, "ms", ""}
};

void RuleEngine::begin() {
  loadRules();
  state.addLog("RuleEngine pret");
}

void RuleEngine::loop() {
  loop(millis());
}

void RuleEngine::loop(uint32_t now) {
  evaluateRules(now);
}

void RuleEngine::loadRules() {
  lastCommandCount = 0;
}

void RuleEngine::evaluate(uint32_t now) {
  evaluateRules(now);
}

void RuleEngine::evaluateRules(uint32_t now) {
  if (state.safetyTripped) {
    pid.reset();
    return;
  }
  uint8_t count = 0;
  uint8_t order[MAX_RULES];
  for (JsonObject rule : config.rules()) {
    if (count < MAX_RULES) order[count++] = count;
  }

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

  bool surplusRegulationActive = false;
  for (uint8_t idx = 0; idx < count; idx++) {
    JsonObject rule = ruleAt(order[idx]);
    if (!rule["enabled"]) continue;
    String errors;
    DynamicJsonDocument oneRule(2048);
    oneRule["rules"].to<JsonArray>().add(rule);
    if (!validateRulesDocument(oneRule, errors)) continue;
    String logic = rule["logic"] | "AND";
    bool matched = logic == "AND";
    for (JsonObject condition : rule["conditions"].as<JsonArray>()) {
      bool ok = evaluateCondition(condition);
      if (logic == "OR") matched = matched || ok;
      else matched = matched && ok;
    }
    if (!matched) continue;
    for (JsonObject action : rule["actions"].as<JsonArray>()) {
      String command = action["command"] | "";
      if (command == "setPowerFromSurplus" || command == "setActuatorPercentFromSurplus") surplusRegulationActive = true;
      executeAction(action, rule["name"] | rule["id"] | "regle");
    }
  }
  if (!surplusRegulationActive) pid.reset();
}

bool RuleEngine::evaluateCondition(JsonObject condition) {
  return conditionMatches(condition);
}

void RuleEngine::executeAction(JsonObject action, const String &ruleName) {
  if (state.safetyTripped) return;
  String command = action["command"] | "";
  String actuatorId = action["actuatorId"] | "";

  if (command == "logEvent") {
    state.addLog(String("Regle ") + ruleName + ": " + String(action["message"] | "evenement"));
    return;
  }

  if (command == "setSafetyWarning") {
    state.safetyLevel = "WARNING";
    state.safetyReason = action["message"] | "Alerte regle";
    state.addLog(String("Regle ") + ruleName + ": safety warning");
    return;
  }

  if (command == "setMode") {
    actuators.setMode(actuatorId, action["mode"] | action["value"] | "OFF");
    return;
  }

  float value = action["value"] | 0.0f;
  const char *sourceSensorId = action["sourceSensorId"] | "";
  const char *sourceVariable = action["sourceVariable"] | "";
  if (strlen(sourceSensorId) && strlen(sourceVariable)) {
    float sourced = sensorValue(sourceSensorId, sourceVariable);
    if (!isnan(sourced)) value = sourced;
  }

  if (command == "setPowerFromSurplus" || command == "setActuatorPercentFromSurplus") {
    value = surplusRegulationPercent(action);
    command = "setPower";
  }

  if (actuators.command(actuatorId, command, value)) {
    logCommandChange(actuatorId + "." + command, value, ruleName);
  }
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
  DynamicJsonDocument doc(12288);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    DynamicJsonDocument out(256);
    out.to<JsonArray>().add(String("JSON invalide: ") + err.c_str());
    serializeJson(out, errorsJson);
    return false;
  }
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
    for (JsonObject action : rule["actions"].as<JsonArray>()) {
      String cmd = action["command"] | "";
      if (!cmd.length()) errors.add(ruleId + ": action sans commande");
      if (cmd != "logEvent" && cmd != "setSafetyWarning" && !action["actuatorId"].is<const char *>()) errors.add(ruleId + ": action sans actuatorId");
      if (cmd == "setMode" && !action["mode"].is<const char *>() && !action["value"].is<const char *>()) errors.add(ruleId + ": mode actionneur manquant");
    }
  }
  serializeJson(out, errorsJson);
  return errors.size() == 0;
}

float RuleEngine::sensorValue(const String &sensorId, const String &variable) {
  if (sensorId == "jsy_grid" && variable == "activePower") return state.gridPowerW;
  if (sensorId == "jsy_grid" && variable == "activePowerW1") return state.activePowerW1;
  if (sensorId == "jsy_grid" && variable == "activePowerW2") return state.activePowerW2;
  if (sensorId == "jsy_grid" && variable == "voltageV1") return state.voltageV1;
  if (sensorId == "jsy_grid" && variable == "voltageV2") return state.voltageV2;
  if (sensorId == "jsy_grid" && variable == "currentA1") return state.currentA1;
  if (sensorId == "jsy_grid" && variable == "currentA2") return state.currentA2;
  if (sensorId == "jsy_grid" && variable == "powerFactor1") return state.powerFactor1;
  if (sensorId == "jsy_grid" && variable == "powerFactor2") return state.powerFactor2;
  if (sensorId == "temp_tank_top" && variable == "temperature") return state.tankTopC;
  if (sensorId == "temp_tank_middle" && variable == "temperature") return state.tankMiddleC;
  if (sensorId == "temp_tank_bottom" && variable == "temperature") return state.tankBottomC;
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
    if (measure == "temperatureC") return state.tankTopC;
    if (measure == "available") return isnan(state.tankTopC) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.tankTopC) ? 4294967295.0f : 0;
  }
  if (source == "DS18B20_MIDDLE") {
    if (measure == "temperatureC") return state.tankMiddleC;
    if (measure == "available") return isnan(state.tankMiddleC) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.tankMiddleC) ? 4294967295.0f : 0;
  }
  if (source == "DS18B20_BOTTOM") {
    if (measure == "temperatureC") return state.tankBottomC;
    if (measure == "available") return isnan(state.tankBottomC) ? 0 : 1;
    if (measure == "lastValidReadAgeMs") return isnan(state.tankBottomC) ? 4294967295.0f : 0;
  }
  if (source == "battery") {
    if (measure == "voltageV") return state.batteryVoltageV;
    if (measure == "currentA") return state.batteryCurrentA;
    if (measure == "powerW") return state.batteryPowerW;
    if (measure == "socPct") return state.batterySocPct;
    if (measure == "available") return state.batteryOnline ? 1 : 0;
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
  if (source == "Redondance") {
    if (measure == "isActiveMaster") return state.isActiveMaster ? 1 : 0;
    if (measure == "epoch") return state.redundancyEpoch;
    if (measure == "lastHeartbeatAgeMs") return millis() - state.lastMasterHeartbeatMs;
  }
  return NAN;
}

String RuleEngine::textMeasureValue(const String &source, const String &measure) {
  if (source == "Systeme") {
    if (measure == "wifiStatus") return state.wifiConnected ? "CONNECTED" : (state.networkMode == "AP_FALLBACK" ? "AP_FALLBACK" : "DISCONNECTED");
    if (measure == "role") return RuntimeState::roleToString(state.role);
  }
  if (source == "Securite") {
    if (measure == "safetyLevel") return state.safetyLevel;
    if (measure == "safetyReason") return state.safetyReason;
  }
  if (source == "Redondance") {
    if (measure == "activeRole") return RuntimeState::roleToString(state.role);
    if (measure == "activeMasterId") return state.activeMasterId;
  }
  if (source == "TIC Linky" && measure == "tariff") return state.ticTariff;
  return "";
}

float RuleEngine::surplusRegulationPercent(JsonObject action) {
  String regulation = action["regulation"] | "PID";
  regulation.toUpperCase();
  if (regulation == "PROPORTIONAL") {
    float maxPowerW = action["maxHeaterPowerW"] | action["maxPowerW"] | config.system()["router"]["ssr1MaxW"] | 1500.0f;
    if (maxPowerW <= 1.0f) maxPowerW = 1500.0f;
    return constrain((state.surplusW / maxPowerW) * 100.0f, 0.0f, 100.0f);
  }
  return pid.computeSurplusPercent(action, millis());
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
