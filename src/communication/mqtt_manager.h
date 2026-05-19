#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../actuators/actuator_manager.h"

class MqttManager {
public:
  MqttManager(ConfigManager &config, RuntimeState &state, ActuatorManager &actuators)
      : config(config), state(state), actuators(actuators), mqtt(wifiClient) {}

  void begin();
  void loop(uint32_t now);
  void publishNow();

private:
  ConfigManager &config;
  RuntimeState &state;
  ActuatorManager &actuators;
  WiFiClient wifiClient;
  PubSubClient mqtt;

  bool enabled = false;
  bool wasConnected = false;
  uint32_t lastReconnectAttemptMs = 0;
  uint32_t lastPublishMs = 0;
  uint32_t publishIntervalMs = 5000;
  String baseTopic = "routeurSolaire";
  String commandTopic = "routeurSolaire/command";
  String actuatorSetTopic = "routeurSolaire/actuator/+/set";
  String availabilityTopic = "routeurSolaire/availability";
  String stateTopic = "routeurSolaire/state";
  bool publishIndividualTopics = true;
  bool retain = false;

  void loadConfig();
  void connectIfNeeded(uint32_t now);
  bool connect();
  void subscribeTopics();
  void publishStateJson();
  void publishIndividual();
  void publishNumber(const String &topic, float value, uint8_t decimals = 1);
  void publishText(const String &topic, const String &value);
  void handleMessage(char *topic, byte *payload, unsigned int length);
  bool handleJsonCommand(const String &payload);
  bool handleActuatorTopic(const String &topic, const String &payload);
  bool topicMatches(const String &topic, const String &pattern);
  String topicPath(const String &suffix) const;

  static MqttManager *instance;
  static void onMessage(char *topic, byte *payload, unsigned int length);
};
