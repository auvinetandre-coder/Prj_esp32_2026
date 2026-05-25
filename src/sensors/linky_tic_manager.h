#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

enum LinkyTICStatus {
  TIC_OK,
  TIC_TIMEOUT,
  TIC_FRAME_ERROR,
  TIC_NOT_CONFIGURED
};

struct LinkyTICReading {
  float apparentPowerVA = NAN;
  float gridPowerW = NAN;
  float currentA = NAN;
  uint64_t energyWh = 0;
  String tariff = "";
  String period = "";
  String lastFrame = "";
  bool available = false;
  uint32_t lastValidReadMs = 0;
  uint16_t errorCount = 0;
  LinkyTICStatus status = TIC_NOT_CONFIGURED;
};

class LinkyTICManager {
public:
  LinkyTICManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}

  void begin();
  void loop();
  void loop(uint32_t now);
  bool parseLine(const String &line);
  bool parseFrame();
  bool isAvailable() const { return reading.available; }
  float getApparentPowerVA() const { return reading.apparentPowerVA; }
  float getCurrentA() const { return reading.currentA; }
  String getTariff() const { return reading.tariff; }
  uint32_t getLastValidReadMs() const { return reading.lastValidReadMs; }
  void reloadConfig();
  void setConfigured(bool value);
  void stop();
  void printStatus();

private:
  ConfigManager &config;
  RuntimeState &state;
  LinkyTICReading reading;

  bool configured = true;
  String mode = "historique";
  uint8_t rxPin = 26;
  int8_t txPin = 27;
  uint32_t baudrate = 1200;
  uint32_t timeoutMs = 5000;
  bool debugEnabled = false;
  bool frameActive = false;
  bool frameHasValidLine = false;
  bool lineOverflow = false;
  String lineBuffer = "";
  String frameBuffer = "";
  uint32_t lastByteMs = 0;
  uint32_t lastErrorLogMs = 0;
  uint32_t lastInvalidLineLogMs = 0;
  uint32_t lastPeriodicLogMs = 0;
  uint32_t lastLabelTraceLogMs = 0;
  String decodedTrace = "";

  void loadConfig();
  bool validateChecksum(const String &line);
  void applyLabelValue(const String &label, const String &value);
  void traceDecodedLabel(const String &label, const String &value, const String &timestamp);
  void flushDecodedTrace(uint32_t now, bool force = false);
  void publishRuntime();
  void setStatus(LinkyTICStatus status);
  const char *statusText(LinkyTICStatus status) const;
  void logError(const __FlashStringHelper *message, uint32_t now);
  void logInvalidLine(const String &line);
  void logPeriodicValues(uint32_t now);
};
