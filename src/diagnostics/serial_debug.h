#pragma once

#include <Arduino.h>
#include "../runtime/runtime_state.h"
#include "../network/wifi_diagnostics.h"
class EspNowManager;
class SolarWiFiManager;
bool serialDebugQuery(const RuntimeState &state, const char *command, char *out, size_t capacity);

// Read-only, volatile diagnostics. Never changes configuration or outputs.
class SerialDebug {
public:
  explicit SerialDebug(RuntimeState &state) : state(state) {}
  void begin();
  void loop();
  bool executeCommand(const char *request, char *reply, size_t capacity);
  void setWifi(SolarWiFiManager *manager) { wifi = manager; }
  void setRadio(EspNowManager *manager) { radio = manager; }
  void observeModule(LoopPerfModule module, uint32_t elapsedMs);

private:
  RuntimeState &state;
  EspNowManager *radio = nullptr;
  SolarWiFiManager *wifi = nullptr;
  WifiDiagnosticSnapshot wifiSnapshot, wifiLastConnected;
  bool wifiLastConnectedValid = false;
  WifiDiagnosticEvent wifiEvents[32]{};
  uint8_t wifiEventCount = 0;
  const char *wifiStorageStatus = "NON_INITIALISE";
  bool wifiSavePending = false;
  uint32_t wifiTotal = 0, wifiBootId = 0, wifiLastConnectedBoot = 0;
  bool enabled = false;
  bool droppingInput = false;
  char input[48]{};
  uint8_t inputLength = 0;
  char output[192]{};
  uint16_t outputLength = 0, outputOffset = 0;
  int16_t reportLine = -1;
  uint32_t lastReportMs = 0;
  enum ReportKind : uint8_t { SUMMARY, EVENTS, PEERS, WIFI_STATE, WIFI_HISTORY };
  uint8_t reportKind = SUMMARY;
  struct Trace { uint32_t ms; uint8_t kind; int32_t value; uint32_t detail; uint64_t epochMs; };
  static constexpr uint8_t traceCapacity = 32;
  Trace traces[traceCapacity]{}, traceSnapshot[traceCapacity]{};
  uint8_t traceHead = 0, traceCount = 0, snapshotCount = 0;
  uint32_t recentModuleMax[PERF_COUNT]{}, reportModuleMax[PERF_COUNT]{};
  uint32_t lastSlowTraceMs[PERF_COUNT]{};
  uint32_t lastObserveMs = 0, previousMqttAttempts = 0, previousDisconnects = 0;
  int previousWifiStatus = -1, previousChannel = -1, previousJsy = -1, previousTic = -1;
  void observe(uint32_t now);
  void trace(uint8_t kind, int32_t value, uint32_t detail);
  void snapshotEvents();
  void command();
  void queue(const char *format, ...);
  void report(uint32_t now);
};
