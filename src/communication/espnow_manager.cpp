#include "espnow_manager.h"
#include "../sensors/sensor_manager.h"
#include <math.h>

static EspNowManager *gEspNow = nullptr;
static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static const char *espNowValueTypeText(uint8_t valueType) {
  switch (valueType) {
    case VALUE_POWER_W: return "POWER";
    case VALUE_GRID_POWER_W: return "GRID";
    case VALUE_VOLTAGE_V: return "VOLT";
    case VALUE_CURRENT_A: return "CURR";
    case VALUE_APPARENT_POWER_VA: return "PAPP";
    case VALUE_POWER_FACTOR: return "PF";
    case VALUE_FREQUENCY_HZ: return "FREQ";
    case VALUE_TEMPERATURE_C: return "TEMP";
    case VALUE_HUMIDITY_PERCENT: return "HUM";
    case VALUE_ENERGY_KWH: return "ENERGY";
    case VALUE_BATTERY_VOLTAGE_V: return "BATV";
    case VALUE_BATTERY_CURRENT_A: return "BATA";
    case VALUE_BATTERY_SOC_PERCENT: return "SOC";
    case VALUE_STATE_BOOL: return "STATE";
    case VALUE_RSSI_DBM: return "RSSI";
    case VALUE_CUSTOM: return "CUSTOM";
    default: return "UNKNOWN";
  }
}

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
  addBroadcastPeer();
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) addPeer(peer.as<String>());
  sendDiscovery();
  state.addLog("ESP-NOW ready");
  Serial.println(F("ESP-NOW ready"));
  return true;
}

void EspNowManager::loop() {
  if (!state.espNowReady) return;
  uint32_t intervalMs = config.system()["espnow"]["discoveryIntervalMs"] | ESPNOW_DEFAULT_ANNOUNCE_INTERVAL_MS;
  intervalMs = constrain(intervalMs, 1000UL, 30000UL);
  const uint32_t now = millis();
  if (now - lastDiscoverySentMs >= intervalMs) sendDiscovery();
  publishLocalSensorsIfNeeded(now);
  publishLocalSensorDiscoveryIfNeeded(now);
  publishDiagnosticIfNeeded(now);
}

bool EspNowManager::addBroadcastPeer() {
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, ESPNOW_BROADCAST_MAC, sizeof(peer.peer_addr));
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_is_peer_exist(ESPNOW_BROADCAST_MAC)) return true;
  return esp_now_add_peer(&peer) == ESP_OK;
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
  if (espNowDebugTransmissionEnabled() && millis() - lastTxLogMs > 1000) {
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
  if (len >= 2 && data[0] == ESPNOW_PROTOCOL_VERSION) {
    if (data[1] == ESPNOW_PACKET_DISCOVERY) {
      handleDiscoveryPacket(mac, data, len);
      return;
    }
    if (data[1] == ESPNOW_PACKET_SENSOR_DATA) {
      handleSensorPacket(mac, data, len);
      return;
    }
    if (data[1] == ESPNOW_PACKET_FAST_DATA) {
      handleFastSensorPacket(mac, data, len);
      return;
    }
    if (data[1] == ESPNOW_PACKET_SENSOR_DISCOVERY) {
      handleSensorDiscoveryPacket(mac, data, len);
      return;
    }
    if (data[1] == ESPNOW_PACKET_DIAGNOSTIC) {
      handleDiagnosticPacket(mac, data, len);
      return;
    }
  }

  if (len != sizeof(EspNowMessage)) {
    state.addLog("ESP-NOW RX taille invalide");
    return;
  }
  if (!isPeerKnown(peer)) {
    if (millis() - lastUnknownPeerLogMs > 5000UL) {
      lastUnknownPeerLogMs = millis();
      state.addLog("ESP-NOW RX peer inconnu: " + peer);
    }
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
  if (status != ESP_NOW_SEND_SUCCESS && millis() - lastSendFailureLogMs > 5000UL) {
    lastSendFailureLogMs = millis();
    state.addLog("ESP-NOW send failed: " + macToString(mac));
  }
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

String EspNowManager::statusJson() {
  DynamicJsonDocument doc(6144);
  JsonObject out = doc.to<JsonObject>();
  out["ready"] = state.espNowReady;
  out["mac"] = WiFi.macAddress();
  out["roleFlags"] = localRoleFlags();
  out["capabilityFlags"] = localCapabilityFlags();
  out["discoveryIntervalMs"] = config.system()["espnow"]["discoveryIntervalMs"] | ESPNOW_DEFAULT_ANNOUNCE_INTERVAL_MS;
  out["sensorDiscoveryIntervalMs"] = config.system()["espnow"]["sensorDiscoveryIntervalMs"] | 30000UL;
  out["diagnosticIntervalMs"] = config.system()["espnow"]["diagnosticIntervalMs"] | 10000UL;
  out["debugTransmission"] = espNowDebugTransmissionEnabled();
  out["debugReception"] = espNowDebugReceptionEnabled();
  JsonArray exports = out["exports"].to<JsonArray>();
  const uint8_t exportSensorIds[] = {SENSOR_LINKY, SENSOR_JSY, 20, 21, 22, 30, 31};
  for (uint8_t sensorId : exportSensorIds) {
    EspNowExportConfig cfg = exportConfigFor(sensorId);
    JsonObject item = exports.add<JsonObject>();
    float value = 0.0f;
    int8_t slot = exportRuntimeIndex(sensorId);
    item["sensorId"] = sensorId;
    item["sensorName"] = cfg.sensorName;
    item["sensorType"] = cfg.sensorType;
    item["sensorTypeText"] = espNowSensorTypeText(item["sensorType"] | SENSOR_UNKNOWN);
    item["exportEnabled"] = cfg.exportEnabled;
    item["exportIntervalMs"] = cfg.exportIntervalMs;
    item["priority"] = cfg.priority;
    item["sendOnChange"] = cfg.sendOnChange;
    item["minDelta"] = cfg.minDelta;
    item["available"] = currentExportValue(sensorId, value);
    item["currentValue"] = value;
    item["lastSentMs"] = slot >= 0 ? exportLastSentMs[slot] : 0;
    item["ageMs"] = slot >= 0 && exportLastSentMs[slot] ? millis() - exportLastSentMs[slot] : 0;
  }
  JsonArray peers = out["peers"].to<JsonArray>();
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) peers.add(peer.as<String>());
  JsonArray nodes = out["discoveredNodes"].to<JsonArray>();
  const uint32_t now = millis();
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (!discoveredNodes[i].used) continue;
    JsonObject node = nodes.add<JsonObject>();
    node["mac"] = macToString(discoveredNodes[i].mac);
    node["nodeId"] = discoveredNodes[i].nodeId;
    node["nodeName"] = discoveredNodes[i].nodeName;
    node["roleFlags"] = discoveredNodes[i].roleFlags;
    node["capabilityFlags"] = discoveredNodes[i].capabilityFlags;
    node["primarySensorType"] = discoveredNodes[i].primarySensorType;
    node["primarySensorText"] = espNowSensorTypeText(discoveredNodes[i].primarySensorType);
    node["lastSeenMs"] = discoveredNodes[i].lastSeenMs;
    node["ageMs"] = now >= discoveredNodes[i].lastSeenMs ? now - discoveredNodes[i].lastSeenMs : 0;
    node["lastSequence"] = discoveredNodes[i].lastSequence;
    node["peerKnown"] = isPeerKnown(node["mac"].as<String>());
  }
  String outText;
  serializeJson(doc, outText);
  return outText;
}

