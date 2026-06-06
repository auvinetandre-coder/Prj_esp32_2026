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
#include "../status/status_led.h"

class WebUi {
public:
  WebUi(ConfigManager &config, RuntimeState &state, SolarWiFiManager &wifi, EspNowManager &espnow,
        RedundancyManager &redundancy, SensorManager &sensors, ActuatorManager &actuators, RuleEngine &rules, SafetyManager &safety,
        SimulationManager &simulation, StatusLed &statusLed)
      : config(config), state(state), wifi(wifi), espnow(espnow), redundancy(redundancy),
        sensors(sensors), actuators(actuators), rules(rules), safety(safety), simulation(simulation), statusLed(statusLed), server(80) {}
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
  StatusLed &statusLed;
  WebServer server;
  bool littleFsOtaBackupOk = false;

  void routes();
  bool authEnabled();
  bool isAuthenticated();
  bool requireAuth();
  void sendJson(DynamicJsonDocument &doc);
  void sendConfig(const char *name);
  void saveConfig(const char *name, const char *path);
  void sendStatusLite();
  void sendSystemInfo();
  void appendJsonNumber(String &out, float value, uint8_t decimals = 1);
  bool streamLittleFsFile(const char *path, const char *contentType);
  bool downloadGithubAssetToUpdate(const String &url, int updateCommand, const char *logCode, String &error, size_t &written, int &httpCode);
  void sendGithubOtaCheck();
  void startGithubFirmwareOta();
  void startGithubLittleFsOta();
  String fallbackStyleCss();
  void sendFsListJson();
  void appendFsListJson(String &out, const char *dirname);
  String fsPage();
  String homePage();
  String litePage();
};
