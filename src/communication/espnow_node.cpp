#include "espnow_node.h"

static EspNowNode *gEspNowNode = nullptr;
static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

EspNowNode::EspNowNode(const EspNowNodeConfig &config) : config(config) {}

bool EspNowNode::begin() {
  gEspNowNode = this;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print(F("ESP-NOW node MAC source STA: "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("ESP-NOW nodeName="));
  Serial.print(config.nodeName);
  Serial.print(F(" nodeId="));
  Serial.print(config.nodeId);
  Serial.print(F(" roleFlags=0x"));
  Serial.print(config.roleFlags, HEX);
  Serial.print(F(" capabilityFlags=0x"));
  Serial.println(config.capabilityFlags, HEX);

  if (esp_now_init() != ESP_OK) {
    ready = false;
    Serial.println(F("ESP-NOW node init: ECHEC"));
    return false;
  }

  esp_now_register_recv_cb(espNowNodeReceiveCallback);
  esp_now_register_send_cb(espNowNodeSendCallback);

  ready = addBroadcastPeer();
  Serial.println(ready ? F("ESP-NOW node init: OK") : F("ESP-NOW broadcast peer: ECHEC"));
  if (ready) sendDiscovery();
  return ready;
}

void EspNowNode::loop(uint32_t now) {
  if (!ready) return;
  if (now - lastAnnounceMs < config.announceIntervalMs) return;
  sendDiscovery();
}

bool EspNowNode::addPeer(const uint8_t mac[6]) {
  if (!mac) return false;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_is_peer_exist(mac)) return true;
  return esp_now_add_peer(&peer) == ESP_OK;
}

bool EspNowNode::addBroadcastPeer() {
  return addPeer(ESPNOW_BROADCAST_MAC);
}

bool EspNowNode::sendDiscovery() {
  if (!ready) return false;

  EspNowDiscoveryPacket packet{};
  packet.version = ESPNOW_PROTOCOL_VERSION;
  packet.packetType = ESPNOW_PACKET_DISCOVERY;
  packet.nodeId = config.nodeId;
  espNowCopyFixedText(packet.nodeName, sizeof(packet.nodeName), config.nodeName);
  packet.roleFlags = config.roleFlags;
  packet.capabilityFlags = config.capabilityFlags;
  packet.primarySensorType = config.primarySensorType;
  WiFi.macAddress(packet.mac);
  packet.sequence = ++discoverySequenceCounter;
  packet.uptimeMs = millis();
  packet.checksum = espNowCalculateChecksum(packet);

  lastAnnounceMs = millis();
  Serial.print(F("ESP-NOW discovery TX nodeName="));
  Serial.print(packet.nodeName);
  Serial.print(F(" nodeId="));
  Serial.print(packet.nodeId);
  Serial.print(F(" sensorType="));
  Serial.println(espNowSensorTypeText(packet.primarySensorType));
  return sendBytes(ESPNOW_BROADCAST_MAC, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
}

bool EspNowNode::sendSensorPacket(const uint8_t receiverMac[6]) {
  if (!ready || !receiverMac) return false;
  if (!addPeer(receiverMac)) {
    Serial.print(F("ESP-NOW peer capteur refuse: "));
    printMac(Serial, receiverMac);
    Serial.println();
    return false;
  }

  currentPacket.timestampMs = millis();
  currentPacket.checksum = espNowCalculateChecksum(currentPacket);
  logPacket(currentPacket);
  return sendBytes(receiverMac, reinterpret_cast<const uint8_t *>(&currentPacket), sizeof(currentPacket));
}

void EspNowNode::clearPacket(uint8_t sensorType, bool sensorOk) {
  clearPacket(sensorType, espNowSensorTypeText(sensorType), sensorType, sensorOk);
}

void EspNowNode::clearPacket(uint8_t sensorId, const char *sensorName, uint8_t sensorType, bool sensorOk) {
  memset(&currentPacket, 0, sizeof(currentPacket));
  currentPacket.version = ESPNOW_PROTOCOL_VERSION;
  currentPacket.packetType = ESPNOW_PACKET_SENSOR_DATA;
  currentPacket.nodeId = config.nodeId;
  espNowCopyFixedText(currentPacket.nodeName, sizeof(currentPacket.nodeName), config.nodeName);
  currentPacket.sensorId = sensorId;
  espNowCopyFixedText(currentPacket.sensorName, sizeof(currentPacket.sensorName), sensorName);
  currentPacket.sensorType = sensorType;
  currentPacket.sequence = ++sequenceCounter;
  currentPacket.timestampMs = millis();
  currentPacket.sensorOk = sensorOk;
}

bool EspNowNode::addValue(const char *key, float value, const char *unit) {
  return addValue(espNowValueTypeFromKey(key), key, value, unit);
}

bool EspNowNode::addValue(uint8_t valueType, const char *key, float value, const char *unit) {
  if (currentPacket.valueCount >= ESPNOW_MAX_SENSOR_VALUES) {
    Serial.println(F("ESP-NOW valeur refusee: ESPNOW_MAX_SENSOR_VALUES atteint"));
    return false;
  }

  EspNowSensorValue &slot = currentPacket.values[currentPacket.valueCount];
  slot.valueType = valueType;
  espNowCopyFixedText(slot.key, sizeof(slot.key), key);
  slot.value = value;
  espNowCopyFixedText(slot.unit, sizeof(slot.unit), unit);
  currentPacket.valueCount++;
  return true;
}

EspNowSensorPacket &EspNowNode::packet() {
  return currentPacket;
}

const EspNowSensorPacket &EspNowNode::packet() const {
  return currentPacket;
}

uint8_t EspNowNode::discoveredCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (discovered[i].used) count++;
  }
  return count;
}

