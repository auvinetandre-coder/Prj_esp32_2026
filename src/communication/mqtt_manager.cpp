#include "mqtt_manager.h"
#include <ArduinoJson.h>

MqttManager *MqttManager::instance = nullptr;

void MqttManager::begin() {
  instance = this;
  mqtt.setCallback(MqttManager::onMessage);
  mqtt.setBufferSize(1024);
  loadConfig();
  state.mqttEnabled = enabled;
  state.mqttConnected = false;
  state.mqttStatus = enabled ? "MQTT_WAIT_WIFI" : "MQTT_DISABLED";
}

void MqttManager::loop(uint32_t now) {
  loadConfig();
  state.mqttEnabled = enabled;
  if (!enabled) {
    if (mqtt.connected()) mqtt.disconnect();
    state.mqttConnected = false;
    state.mqttStatus = "MQTT_DISABLED";
    wasConnected = false;
    return;
  }

  if (!state.wifiConnected) {
    if (mqtt.connected()) mqtt.disconnect();
    state.mqttConnected = false;
    state.mqttStatus = "MQTT_WAIT_WIFI";
    wasConnected = false;
    return;
  }

  connectIfNeeded(now);
  if (!mqtt.connected()) return;

  mqtt.loop();
  if (now - lastPublishMs >= publishIntervalMs) {
    lastPublishMs = now;
    publishNow();
  }
}

void MqttManager::publishNow() {
  if (!enabled || !mqtt.connected()) return;
  publishStateJson();
  if (publishIndividualTopics) publishIndividual();
  state.lastMqttPublishMs = millis();
}

void MqttManager::loadConfig() {
  JsonObject cfg = config.system()["mqtt"].as<JsonObject>();
  enabled = cfg["enabled"] | false;
  const char *base = cfg["baseTopic"] | "routeurSolaire";
  baseTopic = base && base[0] ? base : "routeurSolaire";
  baseTopic.trim();
  while (baseTopic.endsWith("/")) baseTopic.remove(baseTopic.length() - 1);

  publishIntervalMs = cfg["publishIntervalMs"] | 5000;
  if (publishIntervalMs < 1000) publishIntervalMs = 1000;
  retain = cfg["retain"] | false;
  publishIndividualTopics = cfg["publishIndividualTopics"] | true;

  JsonObject topics = cfg["topics"].as<JsonObject>();
  stateTopic = topics["state"] | topicPath("state");
  commandTopic = topics["command"] | topicPath("command");
  actuatorSetTopic = topics["actuatorSet"] | topicPath("actuator/+/set");
  availabilityTopic = topics["availability"] | topicPath("availability");
}

void MqttManager::connectIfNeeded(uint32_t now) {
  if (mqtt.connected()) {
    if (!wasConnected) {
      wasConnected = true;
      state.mqttConnected = true;
      state.mqttStatus = "MQTT_CONNECTED";
      publishText(availabilityTopic, "online");
      subscribeTopics();
      state.logEvent("INFO", "MQTT_CONNECTED", String("Broker MQTT connecte"), "MqttManager");
    }
    return;
  }

  state.mqttConnected = false;
  wasConnected = false;
  state.mqttStatus = "MQTT_DISCONNECTED";
  if (now - lastReconnectAttemptMs < 5000) return;
  lastReconnectAttemptMs = now;
  connect();
}

bool MqttManager::connect() {
  JsonObject cfg = config.system()["mqtt"].as<JsonObject>();
  const char *host = cfg["host"] | "192.168.0.48";
  uint16_t port = cfg["port"] | 1883;
  mqtt.setServer(host, port);

  String clientId = cfg["clientId"] | "";
  if (!clientId.length()) clientId = String("RouteurSolaireESP32-") + state.deviceId;
  clientId.replace(":", "");
  String user = cfg["username"] | "";
  String password = cfg["password"] | "";

  bool ok = false;
  if (user.length()) {
    ok = mqtt.connect(clientId.c_str(), user.c_str(), password.c_str(), availabilityTopic.c_str(), 0, retain, "offline");
  } else {
    ok = mqtt.connect(clientId.c_str(), availabilityTopic.c_str(), 0, retain, "offline");
  }

  state.mqttConnected = ok;
  state.mqttStatus = ok ? "MQTT_CONNECTED" : String("MQTT_ERROR_") + mqtt.state();
  if (!ok) state.logEvent("WARNING", "MQTT_CONNECT_FAIL", state.mqttStatus, "MqttManager");
  return ok;
}

void MqttManager::subscribeTopics() {
  mqtt.subscribe(commandTopic.c_str());
  mqtt.subscribe(actuatorSetTopic.c_str());
  mqtt.subscribe(topicPath("command/#").c_str());
  mqtt.subscribe(topicPath("actuator/+/set").c_str());
}

