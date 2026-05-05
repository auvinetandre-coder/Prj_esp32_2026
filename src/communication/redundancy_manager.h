#pragma once

#include <Arduino.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "espnow_manager.h"

enum RedundancyState {
  REDUNDANCY_PASSIVE,
  REDUNDANCY_ACTIVE_MASTER,
  REDUNDANCY_BACKUP_READY,
  REDUNDANCY_TAKEOVER_PENDING,
  REDUNDANCY_TAKEOVER_ACTIVE,
  REDUNDANCY_SPLIT_BRAIN_DETECTED
};

class RedundancyManager {
public:
  RedundancyManager(ConfigManager &config, RuntimeState &state, EspNowManager &espnow)
      : config(config), state(state), espnow(espnow) {}
  void begin();
  void loop();
  void loop(uint32_t now);
  void onHeartbeatReceived(const String &masterId, uint32_t epoch);
  void sendHeartbeatIfNeeded(uint32_t now);
  void checkMasterTimeout(uint32_t now);
  void becomeActiveMaster();
  bool detectSplitBrain();
  bool isActiveMaster() const { return currentState == REDUNDANCY_ACTIVE_MASTER || currentState == REDUNDANCY_TAKEOVER_ACTIVE; }
  String getActiveMasterId() const { return state.activeMasterId; }
  uint32_t getEpoch() const { return state.redundancyEpoch; }
  void printStatus();

  // Compatibilite avec les anciens appels du firmware.
  void sendHeartbeat();
  void receiveHeartbeat();
  void checkMasterAlive(uint32_t now);
  void becomeMaster();
  void becomeBackup();
  void syncConfig();
  void syncRuntimeState();

private:
  ConfigManager &config;
  RuntimeState &state;
  EspNowManager &espnow;
  RedundancyState currentState = REDUNDANCY_PASSIVE;
  uint32_t heartbeatIntervalMs = 300;
  uint32_t takeoverTimeoutMs = 1000;
  uint32_t lastHeartbeatSentMs = 0;
  uint32_t takeoverPendingSinceMs = 0;

  void setState(RedundancyState next, const String &reason = "");
  const char *stateText(RedundancyState value) const;
};
