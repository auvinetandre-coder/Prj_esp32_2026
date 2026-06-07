#include "status_led.h"

void StatusLed::begin() {
  loadConfig();
  if (bluePin >= 0) pinMode(bluePin, OUTPUT);
  if (redPin >= 0 && redPin != bluePin) pinMode(redPin, OUTPUT);
  write(false);
}

void StatusLed::identify(uint32_t now, uint32_t durationMs) {
  identifyUntilMs = now + durationMs;
  lastToggleMs = 0;
  write(false);
}

void StatusLed::loop(uint32_t now) {
  if (identifyUntilMs && static_cast<int32_t>(identifyUntilMs - now) > 0) {
    if (now - lastToggleMs >= 120) {
      lastToggleMs = now;
      write(!ledOn);
    }
    return;
  }
  if (identifyUntilMs) {
    identifyUntilMs = 0;
    write(false);
  }

  if (state.safetyTripped) {
    const uint16_t pattern[] = {120, 120, 120, 900};
    if (now - lastToggleMs >= pattern[safetyStep]) {
      lastToggleMs = now;
      safetyStep = (safetyStep + 1) % 4;
      write(safetyStep == 1 || safetyStep == 3);
    }
    return;
  }

  safetyStep = 0;

  if (state.networkMode == "STATION" && state.wifiConnected) {
    write(true);
    return;
  }

  uint16_t interval = 700;
  if (state.networkMode == "STA_CONNECTING") interval = 150;
  if (state.networkMode == "AP_FALLBACK") interval = 700;

  if (now - lastToggleMs >= interval) {
    lastToggleMs = now;
    write(!ledOn);
  }
}

void StatusLed::loadConfig() {
  JsonObject led = config.system()["statusLed"].as<JsonObject>();
  bluePin = led["bluePin"].is<int>() ? (led["bluePin"] | 2) : (led["pin"] | 2);
  redPin = led["redPin"] | -1;
  activeHigh = led["activeHigh"] | true;
}

void StatusLed::write(bool on) {
  ledOn = on;
  const uint8_t level = (on == activeHigh) ? HIGH : LOW;
  if (bluePin >= 0) digitalWrite(bluePin, level);
  if (redPin >= 0 && redPin != bluePin) digitalWrite(redPin, level);
}
