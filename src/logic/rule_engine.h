#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"
#include "pid_controller.h"

class RuleEngine {
public:
  enum MeasureType {
    MEASURE_NUMBER,
    MEASURE_BOOL,
    MEASURE_ENUM,
    MEASURE_TEXT
  };

  struct MeasureDef {
    const char *source;
    const char *measure;
    MeasureType type;
    const char *unit;
    const char *enumValues;
  };

  RuleEngine(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators, PIDController &pid)
      : config(config), state(state), actuators(actuators), pid(pid) {}
  void begin();
  void loop();
  void loop(uint32_t now);
  void loadRules();
  void evaluate(uint32_t now);
  void evaluateRules(uint32_t now);
  bool evaluateCondition(JsonObject condition);
  void executeAction(JsonObject action, const String &ruleName);
  void printRulesStatus();
  String validateRulesJson();
  bool validateRulesPayload(const String &payload, String &errorsJson);

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  PIDController &pid;
  static const uint8_t MAX_RULES = 24;
  String lastCommandKey[MAX_RULES];
  float lastCommandValue[MAX_RULES] = {NAN};
  uint8_t lastCommandCount = 0;

  float sensorValue(const String &sensorId, const String &variable);
  const MeasureDef *findMeasure(const String &source, const String &measure);
  bool validateCondition(JsonObject condition, JsonArray errors, const String &ruleId);
  bool validateRulesDocument(JsonDocument &doc, String &errorsJson);
  bool operatorAllowed(MeasureType type, const String &op);
  bool enumValueAllowed(const char *values, const String &value);
  float numericMeasureValue(const String &source, const String &measure);
  String textMeasureValue(const String &source, const String &measure);
  bool conditionMatches(JsonObject condition);
  bool timeWindowMatches(JsonObject condition);
  float surplusRegulationPercent(JsonObject action);
  void logCommandChange(const String &key, float value, const String &ruleName);
  JsonObject ruleAt(uint8_t index);
};
