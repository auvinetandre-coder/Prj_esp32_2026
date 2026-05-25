#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <WiFi.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"
#include "espnow_protocol.h"

class SensorManager;

enum EspNowMessageType {
  MSG_SENSOR_VALUE,
  MSG_ACTUATOR_COMMAND,
  MSG_HEARTBEAT,
  MSG_CONFIG_SYNC,
  MSG_STATUS,
  MSG_ERROR
};

struct EspNowMessage {
  uint8_t messageType = MSG_STATUS;
  uint8_t role = ROLE_NODE_MIXED;
  uint16_t checksum = 0;
  uint32_t sequenceNumber = 0;
  uint32_t timestamp = 0;
  uint32_t epoch = 0;
  uint32_t ttlMs = 1000;
  char senderId[18]{};
  char targetId[18]{};
  char masterId[18]{};
  char sensorId[24]{};
  char actuatorId[24]{};
  char variable[20]{};
  char command[24]{};
  char mode[20]{};
  float value = 0;
  float commandPercent = 0;
};

struct EspNowExportConfig {
  uint8_t sensorId = 0;
  char sensorName[20]{};
  char sensorRole[ESPNOW_SENSOR_ROLE_LEN]{};
  uint8_t sensorType = SENSOR_UNKNOWN;
  bool exportEnabled = false;
  uint32_t exportIntervalMs = 1000;
  uint8_t priority = PRIORITY_NORMAL;
  bool sendOnChange = false;
  float minDelta = 0.0f;
  uint32_t timeoutMs = 0;
};

class EspNowManager {
public:
  typedef bool (*ActuatorCommandHandler)(void *context, const String &actuatorId, const String &command, float value, const String &mode);

  EspNowManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}
  bool begin();
  bool initEspNow();
  void loop();
  bool addPeer(const String &mac);
  bool removePeer(const String &mac);
  bool isPeerKnown(const String &mac);
  bool sendMessage(const String &peer, EspNowMessage message);
  bool sendHeartbeat(const String &peer);
  bool sendSensorValue(const String &peer, const String &sensorId, const String &variable, float value);
  bool sendActuatorCommand(const String &peer, const String &actuatorId, float commandPercent, const String &mode, uint32_t ttlMs = 1000);
  bool sendStatus(const String &peer, const String &variable, float value);
  bool sendError(const String &peer, const String &text);
  void onReceive(const uint8_t *mac, const uint8_t *data, int len);
  void onSent(const uint8_t *mac, esp_now_send_status_t status);
  void handleReceive(const uint8_t *mac, const uint8_t *data, int len);
  void handleSendStatus(const uint8_t *mac, esp_now_send_status_t status);
  void setActuatorCommandHandler(ActuatorCommandHandler handler, void *context);
  void setSensorManager(SensorManager *manager) { sensorManager = manager; }
  String peersJson();
  String statusJson();
  String discoveredNodesJson();
  bool sendDiscovery();
  void printPeers();
  bool isSensorExportEnabled(uint8_t sensorId);
  uint32_t getSensorExportInterval(uint8_t sensorId);
  bool shouldExportSensor(uint8_t sensorId, float currentValue);
  void exportSensorIfNeeded(uint8_t sensorId);
  void sendSensorDiscovery(uint8_t sensorId);
  void sendSensorFastData(uint8_t sensorId);
  void sendSensorDiagnostic(uint8_t sensorId);

