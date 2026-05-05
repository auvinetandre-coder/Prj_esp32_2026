#include "redundancy_manager.h"

void RedundancyManager::begin() {
  heartbeatIntervalMs = config.system()["heartbeatIntervalMs"] | 300;
  takeoverTimeoutMs = config.system()["takeoverTimeoutMs"] | 1000;

  if (state.role == ROLE_MASTER) {
    state.isActiveMaster = true;
    state.masterAlive = true;
    state.activeMasterId = state.deviceId;
    state.redundancyEpoch = max<uint32_t>(state.redundancyEpoch, 1);
    setState(REDUNDANCY_ACTIVE_MASTER, "MASTER local actif");
  } else if (state.role == ROLE_BACKUP) {
    state.isActiveMaster = false;
    state.masterAlive = false;
    setState(REDUNDANCY_BACKUP_READY, "BACKUP pret");
  } else {
    state.isActiveMaster = false;
    state.masterAlive = false;
    setState(REDUNDANCY_PASSIVE, "Noeud passif");
  }
}

void RedundancyManager::loop() {
  loop(millis());
}

void RedundancyManager::loop(uint32_t now) {
  heartbeatIntervalMs = config.system()["heartbeatIntervalMs"] | heartbeatIntervalMs;
  takeoverTimeoutMs = config.system()["takeoverTimeoutMs"] | takeoverTimeoutMs;
  sendHeartbeatIfNeeded(now);
  checkMasterTimeout(now);
  detectSplitBrain();
}

void RedundancyManager::onHeartbeatReceived(const String &masterId, uint32_t epoch) {
  const uint32_t now = millis();
  if (masterId.length() && state.activeMasterId.length() && masterId != state.activeMasterId && state.isActiveMaster) {
    setState(REDUNDANCY_SPLIT_BRAIN_DETECTED, "Heartbeat autre MASTER: " + masterId);
    return;
  }

  if (epoch < state.redundancyEpoch && state.activeMasterId.length() && masterId != state.activeMasterId) return;

  state.lastMasterHeartbeatMs = now;
  state.masterAlive = true;
  state.activeMasterId = masterId.length() ? masterId : state.activeMasterId;
  if (epoch >= state.redundancyEpoch) state.redundancyEpoch = epoch;

  if (state.role == ROLE_BACKUP && !state.isActiveMaster) setState(REDUNDANCY_BACKUP_READY);
}

void RedundancyManager::sendHeartbeatIfNeeded(uint32_t now) {
  if (!isActiveMaster() && state.role != ROLE_BACKUP) return;
  if (now - lastHeartbeatSentMs < heartbeatIntervalMs) return;
  lastHeartbeatSentMs = now;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) espnow.sendHeartbeat(peer.as<String>());
}

void RedundancyManager::checkMasterTimeout(uint32_t now) {
  if (state.role != ROLE_BACKUP || state.isActiveMaster) return;
  if (state.lastMasterHeartbeatMs == 0) return;
  if (now - state.lastMasterHeartbeatMs <= takeoverTimeoutMs) return;

  if (currentState != REDUNDANCY_TAKEOVER_PENDING) {
    takeoverPendingSinceMs = now;
    setState(REDUNDANCY_TAKEOVER_PENDING, "Heartbeat MASTER perdu");
    return;
  }

  if (now - takeoverPendingSinceMs >= 50) becomeActiveMaster();
}

void RedundancyManager::becomeActiveMaster() {
  if (state.isActiveMaster) return;
  state.isActiveMaster = true;
  state.masterAlive = false;
  state.activeMasterId = state.deviceId;
  state.redundancyEpoch++;
  setState(REDUNDANCY_TAKEOVER_ACTIVE, "MASTER_TAKEOVER");
  state.addLog("MASTER_TAKEOVER epoch=" + String(state.redundancyEpoch));
}

bool RedundancyManager::detectSplitBrain() {
  bool split = state.role == ROLE_BACKUP && state.isActiveMaster && state.masterAlive;
  if (split) setState(REDUNDANCY_SPLIT_BRAIN_DETECTED, "Double master detecte");
  state.splitBrainDetected = currentState == REDUNDANCY_SPLIT_BRAIN_DETECTED;
  return state.splitBrainDetected;
}

void RedundancyManager::printStatus() {
  Serial.println(F("=== RedundancyManager status ==="));
  Serial.print(F("state=")); Serial.println(stateText(currentState));
  Serial.print(F("activeMasterId=")); Serial.println(state.activeMasterId);
  Serial.print(F("epoch=")); Serial.println(state.redundancyEpoch);
  Serial.print(F("isActiveMaster=")); Serial.println(state.isActiveMaster ? F("true") : F("false"));
}

void RedundancyManager::sendHeartbeat() {
  sendHeartbeatIfNeeded(millis());
}

void RedundancyManager::receiveHeartbeat() {
  onHeartbeatReceived(state.activeMasterId, state.redundancyEpoch);
}

void RedundancyManager::checkMasterAlive(uint32_t now) {
  checkMasterTimeout(now);
  detectSplitBrain();
}

void RedundancyManager::becomeMaster() {
  becomeActiveMaster();
}

void RedundancyManager::becomeBackup() {
  state.isActiveMaster = false;
  state.masterAlive = true;
  setState(REDUNDANCY_BACKUP_READY, "Retour BACKUP");
}

void RedundancyManager::syncConfig() {
  EspNowMessage msg{};
  msg.messageType = MSG_CONFIG_SYNC;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) espnow.sendMessage(peer.as<String>(), msg);
}

void RedundancyManager::syncRuntimeState() {
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) espnow.sendStatus(peer.as<String>(), "surplus", state.surplusW);
}

void RedundancyManager::setState(RedundancyState next, const String &reason) {
  if (currentState == next && state.redundancyState == stateText(next)) return;
  currentState = next;
  state.redundancyState = stateText(next);
  state.splitBrainDetected = next == REDUNDANCY_SPLIT_BRAIN_DETECTED;
  if (reason.length()) state.addLog("Redondance " + state.redundancyState + ": " + reason);
}

const char *RedundancyManager::stateText(RedundancyState value) const {
  switch (value) {
    case REDUNDANCY_ACTIVE_MASTER: return "ACTIVE_MASTER";
    case REDUNDANCY_BACKUP_READY: return "BACKUP_READY";
    case REDUNDANCY_TAKEOVER_PENDING: return "TAKEOVER_PENDING";
    case REDUNDANCY_TAKEOVER_ACTIVE: return "TAKEOVER_ACTIVE";
    case REDUNDANCY_SPLIT_BRAIN_DETECTED: return "SPLIT_BRAIN_DETECTED";
    default: return "PASSIVE";
  }
}
