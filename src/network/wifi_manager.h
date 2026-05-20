#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

class SolarWiFiManager {
public:
  SolarWiFiManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}
  void begin();
  void loop();
  bool testConnection(const String &ssid, const String &password, uint32_t timeoutMs = 10000);
  void saveCredentials(const String &ssid, const String &password);
  void restart() { ESP.restart(); }

private:
  ConfigManager &config;
  RuntimeState &state;
  uint32_t lastStatusMs = 0;
  uint32_t lastNtpCheckMs = 0;
  bool ntpStarted = false;
  void startFallbackAp();
  void startLocalAp();
  void updateNtp(uint32_t now);
};
