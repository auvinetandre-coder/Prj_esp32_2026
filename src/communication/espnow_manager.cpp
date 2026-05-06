#include "espnow_manager.h"

static EspNowManager *gEspNow = nullptr;

bool EspNowManager::begin() {
  return initEspNow();
}

bool EspNowManager::initEspNow() {
  gEspNow = this;
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    state.espNowReady = false;
    state.addLog("ESP-NOW init failed");
    Serial.println(F("ESP-NOW init failed"));
    return false;
  }
  esp_now_register_recv_cb(onReceiveMessage);
  esp_now_register_send_cb(onSendStatus);
  state.espNowReady = true;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) addPeer(peer.as<String>());
  state.addLog("ESP-NOW ready");
  Serial.println(F("ESP-NOW ready"));
  return true;
}

bool EspNowManager::addPeer(const String &mac) {
  uint8_t addr[6];
  if (!parseMac(mac, addr)) return false;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, addr, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(addr) && esp_now_add_peer(&peer) != ESP_OK) return false;
  JsonArray peers = config.system()["peers"].as<JsonArray>();
  for (JsonVariant p : peers) if (p.as<String>().equalsIgnoreCase(mac)) return true;
  peers.add(mac);
  config.saveSystem();
  state.addLog("ESP-NOW peer ajoute: " + mac);
  return true;
}

bool EspNowManager::removePeer(const String &mac) {
  uint8_t addr[6];
  if (!parseMac(mac, addr)) return false;
  esp_now_del_peer(addr);
  JsonArray peers = config.system()["peers"].as<JsonArray>();
  for (uint8_t i = 0; i < peers.size(); i++) {
    if (peers[i].as<String>().equalsIgnoreCase(mac)) {
      peers.remove(i);
      break;
    }
  }
  state.addLog("ESP-NOW peer retire: " + mac);
  return config.saveSystem();
}

bool EspNowManager::isPeerKnown(const String &mac) {
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) {
    if (peer.as<String>().equalsIgnoreCase(mac)) return true;
  }
  return false;
}

bool EspNowManager::sendMessage(const String &peer, EspNowMessage message) {
  uint8_t addr[6];
  if (!parseMac(peer, addr)) return false;
  prepareMessage(message);
  esp_err_t result = esp_now_send(addr, reinterpret_cast<const uint8_t *>(&message), sizeof(message));
  if (millis() - lastTxLogMs > 1000) {
    lastTxLogMs = millis();
    state.addLog(String("ESP-NOW TX ") + messageTypeText(message.messageType) + " -> " + peer);
  }
  return result == ESP_OK;
}

bool EspNowManager::sendHeartbeat(const String &peer) {
  EspNowMessage msg{};
  msg.messageType = MSG_HEARTBEAT;
  msg.epoch = state.redundancyEpoch;
  strlcpy(msg.masterId, (state.isActiveMaster ? state.deviceId : state.activeMasterId).c_str(), sizeof(msg.masterId));
  return sendMessage(peer, msg);
}

bool EspNowManager::sendSensorValue(const String &peer, const String &sensorId, const String &variable, float value) {
  EspNowMessage msg{};
  msg.messageType = MSG_SENSOR_VALUE;
  strlcpy(msg.sensorId, sensorId.c_str(), sizeof(msg.sensorId));
  strlcpy(msg.variable, variable.c_str(), sizeof(msg.variable));
  msg.value = value;
  return sendMessage(peer, msg);
}

bool EspNowManager::sendActuatorCommand(const String &peer, const String &actuatorId, float commandPercent, const String &mode, uint32_t ttlMs) {
  EspNowMessage msg{};
  msg.messageType = MSG_ACTUATOR_COMMAND;
  msg.epoch = state.redundancyEpoch;
  msg.ttlMs = ttlMs;
  msg.commandPercent = constrain(commandPercent, 0.0f, 100.0f);
  msg.value = msg.commandPercent;
  strlcpy(msg.masterId, (state.isActiveMaster ? state.deviceId : state.activeMasterId).c_str(), sizeof(msg.masterId));
  strlcpy(msg.actuatorId, actuatorId.c_str(), sizeof(msg.actuatorId));
  strlcpy(msg.command, "setPower", sizeof(msg.command));
  strlcpy(msg.mode, mode.c_str(), sizeof(msg.mode));
  return sendMessage(peer, msg);
}

