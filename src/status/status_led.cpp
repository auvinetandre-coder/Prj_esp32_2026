#include "status_led.h"

void StatusLed::begin() {
  pinMode(pin, OUTPUT);
  write(false);
}

void StatusLed::loop(uint32_t now) {
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

void StatusLed::write(bool on) {
  ledOn = on;
  digitalWrite(pin, on ? HIGH : LOW);
}