String EspNowManager::discoveredNodesJson() {
  DynamicJsonDocument doc(3072);
  JsonArray nodes = doc.to<JsonArray>();
  const uint32_t now = millis();
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (!discoveredNodes[i].used) continue;
    JsonObject node = nodes.add<JsonObject>();
    node["mac"] = macToString(discoveredNodes[i].mac);
    node["nodeId"] = discoveredNodes[i].nodeId;
    node["nodeName"] = discoveredNodes[i].nodeName;
    node["roleFlags"] = discoveredNodes[i].roleFlags;
    node["capabilityFlags"] = discoveredNodes[i].capabilityFlags;
    node["primarySensorType"] = discoveredNodes[i].primarySensorType;
    node["primarySensorText"] = espNowSensorTypeText(discoveredNodes[i].primarySensorType);
    node["ageMs"] = now >= discoveredNodes[i].lastSeenMs ? now - discoveredNodes[i].lastSeenMs : 0;
    node["peerKnown"] = isPeerKnown(node["mac"].as<String>());
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool EspNowManager::sendDiscovery() {
  if (!state.espNowReady) return false;
  addBroadcastPeer();

  EspNowDiscoveryPacket packet{};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_DISCOVERY;
  packet.nodeId = static_cast<uint8_t>(state.role);
  String nodeName = state.moduleName;
  if (!nodeName.length()) nodeName = config.device()["deviceName"] | config.device()["name"] | "RouteurSolaire";
  espNowCopyFixedText(packet.nodeName, sizeof(packet.nodeName), nodeName.c_str());
  packet.roleFlags = localRoleFlags();
  packet.capabilityFlags = localCapabilityFlags();
  packet.primarySensorType = SENSOR_ROUTER;
  WiFi.macAddress(packet.mac);
  packet.sequence = ++discoverySequenceCounter;
  packet.uptimeMs = millis();
  packet.checksum = espNowCalculateChecksum(packet);
  lastDiscoverySentMs = packet.uptimeMs;

  esp_err_t result = esp_now_send(ESPNOW_BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  if (result != ESP_OK) {
    state.addLog("ESP-NOW discovery TX failed");
    return false;
  }
  return true;
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

void EspNowManager::handleDiscoveryPacket(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != static_cast<int>(sizeof(EspNowDiscoveryPacket))) {
    state.addLog("ESP-NOW discovery taille invalide");
    return;
  }
  EspNowDiscoveryPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet)) {
    state.addLog("ESP-NOW discovery checksum invalide");
    return;
  }

  const uint8_t *nodeMac = packet.mac[0] || packet.mac[1] || packet.mac[2] || packet.mac[3] || packet.mac[4] || packet.mac[5]
                               ? packet.mac
                               : mac;
  EspNowDiscoveredNode *node = rememberDiscoveredNode(nodeMac, packet);
  if (!node) {
    state.addLog("ESP-NOW discovery table pleine");
    return;
  }
  if (millis() - lastRxLogMs > 1000) {
    lastRxLogMs = millis();
    state.addLog(String("ESP-NOW noeud detecte: ") + node->nodeName + " " + macToString(node->mac));
  }
}

void EspNowManager::handleSensorPacket(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != static_cast<int>(sizeof(EspNowSensorPacket))) {
    state.addLog("ESP-NOW sensor taille invalide");
    return;
  }
  EspNowSensorPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet) || packet.valueCount > ESPNOW_MAX_SENSOR_VALUES) {
    state.addLog("ESP-NOW sensor trame invalide");
    return;
  }

  if (!isPeerKnown(macToString(mac))) {
    if (millis() - lastUnauthorizedSensorLogMs > 5000UL) {
      lastUnauthorizedSensorLogMs = millis();
      state.addLog(String("ESP-NOW sensor ignore peer non autorise: ") + macToString(mac));
    }
    return;
  }

  bool matched = false;
  const String sourceMac = macToString(mac);
  state.lastEspNowSensorReceiveMs = millis();
  logDebugSensorPacketIfNeeded(sourceMac, packet);
  if (sensorManager) matched = sensorManager->updateRemoteSensor(sourceMac, packet);
  if (millis() - lastSensorRxSummaryLogMs > 5000UL) {
    lastSensorRxSummaryLogMs = millis();
    state.addLog(String("ESP-NOW sensor RX ") + packet.nodeName + " valeurs=" + String(packet.valueCount) + (matched ? " appliquees" : " sans mapping"));
  }
  if (!matched && espNowSensorDebugEnabledForMac(sourceMac) && millis() - lastMapFailureLogMs > 2000UL) {
    lastMapFailureLogMs = millis();
    logDebugMapping(String("ESP-NOW mapping sans valeur appliquee mac=") + sourceMac + " node=" + packet.nodeName + " type=" + espNowSensorTypeText(packet.sensorType) + " valueCount=" + String(packet.valueCount));
  }
}

