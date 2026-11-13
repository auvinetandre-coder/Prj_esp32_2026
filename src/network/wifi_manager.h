#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "wifi_diagnostics.h"
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

class SolarWiFiManager {
public:
  SolarWiFiManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}
  void begin();
  void loop();
  bool testConnection(const String &ssid, const String &password, uint32_t timeoutMs = 10000);
  void saveCredentials(const String &ssid, const String &password);
  static constexpr uint8_t diagnosticCapacity = 32;
  const char *diagnosticStorageStatus() const { return persistenceStatus; }
  bool diagnosticPending() const { return diagnosticTotal != persistedTotal; }
  uint32_t diagnosticBoot() const { return diagnosticBootId; }
  uint32_t lastConnectedBoot() const { return diagnosticLastConnectedBoot; }
  uint8_t copyDiagnostics(WifiDiagnosticSnapshot &snapshot, WifiDiagnosticEvent *events, uint8_t limit, uint32_t &total);
  bool copyLastConnected(WifiDiagnosticSnapshot &snapshot);
  void diagnosticsToJson(JsonObject out);
  static const char *diagnosticEventName(uint16_t event);
  static const char *diagnosticReasonName(uint16_t reason);
  void restart() { ESP.restart(); }

private:
  ConfigManager &config;
  RuntimeState &state;
  uint32_t lastStatusMs = 0;
  uint32_t lastNtpCheckMs = 0;
  bool ntpStarted = false;
  bool diagnosticEventsRegistered = false;
  portMUX_TYPE diagnosticMux = portMUX_INITIALIZER_UNLOCKED;
  WifiDiagnosticSnapshot diagnosticState, lastConnectedState;
  bool lastConnectedAvailable = false;
  WifiDiagnosticEvent diagnosticEvents[diagnosticCapacity]{};
  uint8_t diagnosticHead = 0, diagnosticCount = 0;
  uint32_t diagnosticTotal = 0, lastDiagnosticSampleMs = 0;
  bool diagnosticSampled = false;
  uint32_t diagnosticBootId = 1, diagnosticLastConnectedBoot = 0;
  uint32_t persistedGeneration = 0, persistedTotal = 0, lastPersistMs = 0;
  bool persistenceAttempted = false, persistenceAvailable = false;
  int8_t persistedSlot = -1;
  const char *persistenceStatus = "NON_INITIALISE";
  WifiDiagnosticSnapshot previousBootState;
  bool previousBootAvailable = false;
  uint32_t previousBootId = 0;
  void beginDiagnosticsPersistence();
  void persistDiagnostics(uint32_t now);
  void recordDiagnosticEvent(WiFiEvent_t event, const WiFiEventInfo_t &info);
  void sampleDiagnostics(uint32_t now);
  void startFallbackAp();
  void startLocalAp();
  void updateNtp(uint32_t now);
};
