#pragma once

#include <Arduino.h>
#include "../runtime/runtime_state.h"

class StatusLed {
public:
  StatusLed(RuntimeState &state, uint8_t pin = 2) : state(state), pin(pin) {}
  void begin();
  void loop(uint32_t now);

private:
  RuntimeState &state;
  uint8_t pin;
  bool ledOn = false;
  uint32_t lastToggleMs = 0;
  uint8_t safetyStep = 0;

  void write(bool on);
};