void EspNowManager::handleFastSensorPacket(const uint8_t *mac, const uint8_t *data, int len) {
  const String sourceMac = macToString(mac);
  if (len != static_cast<int>(sizeof(EspNowFastSensorPacket))) {
    state.addLog(String("ESP-NOW FAST_DATA taille invalide mac=") + sourceMac + " len=" + String(len) + " attendu=" + String(sizeof(EspNowFastSensorPacket)));
    return;
  }
  EspNowFastSensorPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet) || packet.valueCount > ESPNOW_MAX_FAST_VALUES) {
    state.addLog(String("ESP-NOW FAST_DATA trame invalide mac=") + sourceMac + " nodeId=" + String(packet.nodeId) + " sensorId=" + String(packet.sensorId) + " values=" + String(packet.valueCount) + " checksum=" + String(packet.checksum) + "/" + String(espNowCalculateChecksum(packet)));
    return;
  }
  if (!isPeerKnown(sourceMac)) {
    if (millis() - lastUnauthorizedSensorLogMs > 5000UL) {
      lastUnauthorizedSensorLogMs = millis();
      state.addLog(String("ESP-NOW fast ignore peer non autorise: ") + sourceMac);
    }
    return;
  }
  state.lastEspNowSensorReceiveMs = millis();
  logDebugFastSensorPacketIfNeeded(sourceMac, packet, len);
  bool matched = false;
  if (sensorManager) matched = sensorManager->updateRemoteSensorFast(sourceMac, packet);
  if (!matched && espNowSensorDebugEnabledForMac(sourceMac) && millis() - lastMapFailureLogMs > 2000UL) {
    lastMapFailureLogMs = millis();
    logDebugMapping(String("ESP-NOW FAST_DATA mapping sans valeur appliquee mac=") + sourceMac + " sensorId=" + String(packet.sensorId) + " type=" + espNowSensorTypeText(packet.sensorType) + " valueCount=" + String(packet.valueCount));
  }
}

void EspNowManager::handleSensorDiscoveryPacket(const uint8_t *mac, const uint8_t *data, int len) {
  const String sourceMac = macToString(mac);
  if (len != static_cast<int>(sizeof(EspNowSensorDiscoveryPacket))) {
    state.addLog(String("ESP-NOW SENSOR_DISCOVERY taille invalide mac=") + sourceMac + " len=" + String(len) + " attendu=" + String(sizeof(EspNowSensorDiscoveryPacket)));
    return;
  }
  EspNowSensorDiscoveryPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet) || packet.valueCount > ESPNOW_MAX_FAST_VALUES) {
    state.addLog(String("ESP-NOW SENSOR_DISCOVERY invalide mac=") + sourceMac + " nodeId=" + String(packet.nodeId) + " sensorId=" + String(packet.sensorId) + " values=" + String(packet.valueCount) + " checksum=" + String(packet.checksum) + "/" + String(espNowCalculateChecksum(packet)));
    return;
  }
  logDebugSensorDiscoveryPacketIfNeeded(sourceMac, packet, len);
  if (sensorManager) sensorManager->updateRemoteSensorDiscovery(sourceMac, packet);
}

void EspNowManager::handleDiagnosticPacket(const uint8_t *mac, const uint8_t *data, int len) {
  const String sourceMac = macToString(mac);
  if (len != static_cast<int>(sizeof(EspNowDiagnosticPacket))) {
    state.addLog(String("ESP-NOW DIAGNOSTIC taille invalide mac=") + sourceMac + " len=" + String(len) + " attendu=" + String(sizeof(EspNowDiagnosticPacket)));
    return;
  }
  EspNowDiagnosticPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet)) {
    state.addLog(String("ESP-NOW DIAGNOSTIC checksum invalide mac=") + sourceMac + " checksum=" + String(packet.checksum) + "/" + String(espNowCalculateChecksum(packet)));
    return;
  }
  logDebugDiagnosticPacketIfNeeded(sourceMac, packet, len);
  if (sensorManager) sensorManager->updateRemoteDiagnostic(sourceMac, packet);
}

EspNowDiscoveredNode *EspNowManager::rememberDiscoveredNode(const uint8_t *mac, const EspNowDiscoveryPacket &packet) {
  EspNowDiscoveredNode *slot = nullptr;
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (discoveredNodes[i].used && memcmp(discoveredNodes[i].mac, mac, 6) == 0) {
      slot = &discoveredNodes[i];
      break;
    }
    if (!discoveredNodes[i].used && !slot) slot = &discoveredNodes[i];
  }
  if (!slot) return nullptr;
  slot->used = true;
  memcpy(slot->mac, mac, 6);
  slot->nodeId = packet.nodeId;
  espNowCopyFixedText(slot->nodeName, sizeof(slot->nodeName), packet.nodeName);
  slot->roleFlags = packet.roleFlags;
  slot->capabilityFlags = packet.capabilityFlags;
  slot->primarySensorType = packet.primarySensorType;
  slot->lastSeenMs = millis();
  slot->lastSequence = packet.sequence;
  return slot;
}

const char *EspNowManager::defaultExportSensorName(uint8_t sensorId) {
  if (sensorId == SENSOR_LINKY) return "Linky";
  if (sensorId == SENSOR_JSY) return "JSY";
  if (sensorId >= 20 && sensorId <= 22) return "DS18B20";
  if (sensorId == 30) return "Battery";
  if (sensorId == 31) return "Solar";
  return "Sensor";
}

uint8_t EspNowManager::defaultExportSensorType(uint8_t sensorId) {
  if (sensorId == SENSOR_LINKY) return SENSOR_LINKY;
  if (sensorId == SENSOR_JSY) return SENSOR_JSY;
  if (sensorId >= 20 && sensorId <= 22) return SENSOR_DS18B20;
  if (sensorId == 30) return SENSOR_BATTERY;
  if (sensorId == 31) return SENSOR_SOLAR;
  return SENSOR_UNKNOWN;
}

