#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

class DisplayManager {
public:
  DisplayManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}

  void begin();
  void loop(uint32_t now);

private:
  ConfigManager &config;
  RuntimeState &state;

  bool enabled = true;
  bool ready = false;
  uint8_t page = 0;
  uint32_t lastRefreshMs = 0;
  uint32_t refreshIntervalMs = 4000;
  int pinSclk = 18;
  int pinMosi = 19;
  int pinReset = 16;
  int pinDc = 4;
  int pinCs = 15;

  void loadConfig();
  void oledInit();
  void oledReset();
  void command(uint8_t value);
  void data(uint8_t value);
  void setWindow(uint8_t colStart, uint8_t colEnd, uint8_t pageStart, uint8_t pageEnd);
  void clear();
  void drawPixel(uint8_t x, uint8_t y, bool on);
  void drawChar(uint8_t x, uint8_t y, char c);
  void drawText(uint8_t x, uint8_t y, const String &text);
  void drawHeader(const String &title);
  void render();
  void renderOverview();
  void renderPower();
  void renderTemperatures();
  void renderOutputs();
  void glyphFor(char c, uint8_t out[5]);
  String fmtFloat(float value, uint8_t decimals = 0);
};
