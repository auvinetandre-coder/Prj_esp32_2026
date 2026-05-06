#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../communication/espnow_manager.h"

class ActuatorManager {
public:
  ActuatorManager(ConfigManager &config, RuntimeState &state, EspNowManager &espnow)
      : config(config), state(state), espnow(espnow) {}
  void begin();
  void loop(uint32_t now);
  void forceAllOff();
  void allOff();
  bool command(const String &actuatorId, const String &command, float value = 0);
  void setPower(const String &actuatorId, float percent);
  void setCommandPercent(const String &id, float percent);
  void setPowerWatts(const String &actuatorId, float watts);
  void setMode(const String &id, const String &mode);
  void lockAllForSafety(const String &reason);
  void unlockSafety();
  void stopCritical();
  bool criticalControlAllowed();
  void updateSSR(JsonObject actuator, uint32_t now);
  void updateRobotDyn(JsonObject actuator, uint32_t now);
  void printStatus();

private:
  struct Actuator {
    char id[32];
    char name[48];
    char type[24];
    int gpio = -1;
    int gpioZeroCross = -1;
    int gpioControl = -1;
    bool enabled = false;
    char mode[24];
    float commandPercent = 0;
    bool currentState = false;
    float maxPowerW = 1000;
    uint32_t cycleMs = 1000;
    uint32_t cycleStartMs = 0;
    uint32_t lastCommandMs = 0;
    bool safetyLocked = false;
  };

  static const uint8_t MAX_CHANNELS = 16;
  ConfigManager &config;
  RuntimeState &state;
  EspNowManager &espnow;
  Actuator channels[MAX_CHANNELS];
  uint8_t channelCount = 0;
  bool safetyLocked = false;
  String safetyLockReason = "";
  uint32_t commandTimeoutMs = 5000;
  bool simulationMode = false;
  bool previousSimulationMode = false;
  uint32_t realModeHoldUntilMs = 0;
  bool simOutputEnabled = true;
  int simLedSsr1Pin = 18;
  int simLedSsr2Pin = 19;
  int simLedTriacPin = 21;
  uint32_t simVisualCycleMs = 1000;

  int triacControlPin = -1;
  int triacZeroCrossPin = -1;
  volatile float triacTargetPct = 0;
  volatile bool triacPhaseEnabled = false;
  volatile bool triacZeroCrossPending = false;
  volatile uint64_t triacDelayUs = 0;
  esp_timer_handle_t triacFireTimer = nullptr;
  esp_timer_handle_t triacOffTimer = nullptr;

  Actuator *channelFor(const String &id);
  bool findActuator(const String &id, JsonObject &out);
  void syncChannelFromConfig(Actuator *channel, JsonObject actuator);
  void applyLocal(JsonObject actuator, uint32_t now);
  void setOutput(Actuator *channel, int pin, bool on);
  uint32_t configuredCycleMs(JsonObject actuator);
  String normalizeMode(const String &mode, const String &type);
  bool modeIsSSR(const String &mode);
  bool modeIsRobotDynPhase(const String &mode);
  void publishPower(const String &id, float percent);
  void setupSimulationOutputs();
  static bool handleEspNowActuatorCommand(void *context, const String &actuatorId, const String &command, float value, const String &mode);
  void updateSimulationOutput(const String &id, float percent, uint32_t now);
  void allSimulationOutputsOff();
  void setupRobotDyn(JsonObject actuator);
  void scheduleTriacFire();
  void fireTriacGate();
  void stopTriacGate();

  static ActuatorManager *instance;
  static void IRAM_ATTR onZeroCrossIsr();
  static void onTriacFireTimer(void *arg);
  static void onTriacOffTimer(void *arg);
};
