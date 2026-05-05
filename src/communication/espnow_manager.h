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

class EspNowManager {
public:
  EspNowManager(ConfigManager &config, RuntimeState &state) : config(config), state(state) {}
  bool begin();
  bool initEspNow();
  void loop() {}
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
  String peersJson();
  void printPeers();

private:
  ConfigManager &config;
  RuntimeState &state;
  uint32_t sequenceCounter = 0;
  uint32_t lastRxLogMs = 0;
  uint32_t lastTxLogMs = 0;

  bool parseMac(const String &mac, uint8_t out[6]);
  String macToString(const uint8_t *mac);
  void prepareMessage(EspNowMessage &message);
  uint16_t checksumFor(EspNowMessage message);
  bool checksumValid(const EspNowMessage &message);
  bool acceptActuatorCommand(const EspNowMessage &message, const String &mac);
  const char *messageTypeText(uint8_t type);
};

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceiveMessage(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void onSendStatus(const wifi_tx_info_t *info, esp_now_send_status_t status);
#else
void onReceiveMessage(const uint8_t *mac, const uint8_t *data, int len);
void onSendStatus(const uint8_t *mac, esp_now_send_status_t status);
#endif
