#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#include "espnow_protocol.h"

struct EspNowNodeConfig {
  uint8_t nodeId = 0;
  const char *nodeName = "ESP_NODE";
  uint8_t roleFlags = ESPNOW_ROLE_PRODUCER;
  uint16_t capabilityFlags = ESPNOW_CAP_NONE;
  uint8_t primarySensorType = SENSOR_UNKNOWN;
  uint32_t announceIntervalMs = ESPNOW_DEFAULT_ANNOUNCE_INTERVAL_MS;
};

class EspNowNode {
public:
  typedef void (*SensorPacketHandler)(void *context, const uint8_t *mac, const EspNowSensorPacket &packet);
  typedef void (*DiscoveryHandler)(void *context, const EspNowDiscoveredNode &node);

  explicit EspNowNode(const EspNowNodeConfig &config);

  bool begin();
  void loop(uint32_t now = millis());
  bool addPeer(const uint8_t mac[6]);
  bool addBroadcastPeer();
  bool sendDiscovery();
  bool sendSensorPacket(const uint8_t receiverMac[6]);

  void clearPacket(uint8_t sensorId, const char *sensorName, uint8_t sensorType, bool sensorOk);
  void clearPacket(uint8_t sensorType, bool sensorOk);
  bool addValue(uint8_t valueType, const char *key, float value, const char *unit);
  bool addValue(const char *key, float value, const char *unit);
  EspNowSensorPacket &packet();
  const EspNowSensorPacket &packet() const;

  uint8_t discoveredCount() const;
  const EspNowDiscoveredNode *discoveredNode(uint8_t index) const;
  const EspNowDiscoveredNode *findDiscoveredNodeByMac(const uint8_t mac[6]) const;

  void setSensorPacketHandler(SensorPacketHandler handler, void *context);
  void setDiscoveryHandler(DiscoveryHandler handler, void *context);
  void onReceive(const uint8_t *mac, const uint8_t *data, int len);
  void onSent(const uint8_t *mac, esp_now_send_status_t status);
  void printDiscoveredNodes(Stream &out = Serial) const;
  static void printMac(Stream &out, const uint8_t mac[6]);

private:
  EspNowNodeConfig config;
  EspNowSensorPacket currentPacket{};
  EspNowDiscoveredNode discovered[ESPNOW_MAX_DISCOVERED_NODES]{};
  SensorPacketHandler sensorHandler = nullptr;
  void *sensorHandlerContext = nullptr;
  DiscoveryHandler discoveryHandler = nullptr;
  void *discoveryHandlerContext = nullptr;
  uint32_t sequenceCounter = 0;
  uint32_t discoverySequenceCounter = 0;
  uint32_t lastAnnounceMs = 0;
  bool ready = false;

  bool sendBytes(const uint8_t receiverMac[6], const uint8_t *data, size_t len);
  EspNowDiscoveredNode *rememberNode(const uint8_t *mac, const EspNowDiscoveryPacket &packet);
  void handleDiscovery(const uint8_t *mac, const uint8_t *data, int len);
  void handleSensorData(const uint8_t *mac, const uint8_t *data, int len);
  void logPacket(const EspNowSensorPacket &packet) const;
};

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void espNowNodeReceiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void espNowNodeSendCallback(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
void espNowNodeReceiveCallback(const uint8_t *mac, const uint8_t *data, int len);
void espNowNodeSendCallback(const uint8_t *mac, esp_now_send_status_t status);
#endif
