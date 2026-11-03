#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

struct JSYMK194TReading {
  float voltageV = NAN;
  float currentA = NAN;
  float activePowerW = NAN;
  float gridPowerW = NAN;
  float injectionW = 0;
  float consumptionW = 0;
  float surplusW = 0;
  float powerFactor = NAN;
  float frequencyHz = NAN;
  String energyDirection = "unknown";
  bool available = false;
  uint32_t lastValidReadMs = 0;
  uint16_t errorCount = 0;
};

class JSYMK194TManager {
public:
  JSYMK194TManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}

  void begin();
  void loop();
  void loop(uint32_t now);
  void readFrame(uint32_t now);
  bool parseFrame(uint32_t now);
  void calculatePowerDirection();
  bool isAvailable() const { return reading.available; }
  uint32_t powerSampleMs() const { return reading.lastValidReadMs; }
  float getGridPowerW() const { return reading.gridPowerW; }
  float getInjectionW() const { return reading.injectionW; }
  float getSurplusW() const { return reading.surplusW; }
  void reloadConfig();
  void stop();
  void printStatus();
  float channelValue(uint8_t index, const String &metric) const;
  bool reconfigureTo19200();

private:
  ConfigManager &config;
  RuntimeState &state;
  JSYMK194TReading reading;
  float channelPower[2] = {NAN, NAN};
  float channelMeasuredPower[2] = {NAN, NAN};
  float channelCurrent[2] = {NAN, NAN};
  float channelPf[2] = {NAN, NAN};
  float channelEnergy[2] = {NAN, NAN};

  uint8_t modbusAddress = 1;
  uint8_t rxPin = 26;
  uint8_t txPin = 27;
  uint32_t baudrate = 19200;
  uint32_t readIntervalMs = 350;
  uint32_t timeoutMs = 300;
  bool enabled = true;
  float minInjectionStartW = 200;
  float stopBelowInjectionW = 80;
  bool injectionActive = false;
  String clamp1Role = "production";
  String clamp2Role = "grid";
  String lastError = "";
  uint16_t timeoutCount = 0;
  uint16_t crcErrorCount = 0;

  uint8_t rx[80]{};
  uint8_t rxLen = 0;
  uint8_t expectedLen = 0;
  bool waiting = false;
  uint32_t lastRequestMs = 0;
  uint32_t lastErrorLogMs = 0;
  uint32_t lastAnomalyLogMs = 0;

  void configureFromJson();
  void autoDetectBaudrate();
  bool testReadAtBaud(uint32_t testBaudrate);
  bool writeBaudrate19200At4800();
  bool waitForValidFrame(uint32_t waitMs);
  void beginSerial();
  void sendRequest();
  uint16_t crc16(const uint8_t *data, uint8_t len);
  uint32_t readU32(const uint8_t *data);
  void publishRuntime();
  void logError(const __FlashStringHelper *message, uint32_t now);
};