bool EspNowManager::sendStatus(const String &peer, const String &variable, float value) {
  EspNowMessage msg{};
  msg.messageType = MSG_STATUS;
  strlcpy(msg.variable, variable.c_str(), sizeof(msg.variable));
  msg.value = value;
  return sendMessage(peer, msg);
}

bool EspNowManager::sendError(const String &peer, const String &text) {
  EspNowMessage msg{};
  msg.messageType = MSG_ERROR;
  strlcpy(msg.variable, text.c_str(), sizeof(msg.variable));
  return sendMessage(peer, msg);
}

void EspNowManager::onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  handleReceive(mac, data, len);
}

void EspNowManager::onSent(const uint8_t *mac, esp_now_send_status_t status) {
  handleSendStatus(mac, status);
}

void EspNowManager::handleReceive(const uint8_t *mac, const uint8_t *data, int len) {
  String peer = macToString(mac);
  if (len != sizeof(EspNowMessage)) {
    state.addLog("ESP-NOW RX taille invalide");
    return;
  }
  if (!isPeerKnown(peer)) {
    state.addLog("ESP-NOW RX peer inconnu: " + peer);
    return;
  }

  EspNowMessage msg{};
  memcpy(&msg, data, sizeof(msg));
  if (!checksumValid(msg)) {
    state.addLog("ESP-NOW RX checksum invalide: " + peer);
    return;
  }

  if (millis() - lastRxLogMs > 1000) {
    lastRxLogMs = millis();
    state.addLog(String("ESP-NOW RX ") + messageTypeText(msg.messageType) + " <- " + peer);
  }

  if (msg.messageType == MSG_HEARTBEAT) {
    String previousMaster = state.activeMasterId;
    bool wasActiveMaster = state.isActiveMaster;
    state.lastMasterHeartbeatMs = millis();
    state.masterAlive = true;
    state.activeMasterId = strlen(msg.masterId) ? msg.masterId : msg.senderId;
    if (msg.epoch >= state.redundancyEpoch) state.redundancyEpoch = msg.epoch;
    if (wasActiveMaster && previousMaster.length() && state.activeMasterId != state.deviceId && state.activeMasterId != previousMaster) {
      state.splitBrainDetected = true;
      state.redundancyState = "SPLIT_BRAIN_DETECTED";
      state.addLog("ESP-NOW split brain detecte: " + state.activeMasterId);
    } else if (state.role == ROLE_BACKUP && !state.isActiveMaster) {
      state.redundancyState = "BACKUP_READY";
    }
    return;
  }

  if (msg.messageType == MSG_ACTUATOR_COMMAND) {
    if (!acceptActuatorCommand(msg, peer)) return;
    String command = strlen(msg.command) ? String(msg.command) : "setPower";
    bool executed = actuatorHandler && actuatorHandler(actuatorHandlerContext, msg.actuatorId, command, msg.commandPercent, msg.mode);
    state.addLog(String("ESP-NOW commande actionneur ") + (executed ? "executee: " : "refusee localement: ") + msg.actuatorId + " " + String(msg.commandPercent, 1) + "%");
    return;
  }

  if (msg.messageType == MSG_SENSOR_VALUE) {
    String id = msg.sensorId;
    String variable = msg.variable;
    if (id == "jsy_grid" && variable == "activePower") state.gridPowerW = msg.value;
    if (id == "jsy_grid" && variable == "surplusW") state.surplusW = msg.value;
    if (id == "temp_tank_top" && variable == "temperature") state.tankTopC = msg.value;
    if (id == "temp_tank_middle" && variable == "temperature") state.tankMiddleC = msg.value;
    if (id == "temp_tank_bottom" && variable == "temperature") state.tankBottomC = msg.value;
    return;
  }

  if (msg.messageType == MSG_STATUS && String(msg.variable) == "surplus") state.surplusW = msg.value;
  if (msg.messageType == MSG_ERROR) state.addLog(String("ESP-NOW ERROR: ") + msg.variable);
}