const EspNowDiscoveredNode *EspNowNode::discoveredNode(uint8_t index) const {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (!discovered[i].used) continue;
    if (seen == index) return &discovered[i];
    seen++;
  }
  return nullptr;
}

const EspNowDiscoveredNode *EspNowNode::findDiscoveredNodeByMac(const uint8_t mac[6]) const {
  if (!mac) return nullptr;
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (discovered[i].used && memcmp(discovered[i].mac, mac, 6) == 0) return &discovered[i];
  }
  return nullptr;
}

void EspNowNode::setSensorPacketHandler(SensorPacketHandler handler, void *context) {
  sensorHandler = handler;
  sensorHandlerContext = context;
}

void EspNowNode::setDiscoveryHandler(DiscoveryHandler handler, void *context) {
  discoveryHandler = handler;
  discoveryHandlerContext = context;
}

void EspNowNode::onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (!mac || !data || len < 2) return;
  const uint8_t version = data[0];
  const uint8_t packetType = data[1];
  if (version != ESPNOW_PROTOCOL_VERSION) {
    Serial.println(F("ESP-NOW RX ignore: version protocole incompatible"));
    return;
  }

  if (packetType == ESPNOW_PACKET_DISCOVERY) {
    handleDiscovery(mac, data, len);
  } else if (packetType == ESPNOW_PACKET_SENSOR_DATA) {
    handleSensorData(mac, data, len);
  }
}

void EspNowNode::onSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print(F("ESP-NOW TX callback peer="));
  if (mac) printMac(Serial, mac);
  else Serial.print(F("inconnu"));
  Serial.print(F(" resultat="));
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? F("OK") : F("ECHEC"));
}

void EspNowNode::printDiscoveredNodes(Stream &out) const {
  out.println(F("=== ESP-NOW noeuds detectes ==="));
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (!discovered[i].used) continue;
    printMac(out, discovered[i].mac);
    out.print(F(" nodeId="));
    out.print(discovered[i].nodeId);
    out.print(F(" nodeName="));
    out.print(discovered[i].nodeName);
    out.print(F(" roleFlags=0x"));
    out.print(discovered[i].roleFlags, HEX);
    out.print(F(" capabilityFlags=0x"));
    out.print(discovered[i].capabilityFlags, HEX);
    out.print(F(" primarySensor="));
    out.print(espNowSensorTypeText(discovered[i].primarySensorType));
    out.print(F(" lastSeenMs="));
    out.println(discovered[i].lastSeenMs);
  }
}

void EspNowNode::printMac(Stream &out, const uint8_t mac[6]) {
  for (uint8_t i = 0; i < 6; i++) {
    if (i) out.print(':');
    if (mac[i] < 16) out.print('0');
    out.print(mac[i], HEX);
  }
}

bool EspNowNode::sendBytes(const uint8_t receiverMac[6], const uint8_t *data, size_t len) {
  if (!receiverMac || !data || len == 0 || len > 250) return false;
  const esp_err_t result = esp_now_send(receiverMac, data, len);
  if (result != ESP_OK) {
    Serial.print(F("ESP-NOW TX ECHEC immediat code="));
    Serial.println(static_cast<int>(result));
    return false;
  }
  return true;
}

EspNowDiscoveredNode *EspNowNode::rememberNode(const uint8_t *mac, const EspNowDiscoveryPacket &packet) {
  EspNowDiscoveredNode *freeSlot = nullptr;
  for (uint8_t i = 0; i < ESPNOW_MAX_DISCOVERED_NODES; i++) {
    if (discovered[i].used && memcmp(discovered[i].mac, mac, 6) == 0) {
      freeSlot = &discovered[i];
      break;
    }
    if (!discovered[i].used && !freeSlot) freeSlot = &discovered[i];
  }
  if (!freeSlot) return nullptr;

  freeSlot->used = true;
  memcpy(freeSlot->mac, mac, 6);
  freeSlot->nodeId = packet.nodeId;
  espNowCopyFixedText(freeSlot->nodeName, sizeof(freeSlot->nodeName), packet.nodeName);
  freeSlot->roleFlags = packet.roleFlags;
  freeSlot->capabilityFlags = packet.capabilityFlags;
  freeSlot->primarySensorType = packet.primarySensorType;
  freeSlot->lastSeenMs = millis();
  freeSlot->lastSequence = packet.sequence;
  return freeSlot;
}

