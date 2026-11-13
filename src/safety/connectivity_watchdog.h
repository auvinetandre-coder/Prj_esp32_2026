#pragma once
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../communication/espnow_manager.h"
#include "../actuators/actuator_manager.h"

class ConnectivityWatchdog {
public:
  ConnectivityWatchdog(ConfigManager &config, RuntimeState &state, EspNowManager &radio, ActuatorManager &actuators)
      : config(config), state(state), radio(radio), actuators(actuators) {}
  void begin();
  void loop();
private:
  ConfigManager &config;
  RuntimeState &state;
  EspNowManager &radio;
  ActuatorManager &actuators;
  uint32_t startedMs = 0, lastCheckMs = 0, wifiDownSinceMs = 0;
  uint32_t lastPeerSignature = 0;
  bool wifiDown = false;
};
