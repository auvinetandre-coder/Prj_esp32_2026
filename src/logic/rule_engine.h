#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"
#include "pid_controller.h"

class RuleEngine {
public:
  enum ConditionKind {
    CONDITION_SENSOR,
    CONDITION_SYSTEM,
    CONDITION_TIME,
    CONDITION_CALENDAR,
    CONDITION_VIRTUAL,
    CONDITION_GROUP
  };

  enum ActionKind {
    ACTION_NONE,
    ACTION_AUTOMATE_STATE,
    ACTION_ACTUATOR_COMMAND,
    ACTION_LOG,
    ACTION_NOTIFICATION
  };

  struct Condition {
    ConditionKind kind = CONDITION_SENSOR;
    String source;
    String measure;
    String op = "==";
    String textValue;
    float numberValue = 0.0f;
    bool boolValue = false;
    String logic = "AND";
    uint8_t nestedLevel = 0;
  };

  struct Action {
    ActionKind kind = ACTION_NONE;
    String actuatorId;
    String command;
    String mode;
    String message;
    float value = 0.0f;
  };

  struct Rule {
    String id;
    String name;
    bool enabled = false;
    int priority = 0;
    Condition condition;
    Action thenAction;
    Action elseAction;
  };

  struct RuleDecision {
    bool matched = false;
    bool elseBranch = false;
    bool hasCommand = false;
    String ruleId = "";
    String ruleName = "";
    String actuatorId = "";
    String command = "";
    String mode = "";
    float commandPercent = 0.0f;
    int priority = 0;
    String requestedState = "";
    String reason = "";
  };

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
  RuleDecision evaluateDecision(uint32_t now);
  const RuleDecision &decision() const { return lastDecision; }
  uint8_t ruleCount() const { return lastRuleCount; }
  String activeRuleName() const { return lastDecision.ruleName; }
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
  RuleDecision lastDecision;
  uint8_t lastRuleCount = 0;

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
  bool actionToDecision(JsonObject action, const String &ruleName, RuleDecision &decision);
  void logCommandChange(const String &key, float value, const String &ruleName);
  JsonObject ruleAt(uint8_t index);
};