void MqttManager::publishStateJson() {
  DynamicJsonDocument doc(2048);
  doc["deviceId"] = state.deviceId;
  doc["moduleName"] = state.moduleName;
  doc["role"] = RuntimeState::roleToString(state.role);
  doc["safetyLevel"] = state.safetyLevel;
  doc["safetyReason"] = state.safetyReason;
  doc["simulation"] = state.simulationMode;
  doc["wifiRssi"] = state.rssi;
  doc["uptimeMs"] = millis();
  doc["gridPowerW"] = state.gridPowerW;
  doc["injectionW"] = state.injectionW;
  doc["consumptionW"] = state.consumptionW;
  doc["surplusW"] = state.surplusW;
  doc["voltageV"] = state.gridVoltageV;
  doc["currentA"] = state.gridCurrentA;
  doc["powerFactor"] = state.gridPowerFactor;
  doc["frequencyHz"] = state.gridFrequencyHz;
  doc["jsyOnline"] = state.jsyOnline;
  doc["ticAvailable"] = state.ticAvailable;
  JsonArray ds = doc["ds18b20"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; i++) {
    JsonObject s = ds.add<JsonObject>();
    s["id"] = String("sonde") + String(i + 1);
    s["temperatureC"] = state.ds18b20Temps[i];
    s["available"] = state.ds18b20Available[i];
  }
  JsonObject out = doc["outputs"].to<JsonObject>();
  out["ssr1PowerPct"] = state.ssr1PowerPct;
  out["ssr2PowerPct"] = state.ssr2PowerPct;
  out["robotDynPowerPct"] = state.robotDynPowerPct;

  char buffer[2048];
  size_t len = serializeJson(doc, buffer, sizeof(buffer));
  mqtt.publish(stateTopic.c_str(), reinterpret_cast<const uint8_t *>(buffer), len, retain);
}

void MqttManager::publishIndividual() {
  publishNumber(topicPath("gridPowerW"), state.gridPowerW);
  publishNumber(topicPath("injectionW"), state.injectionW);
  publishNumber(topicPath("consumptionW"), state.consumptionW);
  publishNumber(topicPath("surplusW"), state.surplusW);
  publishNumber(topicPath("voltageV"), state.gridVoltageV);
  publishNumber(topicPath("currentA"), state.gridCurrentA);
  publishNumber(topicPath("ssr1PowerPct"), state.ssr1PowerPct);
  publishNumber(topicPath("ssr2PowerPct"), state.ssr2PowerPct);
  publishNumber(topicPath("robotDynPowerPct"), state.robotDynPowerPct);
  publishText(topicPath("safetyLevel"), state.safetyLevel);
  publishText(topicPath("simulation"), state.simulationMode ? "1" : "0");
  for (uint8_t i = 0; i < 3; i++) {
    publishNumber(topicPath(String("ds18b20/sonde") + String(i + 1) + "/temperatureC"), state.ds18b20Temps[i]);
    publishText(topicPath(String("ds18b20/sonde") + String(i + 1) + "/available"), state.ds18b20Available[i] ? "1" : "0");
  }
}

void MqttManager::publishNumber(const String &topic, float value, uint8_t decimals) {
  if (isnan(value) || isinf(value)) {
    mqtt.publish(topic.c_str(), "null", retain);
    return;
  }
  mqtt.publish(topic.c_str(), String(value, static_cast<unsigned int>(decimals)).c_str(), retain);
}

void MqttManager::publishText(const String &topic, const String &value) {
  mqtt.publish(topic.c_str(), value.c_str(), retain);
}

void MqttManager::handleMessage(char *topicRaw, byte *payloadRaw, unsigned int length) {
  String topic(topicRaw);
  String payload;
  payload.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) payload += static_cast<char>(payloadRaw[i]);
  payload.trim();

  if (handleActuatorTopic(topic, payload)) return;
  if (topic == commandTopic || topicMatches(topic, topicPath("command/#"))) {
    handleJsonCommand(payload);
  }
}

bool MqttManager::handleJsonCommand(const String &payload) {
  DynamicJsonDocument doc(768);
  if (deserializeJson(doc, payload)) {
    state.logEvent("WARNING", "MQTT_COMMAND_REFUSED", "JSON commande MQTT invalide", "MqttManager");
    return false;
  }
  String actuatorId = doc["actuatorId"] | "";
  String command = doc["command"] | "setActuatorPercent";
  float value = doc["value"] | doc["commandPercent"] | 0.0f;
  if (!actuatorId.length()) return false;
  if (state.safetyTripped && command != "stop" && command != "off") {
    state.logEvent("WARNING", "MQTT_COMMAND_REFUSED", "Commande MQTT refusee: Safety CRITICAL", "MqttManager");
    return false;
  }
  bool ok = actuators.command(actuatorId, command, value);
  state.logEvent(ok ? "INFO" : "WARNING", ok ? "MQTT_COMMAND" : "MQTT_COMMAND_REFUSED",
                 String("MQTT ") + actuatorId + " " + command + " " + String(value, 1), "MqttManager");
  return ok;
}

bool MqttManager::handleActuatorTopic(const String &topic, const String &payload) {
  String prefix = topicPath("actuator/");
  String suffix = "/set";
  if (!topic.startsWith(prefix) || !topic.endsWith(suffix)) return false;
  String actuatorId = topic.substring(prefix.length(), topic.length() - suffix.length());
  if (!actuatorId.length()) return false;
  if (state.safetyTripped) {
    state.logEvent("WARNING", "MQTT_COMMAND_REFUSED", "Commande actionneur refusee: Safety CRITICAL", "MqttManager");
    return true;
  }
  float value = payload.toFloat();
  bool ok = actuators.command(actuatorId, "setActuatorPercent", value);
  state.logEvent(ok ? "INFO" : "WARNING", ok ? "MQTT_COMMAND" : "MQTT_COMMAND_REFUSED",
                 String("MQTT ") + actuatorId + " = " + String(value, 1) + "%", "MqttManager");
  return true;
}

bool MqttManager::topicMatches(const String &topic, const String &pattern) {
  if (pattern.endsWith("/#")) {
    String prefix = pattern.substring(0, pattern.length() - 1);
    return topic.startsWith(prefix);
  }
  return topic == pattern;
}

String MqttManager::topicPath(const String &suffix) const {
  String clean = suffix;
  while (clean.startsWith("/")) clean.remove(0, 1);
  return baseTopic + "/" + clean;
}

void MqttManager::onMessage(char *topic, byte *payload, unsigned int length) {
  if (instance) instance->handleMessage(topic, payload, length);
}