private:
  static const uint8_t MAX_EXPORT_RUNTIME_SLOTS = 12;

  ConfigManager &config;
  RuntimeState &state;
  SensorManager *sensorManager = nullptr;
  uint32_t sequenceCounter = 0;
  uint32_t lastRxLogMs = 0;
  uint32_t lastTxLogMs = 0;
  uint32_t discoverySequenceCounter = 0;
  uint32_t lastDiscoverySentMs = 0;
  uint32_t lastSensorPublishMs = 0;
  uint32_t lastSensorDiscoveryPublishMs = 0;
  uint32_t lastDiagnosticPublishMs = 0;
  uint32_t lastMapFailureLogMs = 0;
  uint32_t lastUnknownPeerLogMs = 0;
  uint32_t lastUnauthorizedSensorLogMs = 0;
  uint32_t lastSendFailureLogMs = 0;
  uint32_t lastSensorRxSummaryLogMs = 0;
  uint32_t lastFastTxSummaryLogMs = 0;
  uint32_t lastDiscoveryTxSummaryLogMs = 0;
  uint32_t lastDiagnosticTxSummaryLogMs = 0;
  uint32_t currentLoopNowMs = 0;
  uint8_t exportRuntimeSensorIds[MAX_EXPORT_RUNTIME_SLOTS]{};
  uint32_t exportLastSentMs[MAX_EXPORT_RUNTIME_SLOTS]{};
  float exportLastValues[MAX_EXPORT_RUNTIME_SLOTS]{};
  bool exportLastValueValid[MAX_EXPORT_RUNTIME_SLOTS]{};
  EspNowDiscoveredNode discoveredNodes[ESPNOW_MAX_DISCOVERED_NODES]{};
  ActuatorCommandHandler actuatorHandler = nullptr;
  void *actuatorHandlerContext = nullptr;

  bool parseMac(const String &mac, uint8_t out[6]);
  String macToString(const uint8_t *mac);
  void prepareMessage(EspNowMessage &message);
  uint16_t checksumFor(EspNowMessage message);
  bool checksumValid(const EspNowMessage &message);
  bool acceptActuatorCommand(const EspNowMessage &message, const String &mac);
  bool addBroadcastPeer();
  void handleDiscoveryPacket(const uint8_t *mac, const uint8_t *data, int len);
  void handleSensorPacket(const uint8_t *mac, const uint8_t *data, int len);
  void handleFastSensorPacket(const uint8_t *mac, const uint8_t *data, int len);
  void handleSensorDiscoveryPacket(const uint8_t *mac, const uint8_t *data, int len);
  void handleDiagnosticPacket(const uint8_t *mac, const uint8_t *data, int len);
  EspNowDiscoveredNode *rememberDiscoveredNode(const uint8_t *mac, const EspNowDiscoveryPacket &packet);
  void publishLocalSensorsIfNeeded(uint32_t now);
  void publishLocalSensorDiscoveryIfNeeded(uint32_t now);
  void publishDiagnosticIfNeeded(uint32_t now);
  EspNowExportConfig exportConfigFor(uint8_t sensorId, const char *defaultName = "", uint8_t defaultType = SENSOR_UNKNOWN);
  int8_t exportRuntimeIndex(uint8_t sensorId);
  int8_t ensureExportRuntimeIndex(uint8_t sensorId);
  bool currentExportValue(uint8_t sensorId, float &value);
  bool buildFastPacketForSensor(uint8_t sensorId, EspNowFastSensorPacket &packet, uint32_t now);
  bool buildDiscoveryPacketForSensor(uint8_t sensorId, EspNowSensorDiscoveryPacket &packet);
  const char *defaultExportSensorName(uint8_t sensorId);
  uint8_t defaultExportSensorType(uint8_t sensorId);
  bool sendSensorPacketToPeers(EspNowSensorPacket &packet);
  bool sendFastPacketToPeers(EspNowFastSensorPacket &packet);
  bool sendSensorDiscoveryToPeers(EspNowSensorDiscoveryPacket &packet);
  bool sendDiagnosticToPeers(EspNowDiagnosticPacket &packet);
  bool addSensorPacketValue(EspNowSensorPacket &packet, uint8_t valueType, const char *key, float value, const char *unit);
  bool addFastPacketValue(EspNowFastSensorPacket &packet, uint8_t valueType, float value);
  bool addSensorDiscoveryValue(EspNowSensorDiscoveryPacket &packet, uint8_t valueType, const char *key, const char *unit);
  bool espNowDebugTransmissionEnabled();
  bool espNowDebugReceptionEnabled();
  bool debugEnabledForRemoteSensor(const String &sourceMac, uint8_t sensorId, uint8_t sensorType, String &configuredName);
  void logDebugSensorPacketIfNeeded(const String &sourceMac, const EspNowSensorPacket &packet);
  void logDebugFastSensorPacketIfNeeded(const String &sourceMac, const EspNowFastSensorPacket &packet, int len);
  void logDebugSensorDiscoveryPacketIfNeeded(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet, int len);
  void logDebugDiagnosticPacketIfNeeded(const String &sourceMac, const EspNowDiagnosticPacket &packet, int len);
  bool espNowSensorDebugEnabledForMac(const String &sourceMac);
  void logDebugMapping(const String &message);
  uint8_t localRoleFlags() const;
  uint16_t localCapabilityFlags();
  const char *messageTypeText(uint8_t type);
};

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceiveMessage(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void onSendStatus(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
void onReceiveMessage(const uint8_t *mac, const uint8_t *data, int len);
void onSendStatus(const uint8_t *mac, esp_now_send_status_t status);
#endif