void EspNowNode::handleDiscovery(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != static_cast<int>(sizeof(EspNowDiscoveryPacket))) {
    Serial.println(F("ESP-NOW discovery RX ignore: taille invalide"));
    return;
  }

  EspNowDiscoveryPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet)) {
    Serial.println(F("ESP-NOW discovery RX ignore: checksum invalide"));
    return;
  }

  const uint8_t *nodeMac = packet.mac[0] || packet.mac[1] || packet.mac[2] || packet.mac[3] || packet.mac[4] || packet.mac[5]
                               ? packet.mac
                               : mac;
  EspNowDiscoveredNode *node = rememberNode(nodeMac, packet);
  if (!node) {
    Serial.println(F("ESP-NOW discovery RX ignore: table pleine"));
    return;
  }

  Serial.print(F("ESP-NOW discovery RX "));
  Serial.print(node->nodeName);
  Serial.print(F(" "));
  printMac(Serial, node->mac);
  Serial.print(F(" primarySensor="));
  Serial.println(espNowSensorTypeText(node->primarySensorType));

  if (discoveryHandler) discoveryHandler(discoveryHandlerContext, *node);
}

void EspNowNode::handleSensorData(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != static_cast<int>(sizeof(EspNowSensorPacket))) {
    Serial.println(F("ESP-NOW sensor RX ignore: taille invalide"));
    return;
  }

  EspNowSensorPacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!espNowChecksumValid(packet)) {
    Serial.println(F("ESP-NOW sensor RX ignore: checksum invalide"));
    return;
  }
  if (packet.valueCount > ESPNOW_MAX_SENSOR_VALUES) {
    Serial.println(F("ESP-NOW sensor RX ignore: valueCount invalide"));
    return;
  }

  Serial.print(F("ESP-NOW sensor RX nodeName="));
  Serial.print(packet.nodeName);
  Serial.print(F(" sensorName="));
  Serial.print(packet.sensorName);
  Serial.print(F(" sensorType="));
  Serial.print(espNowSensorTypeText(packet.sensorType));
  Serial.print(F(" valueCount="));
  Serial.println(packet.valueCount);

  if (sensorHandler) sensorHandler(sensorHandlerContext, mac, packet);
}

void EspNowNode::logPacket(const EspNowSensorPacket &packet) const {
  Serial.println(F("----- ESP-NOW SensorPacket TX -----"));
  Serial.print(F("sequence="));
  Serial.println(packet.sequence);
  Serial.print(F("nodeName="));
  Serial.println(packet.nodeName);
  Serial.print(F("sensorId="));
  Serial.println(packet.sensorId);
  Serial.print(F("sensorName="));
  Serial.println(packet.sensorName);
  Serial.print(F("sensorType="));
  Serial.println(espNowSensorTypeText(packet.sensorType));
  Serial.print(F("valueCount="));
  Serial.println(packet.valueCount);
  for (uint8_t i = 0; i < packet.valueCount && i < ESPNOW_MAX_SENSOR_VALUES; i++) {
    Serial.print(F("  "));
    Serial.print(F("vt"));
    Serial.print(packet.values[i].valueType);
    Serial.print('/');
    Serial.print(packet.values[i].key);
    Serial.print(F(" = "));
    Serial.print(packet.values[i].value, 3);
    Serial.print(' ');
    Serial.println(packet.values[i].unit);
  }
  Serial.print(F("packetSize="));
  Serial.print(sizeof(EspNowSensorPacket));
  Serial.println(F(" bytes"));
  Serial.print(F("checksum="));
  Serial.println(packet.checksum);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void espNowNodeReceiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (gEspNowNode && info) gEspNowNode->onReceive(info->src_addr, data, len);
}

void espNowNodeSendCallback(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (gEspNowNode) gEspNowNode->onSent(info ? info->des_addr : nullptr, status);
}
#else
void espNowNodeReceiveCallback(const uint8_t *mac, const uint8_t *data, int len) {
  if (gEspNowNode) gEspNowNode->onReceive(mac, data, len);
}

void espNowNodeSendCallback(const uint8_t *mac, esp_now_send_status_t status) {
  if (gEspNowNode) gEspNowNode->onSent(mac, status);
}
#endif