EspNowExportConfig EspNowManager::exportConfigFor(uint8_t sensorId, const char *defaultName, uint8_t defaultType) {
  EspNowExportConfig out{};
  out.sensorId = sensorId;
  out.sensorType = defaultType == SENSOR_UNKNOWN ? defaultExportSensorType(sensorId) : defaultType;
  espNowCopyFixedText(out.sensorName, sizeof(out.sensorName), defaultName && defaultName[0] ? defaultName : defaultExportSensorName(sensorId));

  if (sensorId >= 20 && sensorId <= 22) {
    uint8_t index = sensorId - 20;
    JsonArray ds18b20 = config.sensorsDoc()["ds18b20"].as<JsonArray>();
    if (index < ds18b20.size()) {
      JsonObject sensor = ds18b20[index];
      const char *name = sensor["name"] | sensor["id"] | out.sensorName;
      const char *role = sensor["role"] | name;
      espNowCopyFixedText(out.sensorName, sizeof(out.sensorName), name);
      espNowCopyFixedText(out.sensorRole, sizeof(out.sensorRole), role);
      if (sensor["espNowExportEnabled"].is<bool>()) {
        out.sensorType = SENSOR_DS18B20;
        out.exportEnabled = sensor["espNowExportEnabled"] | false;
        out.exportIntervalMs = sensor["espNowExportIntervalMs"] | out.exportIntervalMs;
        out.priority = sensor["espNowExportPriority"] | out.priority;
        out.sendOnChange = sensor["espNowSendOnChange"] | false;
        out.minDelta = sensor["espNowMinDelta"] | 0.0f;
        out.timeoutMs = sensor["espNowTimeoutMs"] | 0UL;
        return out;
      }
    }
  }

  for (JsonObject sensor : config.sensors()) {
    String source = sensor["source"] | "local";
    if (source.equalsIgnoreCase("espnow")) continue;
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    bool matches = false;
    if (sensorId == SENSOR_LINKY && (id == "tic_linky" || type == "TIC Linky")) matches = true;
    if (sensorId == SENSOR_JSY && (id == "jsy_grid" || type == "JSY-MK-194T")) matches = true;
    if (sensorId == 30 && type == "Battery") matches = true;
    if (sensorId == 31 && type == "Solar") matches = true;
    if (!matches) continue;

    const char *name = sensor["name"] | sensor["id"] | out.sensorName;
    const char *role = sensor["role"] | "";
    espNowCopyFixedText(out.sensorName, sizeof(out.sensorName), name);
    espNowCopyFixedText(out.sensorRole, sizeof(out.sensorRole), role);
    if (sensor["espNowExportEnabled"].is<bool>()) {
      out.exportEnabled = sensor["espNowExportEnabled"] | false;
      out.exportIntervalMs = sensor["espNowExportIntervalMs"] | out.exportIntervalMs;
      out.priority = sensor["espNowExportPriority"] | out.priority;
      out.sendOnChange = sensor["espNowSendOnChange"] | false;
      out.minDelta = sensor["espNowMinDelta"] | 0.0f;
      out.timeoutMs = sensor["espNowTimeoutMs"] | 0UL;
      return out;
    }
  }

  JsonArray exports = config.system()["espnow"]["exports"].as<JsonArray>();
  if (!exports.isNull()) {
    for (JsonObject item : exports) {
      if ((item["sensorId"] | 0) != sensorId) continue;
      const char *name = item["sensorName"] | out.sensorName;
      const char *role = item["sensorRole"] | "";
      espNowCopyFixedText(out.sensorName, sizeof(out.sensorName), name);
      espNowCopyFixedText(out.sensorRole, sizeof(out.sensorRole), role);
      out.sensorType = item["sensorType"] | out.sensorType;
      out.exportEnabled = item["exportEnabled"] | false;
      out.exportIntervalMs = item["exportIntervalMs"] | out.exportIntervalMs;
      out.priority = item["priority"] | out.priority;
      out.sendOnChange = item["sendOnChange"] | false;
      out.minDelta = item["minDelta"] | 0.0f;
      out.timeoutMs = item["timeoutMs"] | 0UL;
      return out;
    }
  }

  out.exportEnabled = false;
  return out;
}

int8_t EspNowManager::exportRuntimeIndex(uint8_t sensorId) {
  for (uint8_t i = 0; i < MAX_EXPORT_RUNTIME_SLOTS; i++) {
    if (exportRuntimeSensorIds[i] == sensorId) return i;
  }
  return -1;
}

int8_t EspNowManager::ensureExportRuntimeIndex(uint8_t sensorId) {
  int8_t existing = exportRuntimeIndex(sensorId);
  if (existing >= 0) return existing;
  for (uint8_t i = 0; i < MAX_EXPORT_RUNTIME_SLOTS; i++) {
    if (exportRuntimeSensorIds[i] != 0) continue;
    exportRuntimeSensorIds[i] = sensorId;
    exportLastSentMs[i] = 0;
    exportLastValues[i] = 0.0f;
    exportLastValueValid[i] = false;
    return i;
  }
  return -1;
}

bool EspNowManager::isSensorExportEnabled(uint8_t sensorId) {
  return exportConfigFor(sensorId).exportEnabled;
}

uint32_t EspNowManager::getSensorExportInterval(uint8_t sensorId) {
  EspNowExportConfig cfg = exportConfigFor(sensorId);
  return constrain(cfg.exportIntervalMs, 100UL, 60000UL);
}

bool EspNowManager::currentExportValue(uint8_t sensorId, float &value) {
  if (sensorId == SENSOR_LINKY) {
    if (!state.ticAvailable || isnan(state.ticGridPowerW)) return false;
    value = state.ticGridPowerW;
    return true;
  }
  if (sensorId == SENSOR_JSY) {
    if (!state.jsyOnline || isnan(state.jsyGridPowerW)) return false;
    value = state.jsyGridPowerW;
    return true;
  }
  if (sensorId >= 20 && sensorId <= 22) {
    uint8_t index = sensorId - 20;
    JsonArray ds18b20 = config.sensorsDoc()["ds18b20"].as<JsonArray>();
    if (index >= ds18b20.size()) return false;
    JsonObject sensor = ds18b20[index];
    if (!(sensor["enabled"] | true)) return false;
    if (!state.ds18b20Available[index] || isnan(state.ds18b20Temps[index])) return false;
    value = state.ds18b20Temps[index];
    return true;
  }
  if (sensorId == 30) {
    if (!state.batteryOnline || isnan(state.batteryVoltageV)) return false;
    value = state.batteryVoltageV;
    return true;
  }
  if (sensorId == 31) {
    if (isnan(state.productionW) || state.productionW <= 0.0f) return false;
    value = state.productionW;
    return true;
  }
  return false;
}

bool EspNowManager::shouldExportSensor(uint8_t sensorId, float currentValue) {
  EspNowExportConfig cfg = exportConfigFor(sensorId);
  if (!cfg.exportEnabled) return false;

  int8_t slot = ensureExportRuntimeIndex(sensorId);
  if (slot < 0) return false;

  uint32_t intervalMs = constrain(cfg.exportIntervalMs, 100UL, 60000UL);
  if (exportLastSentMs[slot] == 0) return true;
  if (currentLoopNowMs - exportLastSentMs[slot] >= intervalMs) return true;

  if (cfg.sendOnChange && exportLastValueValid[slot]) {
    float delta = fabs(currentValue - exportLastValues[slot]);
    if (delta >= cfg.minDelta) return true;
  }
  return false;
}