void EspNowManager::handleSendStatus(const uint8_t *mac, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) state.addLog("ESP-NOW send failed: " + macToString(mac));
}

void EspNowManager::setActuatorCommandHandler(ActuatorCommandHandler handler, void *context) {
  actuatorHandler = handler;
  actuatorHandlerContext = context;
}

String EspNowManager::peersJson() {
  String out;
  serializeJson(config.system()["peers"], out);
  return out;
}

void EspNowManager::printPeers() {
  Serial.println(F("=== ESP-NOW peers ==="));
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) Serial.println(peer.as<String>());
}

bool EspNowManager::parseMac(const String &mac, uint8_t out[6]) {
  int values[6];
  if (sscanf(mac.c_str(), "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6) return false;
  for (uint8_t i = 0; i < 6; i++) out[i] = static_cast<uint8_t>(values[i]);
  return true;
}

String EspNowManager::macToString(const uint8_t *mac) {
  char out[18];
  snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(out);
}

void EspNowManager::prepareMessage(EspNowMessage &message) {
  message.role = static_cast<uint8_t>(state.role);
  message.sequenceNumber = ++sequenceCounter;
  message.timestamp = millis();
  if (!strlen(message.senderId)) strlcpy(message.senderId, state.deviceId.c_str(), sizeof(message.senderId));
  if (!strlen(message.masterId)) strlcpy(message.masterId, (state.isActiveMaster ? state.deviceId : state.activeMasterId).c_str(), sizeof(message.masterId));
  message.checksum = checksumFor(message);
}

uint16_t EspNowManager::checksumFor(EspNowMessage message) {
  message.checksum = 0;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&message);
  uint16_t sum = 0xA5A5;
  for (size_t i = 0; i < sizeof(EspNowMessage); i++) sum = static_cast<uint16_t>((sum << 1) ^ bytes[i] ^ (sum >> 15));
  return sum;
}

bool EspNowManager::checksumValid(const EspNowMessage &message) {
  return message.checksum == checksumFor(message);
}

bool EspNowManager::acceptActuatorCommand(const EspNowMessage &message, const String &mac) {
  if (state.safetyTripped) {
    state.addLog("ESP-NOW commande refusee: Safety CRITICAL");
    return false;
  }
  if (message.ttlMs && millis() - message.timestamp > message.ttlMs) {
    state.addLog("ESP-NOW commande refusee: expiree");
    return false;
  }
  if (!state.activeMasterId.length()) {
    state.addLog("ESP-NOW commande refusee: master inconnu");
    return false;
  }
  if (String(message.masterId) != state.activeMasterId) {
    state.addLog("ESP-NOW commande refusee: ancien masterId depuis " + mac);
    return false;
  }
  if (message.epoch < state.redundancyEpoch) {
    state.addLog("ESP-NOW commande refusee: epoch ancien");
    return false;
  }
  if (message.epoch > state.redundancyEpoch) state.redundancyEpoch = message.epoch;
  return true;
}

const char *EspNowManager::messageTypeText(uint8_t type) {
  switch (type) {
    case MSG_SENSOR_VALUE: return "SENSOR_VALUE";
    case MSG_ACTUATOR_COMMAND: return "ACTUATOR_COMMAND";
    case MSG_HEARTBEAT: return "HEARTBEAT";
    case MSG_CONFIG_SYNC: return "CONFIG_SYNC";
    case MSG_STATUS: return "STATUS";
    case MSG_ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceiveMessage(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (gEspNow) gEspNow->onReceive(info->src_addr, data, len);
}

void onSendStatus(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (gEspNow) gEspNow->onSent(info->des_addr, status);
}
#else
void onReceiveMessage(const uint8_t *mac, const uint8_t *data, int len) {
  if (gEspNow) gEspNow->onReceive(mac, data, len);
}

void onSendStatus(const uint8_t *mac, esp_now_send_status_t status) {
  if (gEspNow) gEspNow->onSent(mac, status);
}
#endif
