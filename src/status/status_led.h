#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

class StatusLed {
public:
  StatusLed(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}
  void begin();
  void loop(uint32_t now);
  void identify(uint32_t now, uint32_t durationMs = 120000UL);

private:
  ConfigManager &config;
  RuntimeState &state;
  int8_t bluePin = 2;
  int8_t redPin = -1;
  bool activeHigh = true;
  bool ledOn = false;
  uint32_t lastToggleMs = 0;
  uint32_t identifyUntilMs = 0;
  uint8_t safetyStep = 0;

  void loadConfig();
  void write(bool on);
};