bool EspNowManager::buildFastPacketForSensor(uint8_t sensorId, EspNowFastSensorPacket &packet, uint32_t now) {
  EspNowExportConfig cfg = exportConfigFor(sensorId);
  if (!cfg.exportEnabled) return false;

  packet = {};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_FAST_DATA;
  packet.nodeId = static_cast<uint8_t>(state.role);
  packet.sensorId = sensorId;
  packet.sensorType = cfg.sensorType;
  packet.sequence = ++sequenceCounter;
  packet.timestampMs = now;
  packet.sensorOk = true;

  if (sensorId == SENSOR_LINKY) {
    if (!state.ticAvailable) return false;
    addFastPacketValue(packet, VALUE_GRID_POWER_W, state.ticGridPowerW);
    addFastPacketValue(packet, VALUE_APPARENT_POWER_VA, state.ticApparentPowerVA);
    addFastPacketValue(packet, VALUE_CURRENT_A, state.ticCurrentA);
    return packet.valueCount > 0;
  }
  if (sensorId == SENSOR_JSY) {
    if (!state.jsyOnline) return false;
    addFastPacketValue(packet, VALUE_GRID_POWER_W, state.jsyGridPowerW);
    addFastPacketValue(packet, VALUE_VOLTAGE_V, state.gridVoltageV);
    addFastPacketValue(packet, VALUE_CURRENT_A, state.gridCurrentA);
    addFastPacketValue(packet, VALUE_POWER_FACTOR, state.gridPowerFactor);
    addFastPacketValue(packet, VALUE_FREQUENCY_HZ, state.gridFrequencyHz);
    return packet.valueCount > 0;
  }
  if (sensorId >= 20 && sensorId <= 22) {
    uint8_t index = sensorId - 20;
    if (!state.ds18b20Available[index] || isnan(state.ds18b20Temps[index])) return false;
    addFastPacketValue(packet, VALUE_TEMPERATURE_C, state.ds18b20Temps[index]);
    return packet.valueCount > 0;
  }
  if (sensorId == 30) {
    if (!state.batteryOnline) return false;
    addFastPacketValue(packet, VALUE_BATTERY_VOLTAGE_V, state.batteryVoltageV);
    addFastPacketValue(packet, VALUE_BATTERY_CURRENT_A, state.batteryCurrentA);
    addFastPacketValue(packet, VALUE_POWER_W, state.batteryPowerW);
    addFastPacketValue(packet, VALUE_BATTERY_SOC_PERCENT, state.batterySocPct);
    return packet.valueCount > 0;
  }
  if (sensorId == 31) {
    if (isnan(state.productionW) || state.productionW <= 0.0f) return false;
    addFastPacketValue(packet, VALUE_POWER_W, state.productionW);
    return packet.valueCount > 0;
  }
  return false;
}

bool EspNowManager::buildDiscoveryPacketForSensor(uint8_t sensorId, EspNowSensorDiscoveryPacket &packet) {
  EspNowExportConfig cfg = exportConfigFor(sensorId);
  if (!cfg.exportEnabled) return false;

  if (sensorId >= 20 && sensorId <= 22) {
    uint8_t index = sensorId - 20;
    JsonArray ds18b20 = config.sensorsDoc()["ds18b20"].as<JsonArray>();
    if (index >= ds18b20.size()) return false;
    JsonObject sensor = ds18b20[index];
    if (!(sensor["enabled"] | true)) return false;
    const char *name = sensor["name"] | sensor["id"] | cfg.sensorName;
    const char *role = sensor["role"] | cfg.sensorRole;
    espNowCopyFixedText(cfg.sensorName, sizeof(cfg.sensorName), name);
    espNowCopyFixedText(cfg.sensorRole, sizeof(cfg.sensorRole), role);
  }

  String nodeName = state.moduleName;
  if (!nodeName.length()) nodeName = config.device()["deviceName"] | config.device()["name"] | "RouteurSolaire";
  String firmware = config.device()["firmwareVersion"] | "";
  if (!firmware.length()) firmware = "v3";

  packet = {};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_SENSOR_DISCOVERY;
  packet.nodeId = static_cast<uint8_t>(state.role);
  espNowCopyFixedText(packet.nodeName, sizeof(packet.nodeName), nodeName.c_str());
  packet.sensorId = sensorId;
  espNowCopyFixedText(packet.sensorName, sizeof(packet.sensorName), cfg.sensorName);
  espNowCopyFixedText(packet.sensorRole, sizeof(packet.sensorRole), cfg.sensorRole);
  packet.sensorType = cfg.sensorType;
  espNowCopyFixedText(packet.firmwareVersion, sizeof(packet.firmwareVersion), firmware.c_str());

  if (sensorId == SENSOR_LINKY) {
    addSensorDiscoveryValue(packet, VALUE_GRID_POWER_W, "GRID", "W");
    addSensorDiscoveryValue(packet, VALUE_APPARENT_POWER_VA, "PAPP", "VA");
    addSensorDiscoveryValue(packet, VALUE_CURRENT_A, "IINST", "A");
  } else if (sensorId == SENSOR_JSY) {
    addSensorDiscoveryValue(packet, VALUE_GRID_POWER_W, "GRID", "W");
    addSensorDiscoveryValue(packet, VALUE_VOLTAGE_V, "VOLT", "V");
    addSensorDiscoveryValue(packet, VALUE_CURRENT_A, "CURR", "A");
    addSensorDiscoveryValue(packet, VALUE_POWER_FACTOR, "PF", "");
    addSensorDiscoveryValue(packet, VALUE_FREQUENCY_HZ, "FREQ", "Hz");
  } else if (sensorId >= 20 && sensorId <= 22) {
    addSensorDiscoveryValue(packet, VALUE_TEMPERATURE_C, "TEMP", "C");
  } else if (sensorId == 30) {
    addSensorDiscoveryValue(packet, VALUE_BATTERY_VOLTAGE_V, "BATV", "V");
    addSensorDiscoveryValue(packet, VALUE_BATTERY_CURRENT_A, "BATA", "A");
    addSensorDiscoveryValue(packet, VALUE_POWER_W, "BATP", "W");
    addSensorDiscoveryValue(packet, VALUE_BATTERY_SOC_PERCENT, "SOC", "%");
  } else if (sensorId == 31) {
    addSensorDiscoveryValue(packet, VALUE_POWER_W, "POWER", "W");
  }
  return packet.valueCount > 0;
}

void EspNowManager::exportSensorIfNeeded(uint8_t sensorId) {
  float currentValue = 0.0f;
  if (!currentExportValue(sensorId, currentValue)) return;
  if (!shouldExportSensor(sensorId, currentValue)) return;

  EspNowFastSensorPacket packet{};
  if (!buildFastPacketForSensor(sensorId, packet, currentLoopNowMs)) return;
  if (!sendFastPacketToPeers(packet)) return;

  int8_t slot = ensureExportRuntimeIndex(sensorId);
  if (slot >= 0) {
    exportLastSentMs[slot] = currentLoopNowMs;
    exportLastValues[slot] = currentValue;
    exportLastValueValid[slot] = true;
  }
}

