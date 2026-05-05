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
  float getGridPowerW() const { return reading.gridPowerW; }
  float getInjectionW() const { return reading.injectionW; }
  float getSurplusW() const { return reading.surplusW; }
  void printStatus();

private:
  ConfigManager &config;
  RuntimeState &state;
  JSYMK194TReading reading;

  uint8_t modbusAddress = 1;
  uint8_t rxPin = 16;
  uint8_t txPin = 17;
  uint32_t baudrate = 4800;
  uint32_t readIntervalMs = 500;
  uint32_t timeoutMs = 400;
  float minInjectionStartW = 200;
  float stopBelowInjectionW = 80;
  bool injectionActive = false;

  uint8_t rx[80]{};
  uint8_t rxLen = 0;
  uint8_t expectedLen = 0;
  bool waiting = false;
  uint32_t lastRequestMs = 0;
  uint32_t lastErrorLogMs = 0;

  void sendRequest();
  uint16_t crc16(const uint8_t *data, uint8_t len);
  uint32_t readU32(const uint8_t *data);
  void publishRuntime();
  void logError(const __FlashStringHelper *message, uint32_t now);
};
