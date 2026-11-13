#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "../communication/espnow_manager.h"
#include "shared_actuator_control.h"
#include "ssr_frame.h"

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
  bool hasEspNowCommand(const String &id);
  float acceptedPercent(const String &id);
  bool requestSharedControl(const String &id, const String &mode, float value);
  void setPowerWatts(const String &actuatorId, float watts);
  float powerWattsToPercent(const String &actuatorId, float watts, float maxPowerOverrideW = 0.0f);
  void setMode(const String &id, const String &mode);
  void lockAllForSafety(const String &reason);
  void unlockSafety();
  void stopCritical();
  bool criticalControlAllowed();
  void runtimeToJson(JsonArray out);
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
    bool activeHigh = true;
    char mode[24];
    float commandPercent = 0;
    bool currentState = false;
    float maxPowerW = 1000;
    uint32_t cycleMs = 1000;
    uint32_t cycleStartMs = 0;
    uint32_t lastCommandMs = 0;
    uint32_t espNowLeaseMs = 0;
    SharedActuatorControl sharedControl;
    uint32_t controlIssuedMs = 0;
    bool remoteNodeLost = false;
    bool safetyLocked = false;
  };

  static const uint8_t MAX_CHANNELS = 16;
  struct TimedSsr {
    int pin = -1;
    bool activeHigh = true;
    bool on = false;
    uint8_t step = 0;
    SsrFrame frame{0, 1};
    SsrFrame requested{0, 1};
    SsrHalfWave halfWave;
    bool demiSinus = false;
    uint8_t halfWavePercent = 0;
    int64_t expiresUs = 0;
  };
  TimedSsr timedSsr[MAX_CHANNELS];
  uint32_t ssrRevision[MAX_CHANNELS]{};
  portMUX_TYPE ssrMux = portMUX_INITIALIZER_UNLOCKED;
  hw_timer_t *ssrTimer = nullptr;
  esp_timer_handle_t ssrWatchdog = nullptr;
  int64_t ssrLastTickUs = 0;
  uint32_t ssrMissedSteps = 0;
  uint32_t ssrMaxIntervalUs = 0;
  bool ssrTimingFault = false;
  bool ssrFaultLogged = false;
  void startSsrTimer();
  void limitTimedSsrLease(Actuator *channel);
  void stopTimedSsr(Actuator *channel = nullptr);
  bool updateTimedSsr(Actuator *channel, bool demiSinus);
  static void checkSsrTiming(void *context);
  static void IRAM_ATTR onSsrTick(void *context);

  ConfigManager &config;
  RuntimeState &state;
  EspNowManager &espnow;
  Actuator channels[MAX_CHANNELS];
  uint8_t channelCount = 0;
  bool safetyLocked = false;
  String safetyLockReason = "";
  uint32_t commandTimeoutMs = 5000;
  uint32_t lastStatePublishMs = 0;

  int triacControlPin = -1;
  int triacZeroCrossPin = -1;
  volatile float triacTargetPct = 0;
  volatile bool triacPhaseEnabled = false;
  volatile bool triacZeroCrossPending = false;
  volatile uint64_t triacDelayUs = 0;
  esp_timer_handle_t triacFireTimer = nullptr;
  esp_timer_handle_t triacOffTimer = nullptr;

  Actuator *channelFor(const String &id);
  Actuator *channelForExisting(const String &id);
  bool findActuator(const String &id, JsonObject &out);
  void syncChannelFromConfig(Actuator *channel, JsonObject actuator);
  void applyLocal(JsonObject actuator, uint32_t now);
  void setOutput(Actuator *channel, int pin, bool on, bool writePin = true);
  uint32_t configuredCycleMs(JsonObject actuator);
  String normalizeMode(const String &mode, const String &type);
  bool modeIsSSR(const String &mode);
  bool modeIsRobotDynPhase(const String &mode);
  void publishPower(const String &id, float percent);
  static bool handleEspNowActuatorCommand(void *context, const String &actuatorId, const String &command, float value, const String &mode, uint32_t ttlMs, const String &peer, uint32_t token);
  void observeSharedControl(Actuator *channel, const EspNowActuatorState &reported);
  void setupRobotDyn(JsonObject actuator);
  void scheduleTriacFire();
  void fireTriacGate();
  void stopTriacGate();

  static ActuatorManager *instance;
  static void IRAM_ATTR onZeroCrossIsr();
  static void onTriacFireTimer(void *arg);
  static void onTriacOffTimer(void *arg);
};