void EspNowManager::sendSensorFastData(uint8_t sensorId) {
  float currentValue = 0.0f;
  if (!currentExportValue(sensorId, currentValue)) return;
  uint32_t now = millis();
  EspNowFastSensorPacket packet{};
  if (!buildFastPacketForSensor(sensorId, packet, now)) return;
  if (!sendFastPacketToPeers(packet)) return;
  int8_t slot = ensureExportRuntimeIndex(sensorId);
  if (slot >= 0) {
    exportLastSentMs[slot] = now;
    exportLastValues[slot] = currentValue;
    exportLastValueValid[slot] = true;
  }
}

void EspNowManager::sendSensorDiscovery(uint8_t sensorId) {
  EspNowSensorDiscoveryPacket packet{};
  if (buildDiscoveryPacketForSensor(sensorId, packet)) sendSensorDiscoveryToPeers(packet);
}

void EspNowManager::sendSensorDiagnostic(uint8_t sensorId) {
  (void)sensorId;
  EspNowDiagnosticPacket packet{};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_DIAGNOSTIC;
  packet.nodeId = static_cast<uint8_t>(state.role);
  packet.uptimeMs = millis();
  packet.freeHeap = ESP.getFreeHeap();
  packet.rssiDbm = static_cast<int8_t>(constrain(state.rssi, -128, 127));
  sendDiagnosticToPeers(packet);
}

void EspNowManager::publishLocalSensorsIfNeeded(uint32_t now) {
  currentLoopNowMs = now;
  exportSensorIfNeeded(SENSOR_LINKY);
  exportSensorIfNeeded(SENSOR_JSY);

  JsonArray ds18b20 = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (uint8_t i = 0; i < 3 && i < ds18b20.size(); i++) exportSensorIfNeeded(20 + i);

  exportSensorIfNeeded(30);
  exportSensorIfNeeded(31);
}

void EspNowManager::publishLocalSensorDiscoveryIfNeeded(uint32_t now) {
  uint32_t intervalMs = config.system()["espnow"]["sensorDiscoveryIntervalMs"] | 30000UL;
  intervalMs = constrain(intervalMs, 10000UL, 120000UL);
  if (now - lastSensorDiscoveryPublishMs < intervalMs) return;
  lastSensorDiscoveryPublishMs = now;

  sendSensorDiscovery(SENSOR_LINKY);
  sendSensorDiscovery(SENSOR_JSY);

  JsonArray ds18b20 = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (uint8_t i = 0; i < 3 && i < ds18b20.size(); i++) {
    sendSensorDiscovery(20 + i);
  }

  sendSensorDiscovery(30);
  sendSensorDiscovery(31);
}

void EspNowManager::publishDiagnosticIfNeeded(uint32_t now) {
  uint32_t intervalMs = config.system()["espnow"]["diagnosticIntervalMs"] | 10000UL;
  intervalMs = constrain(intervalMs, 5000UL, 60000UL);
  if (now - lastDiagnosticPublishMs < intervalMs) return;
  lastDiagnosticPublishMs = now;

  EspNowDiagnosticPacket packet{};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_DIAGNOSTIC;
  packet.nodeId = static_cast<uint8_t>(state.role);
  packet.uptimeMs = now;
  packet.freeHeap = ESP.getFreeHeap();
  packet.rssiDbm = static_cast<int8_t>(constrain(state.rssi, -128, 127));
  sendDiagnosticToPeers(packet);
}

bool EspNowManager::sendSensorPacketToPeers(EspNowSensorPacket &packet) {
  if (!packet.valueCount) return false;
  packet.checksum = espNowCalculateChecksum(packet);
  bool sent = false;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) {
    uint8_t addr[6];
    String mac = peer.as<String>();
    if (!parseMac(mac, addr)) continue;
    if (!esp_now_is_peer_exist(addr)) addPeer(mac);
    if (esp_now_send(addr, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK) sent = true;
  }
  return sent;
}

bool EspNowManager::sendFastPacketToPeers(EspNowFastSensorPacket &packet) {
  if (!packet.valueCount) return false;
  packet.checksum = espNowCalculateChecksum(packet);
  bool sent = false;
  uint8_t queuedCount = 0;
  uint8_t peerCount = 0;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) {
    uint8_t addr[6];
    String mac = peer.as<String>();
    if (!parseMac(mac, addr)) continue;
    peerCount++;
    if (!esp_now_is_peer_exist(addr)) addPeer(mac);
    if (esp_now_send(addr, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK) {
      sent = true;
      queuedCount++;
    }
  }
  if (espNowDebugTransmissionEnabled() && millis() - lastFastTxSummaryLogMs > 3000UL) {
    lastFastTxSummaryLogMs = millis();
    String line = "ESP-NOW TX FAST_DATA sensorId=";
    line += String(packet.sensorId);
    line += " type=";
    line += espNowSensorTypeText(packet.sensorType);
    line += " seq=";
    line += String(packet.sequence);
    line += " values=";
    line += String(packet.valueCount);
    line += " len=";
    line += String(sizeof(EspNowFastSensorPacket));
    line += " peersQueued=";
    line += String(queuedCount);
    line += "/";
    line += String(peerCount);
    state.addLog(line);
  }
  return sent;
}

bool EspNowManager::sendSensorDiscoveryToPeers(EspNowSensorDiscoveryPacket &packet) {
  packet.checksum = espNowCalculateChecksum(packet);
  bool sent = false;
  uint8_t queuedCount = 0;
  uint8_t peerCount = 0;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) {
    uint8_t addr[6];
    String mac = peer.as<String>();
    if (!parseMac(mac, addr)) continue;
    peerCount++;
    if (!esp_now_is_peer_exist(addr)) addPeer(mac);
    if (esp_now_send(addr, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK) {
      sent = true;
      queuedCount++;
    }
  }
  if (espNowDebugTransmissionEnabled() && millis() - lastDiscoveryTxSummaryLogMs > 10000UL) {
    lastDiscoveryTxSummaryLogMs = millis();
    state.addLog(String("ESP-NOW TX SENSOR_DISCOVERY sensorId=") + String(packet.sensorId) + " type=" + espNowSensorTypeText(packet.sensorType) + " values=" + String(packet.valueCount) + " len=" + String(sizeof(EspNowSensorDiscoveryPacket)) + " peersQueued=" + String(queuedCount) + "/" + String(peerCount));
  }
  return sent;
}

