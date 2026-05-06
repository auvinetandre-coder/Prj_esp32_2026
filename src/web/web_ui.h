#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../network/wifi_manager.h"
#include "../communication/espnow_manager.h"
#include "../communication/redundancy_manager.h"
#include "../sensors/sensor_manager.h"
#include "../actuators/actuator_manager.h"
#include "../logic/rule_engine.h"
#include "../safety/safety_manager.h"
#include "../simulation/simulation_manager.h"

class WebUi {
public:
  WebUi(ConfigManager &config, RuntimeState &state, SolarWiFiManager &wifi, EspNowManager &espnow,
        RedundancyManager &redundancy, SensorManager &sensors, ActuatorManager &actuators, RuleEngine &rules, SafetyManager &safety,
        SimulationManager &simulation)
      : config(config), state(state), wifi(wifi), espnow(espnow), redundancy(redundancy),
        sensors(sensors), actuators(actuators), rules(rules), safety(safety), simulation(simulation), server(80) {}
  void begin();
  void loop();

private:
  ConfigManager &config;
  RuntimeState &state;
  SolarWiFiManager &wifi;
  EspNowManager &espnow;
  RedundancyManager &redundancy;
  SensorManager &sensors;
  ActuatorManager &actuators;
  RuleEngine &rules;
  SafetyManager &safety;
  SimulationManager &simulation;
  WebServer server;

  void routes();
  void sendJson(DynamicJsonDocument &doc);
  void sendConfig(const char *name);
  void saveConfig(const char *name, const char *path);
  void sendStatusLite();
  void sendSystemInfo();
  void appendJsonNumber(String &out, float value, uint8_t decimals = 1);
  bool streamLittleFsFile(const char *path, const char *contentType);
  String fallbackStyleCss();
  void sendFsListJson();
  void appendFsListJson(String &out, const char *dirname);
  String fsPage();
  String homePage();
  String page();
  String litePage();
};
