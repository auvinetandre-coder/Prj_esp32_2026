#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../network/wifi_manager.h"
#include "../communication/espnow_manager.h"
#include "../sensors/sensor_manager.h"
#include "../actuators/actuator_manager.h"
#include "../logic/rule_engine.h"
#include "../safety/safety_manager.h"
#include "../status/status_led.h"
#include "../diagnostics/serial_debug.h"

class WebUi {
public:
  WebUi(ConfigManager &config, RuntimeState &state, SolarWiFiManager &wifi, EspNowManager &espnow,
        SensorManager &sensors, ActuatorManager &actuators, RuleEngine &rules, SafetyManager &safety,
        StatusLed &statusLed, SerialDebug &serialDebug)
      : config(config), state(state), wifi(wifi), espnow(espnow),
        sensors(sensors), actuators(actuators), rules(rules), safety(safety), statusLed(statusLed), serialDebug(serialDebug), server(80) {}
  void begin();
  void loop();

private:
  ConfigManager &config;
  RuntimeState &state;
  SolarWiFiManager &wifi;
  EspNowManager &espnow;
  SensorManager &sensors;
  ActuatorManager &actuators;
  RuleEngine &rules;
  SafetyManager &safety;
  StatusLed &statusLed;
  SerialDebug &serialDebug;
  WebServer server;

  void routes();
  void consoleRoutes();
  bool authorizeConsole();
  bool authEnabled();
  bool isAuthenticated();
  bool requireAuth();
  void sendJson(DynamicJsonDocument &doc);
  void sendConfig(const char *name);
  void saveConfig(const char *name, const char *path);
  void sendStatusLite();
  float gridPowerForGraph();
  void appendActuatorMeasurements(JsonArray items);
  void sendSystemInfo();
  void appendJsonNumber(String &out, float value, uint8_t decimals = 1);
  bool streamLittleFsFile(const char *path, const char *contentType);
  String fallbackStyleCss();
  void sendFsListJson();
  void appendFsListJson(String &out, const char *dirname);
  String fsPage();
  String homePage();
  String litePage();
};