bool EspNowManager::sendDiagnosticToPeers(EspNowDiagnosticPacket &packet) {
  packet.checksum = espNowCalculateChecksum(packet);
  bool sent = false;
  uint8_t queuedCount = 0;
  uint8_t peerCount = 0;
  for (JsonVariant peer : config.system()["peers"].as<JsonArray>()) {
    uint8_t addr[6];
    String mac = peer.as<String>();
    if (!parseMac(mac, addr)) continue;
    peerCount++;
    if (!esp_now_is_peer_exist(addr)) addPeer(mac);
    if (esp_now_send(addr, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK) {
      sent = true;
      queuedCount++;
    }
  }
  if (espNowDebugTransmissionEnabled() && millis() - lastDiagnosticTxSummaryLogMs > 10000UL) {
    lastDiagnosticTxSummaryLogMs = millis();
    state.addLog(String("ESP-NOW TX DIAGNOSTIC nodeId=") + String(packet.nodeId) + " len=" + String(sizeof(EspNowDiagnosticPacket)) + " peersQueued=" + String(queuedCount) + "/" + String(peerCount));
  }
  return sent;
}

bool EspNowManager::addSensorPacketValue(EspNowSensorPacket &packet, uint8_t valueType, const char *key, float value, const char *unit) {
  if (packet.valueCount >= ESPNOW_MAX_SENSOR_VALUES) return false;
  if (isnan(value) || isinf(value)) return false;
  EspNowSensorValue &slot = packet.values[packet.valueCount++];
  slot.valueType = valueType;
  espNowCopyFixedText(slot.key, sizeof(slot.key), key);
  slot.value = value;
  espNowCopyFixedText(slot.unit, sizeof(slot.unit), unit);
  return true;
}

bool EspNowManager::addFastPacketValue(EspNowFastSensorPacket &packet, uint8_t valueType, float value) {
  if (packet.valueCount >= ESPNOW_MAX_FAST_VALUES) return false;
  if (isnan(value) || isinf(value)) return false;
  EspNowCompactSensorValue &slot = packet.values[packet.valueCount++];
  slot.valueType = valueType;
  slot.value = value;
  return true;
}

bool EspNowManager::addSensorDiscoveryValue(EspNowSensorDiscoveryPacket &packet, uint8_t valueType, const char *key, const char *unit) {
  if (packet.valueCount >= ESPNOW_MAX_FAST_VALUES) return false;
  EspNowSensorDiscoveryValue &slot = packet.values[packet.valueCount++];
  slot.valueType = valueType;
  espNowCopyFixedText(slot.key, sizeof(slot.key), key);
  espNowCopyFixedText(slot.unit, sizeof(slot.unit), unit);
  return true;
}

bool EspNowManager::espNowDebugTransmissionEnabled() {
  return config.system()["espnow"]["debugTransmission"] | false;
}

bool EspNowManager::espNowDebugReceptionEnabled() {
  return config.system()["espnow"]["debugReception"] | false;
}

bool EspNowManager::espNowSensorDebugEnabledForMac(const String &sourceMac) {
  for (JsonObject sensor : config.sensors()) {
    if (!(sensor["enabled"] | true)) continue;
    String source = sensor["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) continue;
    String mac = sensor["mac"] | "";
    if (!mac.equalsIgnoreCase(sourceMac)) continue;
    if (sensor["debug"] | false) return true;
  }
  return false;
}

bool EspNowManager::debugEnabledForRemoteSensor(const String &sourceMac, uint8_t sensorId, uint8_t sensorType, String &configuredName) {
  for (JsonObject sensor : config.sensors()) {
    if (!(sensor["enabled"] | true)) continue;
    String source = sensor["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) continue;
    String mac = sensor["mac"] | "";
    if (!mac.equalsIgnoreCase(sourceMac)) continue;
    if (!(sensor["debug"] | false)) continue;
    if (sensor["remoteSensorId"].is<uint8_t>() && (sensor["remoteSensorId"] | 0) != sensorId) continue;

    String configuredType = sensor["type"] | "";
    configuredType.toUpperCase();
    bool typeMatches = false;
    if ((configuredType.indexOf("TIC") >= 0 || configuredType.indexOf("LINKY") >= 0) && sensorType == SENSOR_LINKY) typeMatches = true;
    if (configuredType.indexOf("JSY") >= 0 && sensorType == SENSOR_JSY) typeMatches = true;
    if ((configuredType.indexOf("DS18") >= 0 || configuredType.indexOf("TEMP") >= 0) && (sensorType == SENSOR_DS18B20 || sensorType == SENSOR_TEMP_HUM)) typeMatches = true;
    if (configuredType.indexOf("BAT") >= 0 && sensorType == SENSOR_BATTERY) typeMatches = true;
    if ((configuredType.indexOf("SOLAR") >= 0 || configuredType.indexOf("SOLAIRE") >= 0) && sensorType == SENSOR_SOLAR) typeMatches = true;
    if (configuredType.length() && !typeMatches) continue;

    configuredName = sensor["name"] | sensor["id"] | "";
    return true;
  }
  return false;
}

void EspNowManager::logDebugMapping(const String &message) {
  Serial.println(message);
  state.logEvent("INFO", "ESPNOW_SENSOR_MAP", message, "EspNow");
}

void EspNowManager::logDebugSensorPacketIfNeeded(const String &sourceMac, const EspNowSensorPacket &packet) {
  String sensorName = packet.sensorName;
  if (!sensorName.length()) sensorName = packet.nodeName;
  String configuredName;
  if (!debugEnabledForRemoteSensor(sourceMac, packet.sensorId, packet.sensorType, configuredName)) return;
  if (configuredName.length()) sensorName = configuredName;

  String line = "ESP-NOW decode ";
  line += sensorName.length() ? sensorName : String(packet.nodeName);
  line += " mac=";
  line += sourceMac;
  line += " seq=";
  line += String(packet.sequence);
  line += " sensorId=";
  line += String(packet.sensorId);
  line += " sensorName=";
  line += packet.sensorName;
  line += " ok=";
  line += packet.sensorOk ? "true" : "false";
  line += " type=";
  line += espNowSensorTypeText(packet.sensorType);
  line += " values:";

  for (uint8_t i = 0; i < packet.valueCount && i < ESPNOW_MAX_SENSOR_VALUES; i++) {
    line += " ";
    line += "vt";
    line += String(packet.values[i].valueType);
    line += "/";
    line += packet.values[i].key;
    line += "=";
    if (isnan(packet.values[i].value) || isinf(packet.values[i].value)) line += "N/A";
    else line += String(packet.values[i].value, 3);
    if (packet.values[i].unit[0]) line += packet.values[i].unit;
  }

  Serial.println(line);
  state.logEvent(packet.sensorOk ? "INFO" : "WARNING", "ESPNOW_SENSOR_DECODE", line, "EspNow");
}

void EspNowManager::logDebugFastSensorPacketIfNeeded(const String &sourceMac, const EspNowFastSensorPacket &packet, int len) {
  String sensorName;
  if (!espNowDebugReceptionEnabled() && !debugEnabledForRemoteSensor(sourceMac, packet.sensorId, packet.sensorType, sensorName)) return;
  if (!sensorName.length()) sensorName = String("sensorId ") + String(packet.sensorId);

  String line = "ESP-NOW FAST_DATA ";
  line += sensorName;
  line += " mac=";
  line += sourceMac;
  line += " len=";
  line += String(len);
  line += " nodeId=";
  line += String(packet.nodeId);
  line += " sensorId=";
  line += String(packet.sensorId);
  line += " seq=";
  line += String(packet.sequence);
  line += " remoteTsMs=";
  line += String(packet.timestampMs);
  line += " ok=";
  line += packet.sensorOk ? "true" : "false";
  line += " type=";
  line += espNowSensorTypeText(packet.sensorType);
  line += " checksum=";
  line += String(packet.checksum);
  line += "/";
  line += String(espNowCalculateChecksum(packet));
  line += " values:";

  for (uint8_t i = 0; i < packet.valueCount && i < ESPNOW_MAX_FAST_VALUES; i++) {
    line += " ";
    line += espNowValueTypeText(packet.values[i].valueType);
    line += "=";
    if (isnan(packet.values[i].value) || isinf(packet.values[i].value)) line += "N/A";
    else line += String(packet.values[i].value, 3);
  }

  Serial.println(line);
  state.logEvent(packet.sensorOk ? "INFO" : "WARNING", "ESPNOW_SENSOR_DECODE", line, "EspNow");
}

void EspNowManager::logDebugSensorDiscoveryPacketIfNeeded(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet, int len) {
  String sensorName = packet.sensorName;
  String configuredName;
  if (!espNowDebugReceptionEnabled() && !debugEnabledForRemoteSensor(sourceMac, packet.sensorId, packet.sensorType, configuredName)) return;
  if (configuredName.length()) sensorName = configuredName;
  if (!sensorName.length()) sensorName = String("sensorId ") + String(packet.sensorId);

  String line = "ESP-NOW SENSOR_DISCOVERY ";
  line += sensorName;
  line += " mac=";
  line += sourceMac;
  line += " len=";
  line += String(len);
  line += " nodeId=";
  line += String(packet.nodeId);
  line += " nodeName=";
  line += packet.nodeName;
  line += " sensorId=";
  line += String(packet.sensorId);
  line += " sensorName=";
  line += packet.sensorName;
  line += " role=";
  line += packet.sensorRole;
  line += " type=";
  line += espNowSensorTypeText(packet.sensorType);
  line += " firmware=";
  line += packet.firmwareVersion;
  line += " checksum=";
  line += String(packet.checksum);
  line += "/";
  line += String(espNowCalculateChecksum(packet));
  line += " values:";

  for (uint8_t i = 0; i < packet.valueCount && i < ESPNOW_MAX_FAST_VALUES; i++) {
    line += " ";
    line += espNowValueTypeText(packet.values[i].valueType);
    line += "/";
    line += packet.values[i].key;
    if (packet.values[i].unit[0]) {
      line += "(";
      line += packet.values[i].unit;
      line += ")";
    }
  }

  Serial.println(line);
  state.logEvent("INFO", "ESPNOW_SENSOR_DECODE", line, "EspNow");
}

void EspNowManager::logDebugDiagnosticPacketIfNeeded(const String &sourceMac, const EspNowDiagnosticPacket &packet, int len) {
  if (!espNowDebugReceptionEnabled() && !espNowSensorDebugEnabledForMac(sourceMac)) return;

  String line = "ESP-NOW DIAGNOSTIC mac=";
  line += sourceMac;
  line += " len=";
  line += String(len);
  line += " nodeId=";
  line += String(packet.nodeId);
  line += " uptimeMs=";
  line += String(packet.uptimeMs);
  line += " heap=";
  line += String(packet.freeHeap);
  line += " rssi=";
  line += String(packet.rssiDbm);
  line += " sendOk=";
  line += String(packet.sendOkCount);
  line += " sendFail=";
  line += String(packet.sendFailCount);
  line += " rx=";
  line += String(packet.receivedCount);
  line += " lost=";
  line += String(packet.lostPackets);
  line += " lastError=";
  line += String(packet.lastError);
  line += " checksum=";
  line += String(packet.checksum);
  line += "/";
  line += String(espNowCalculateChecksum(packet));

  Serial.println(line);
  state.logEvent("INFO", "ESPNOW_SENSOR_DECODE", line, "EspNow");
}

uint8_t EspNowManager::localRoleFlags() const {
  switch (state.role) {
    case ROLE_MASTER:
    case ROLE_BACKUP:
      return ESPNOW_ROLE_CONSUMER | ESPNOW_ROLE_ROUTER | ESPNOW_ROLE_ACTUATOR;
    case ROLE_NODE_SENSOR:
      return ESPNOW_ROLE_PRODUCER;
    case ROLE_NODE_ACTUATOR:
      return ESPNOW_ROLE_CONSUMER | ESPNOW_ROLE_ACTUATOR;
    case ROLE_NODE_MIXED:
      return ESPNOW_ROLE_PRODUCER | ESPNOW_ROLE_CONSUMER | ESPNOW_ROLE_ACTUATOR;
    default:
      return ESPNOW_ROLE_NONE;
  }
}

uint16_t EspNowManager::localCapabilityFlags() {
  uint16_t flags = ESPNOW_CAP_NONE;
  if (state.role == ROLE_MASTER || state.role == ROLE_BACKUP) flags |= ESPNOW_CAP_ROUTER;
  for (JsonObject sensor : config.sensors()) {
    String type = sensor["type"] | "";
    type.toUpperCase();
    if (type.indexOf("LINKY") >= 0 || type.indexOf("TIC") >= 0) flags |= ESPNOW_CAP_LINKY;
    if (type.indexOf("JSY") >= 0) flags |= ESPNOW_CAP_JSY;
    if (type.indexOf("DS18") >= 0 || type.indexOf("TEMP") >= 0) flags |= ESPNOW_CAP_TEMP;
  }
  for (JsonObject actuator : config.actuators()) {
    if ((actuator["enabled"] | false) || String(actuator["id"] | "").length()) flags |= ESPNOW_CAP_ACTUATOR;
  }
  return flags;
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
