#pragma once

#include <Arduino.h>
#include <stddef.h>

static const uint8_t ESPNOW_PROTOCOL_VERSION = 3;
static const uint8_t ESPNOW_MAX_SENSOR_VALUES = 8;
static const uint8_t ESPNOW_MAX_FAST_VALUES = 6;
static const uint8_t ESPNOW_SENSOR_NAME_LEN = 12;
static const uint8_t ESPNOW_SENSOR_ROLE_LEN = 24;
static const uint8_t ESPNOW_MAX_DISCOVERED_NODES = 12;
static const uint32_t ESPNOW_DEFAULT_ANNOUNCE_INTERVAL_MS = 3000;

enum EspNowPacketType : uint8_t {
  ESPNOW_PACKET_UNKNOWN = 0,
  ESPNOW_PACKET_DISCOVERY = 1,
  ESPNOW_PACKET_SENSOR_DATA = 2,
  ESPNOW_PACKET_FAST_DATA = 3,
  ESPNOW_PACKET_DIAGNOSTIC = 4,
  ESPNOW_PACKET_SENSOR_DISCOVERY = 5
};

enum EspNowFrameType : uint8_t {
  FRAME_UNKNOWN = ESPNOW_PACKET_UNKNOWN,
  FRAME_DISCOVERY = ESPNOW_PACKET_DISCOVERY,
  FRAME_LEGACY_SENSOR_DATA = ESPNOW_PACKET_SENSOR_DATA,
  FRAME_FAST_DATA = ESPNOW_PACKET_FAST_DATA,
  FRAME_DIAGNOSTIC = ESPNOW_PACKET_DIAGNOSTIC,
  FRAME_SENSOR_DISCOVERY = ESPNOW_PACKET_SENSOR_DISCOVERY
};

enum EspNowNodeRole : uint8_t {
  ESPNOW_ROLE_NONE = 0x00,
  ESPNOW_ROLE_PRODUCER = 0x01,
  ESPNOW_ROLE_CONSUMER = 0x02,
  ESPNOW_ROLE_ROUTER = 0x04,
  ESPNOW_ROLE_ACTUATOR = 0x08
};

enum EspNowSensorType : uint8_t {
  SENSOR_UNKNOWN = 0,
  SENSOR_LINKY = 1,
  SENSOR_JSY = 2,
  SENSOR_DS18B20 = 3,
  SENSOR_TEMP_HUM = 4,
  SENSOR_BATTERY = 5,
  SENSOR_SOLAR = 6,
  SENSOR_RELAY_STATUS = 7,
  SENSOR_ROUTER = 8,
  SENSOR_CUSTOM = 255
};

static const uint8_t SENSOR_TEMP = SENSOR_TEMP_HUM;

enum SensorOrigin : uint8_t {
  SENSOR_ORIGIN_LOCAL = 0,
  SENSOR_ORIGIN_ESPNOW = 1
};

enum EspNowSensorValueType : uint8_t {
  VALUE_UNKNOWN = 0,
  VALUE_POWER_W = 1,
  VALUE_GRID_POWER_W = 2,
  VALUE_VOLTAGE_V = 3,
  VALUE_CURRENT_A = 4,
  VALUE_APPARENT_POWER_VA = 5,
  VALUE_POWER_FACTOR = 6,
  VALUE_FREQUENCY_HZ = 7,
  VALUE_TEMPERATURE_C = 8,
  VALUE_HUMIDITY_PERCENT = 9,
  VALUE_ENERGY_KWH = 10,
  VALUE_BATTERY_VOLTAGE_V = 11,
  VALUE_BATTERY_CURRENT_A = 12,
  VALUE_BATTERY_SOC_PERCENT = 13,
  VALUE_STATE_BOOL = 14,
  VALUE_RSSI_DBM = 15,
  VALUE_CUSTOM = 255
};

enum EspNowSensorPriority : uint8_t {
  PRIORITY_LOW = 0,
  PRIORITY_NORMAL = 1,
  PRIORITY_HIGH = 2,
  PRIORITY_CRITICAL = 3
};

enum EspNowCapability : uint16_t {
  ESPNOW_CAP_NONE = 0x0000,
  ESPNOW_CAP_LINKY = 0x0001,
  ESPNOW_CAP_JSY = 0x0002,
  ESPNOW_CAP_TEMP = 0x0004,
  ESPNOW_CAP_BATTERY = 0x0008,
  ESPNOW_CAP_SOLAR = 0x0010,
  ESPNOW_CAP_ROUTER = 0x0020,
  ESPNOW_CAP_ACTUATOR = 0x0040
};

typedef struct __attribute__((packed)) {
  uint8_t valueType;
  char key[12];
  float value;
  char unit[8];
} EspNowSensorValue;

typedef struct __attribute__((packed)) {
  uint8_t valueType;
  float value;
} EspNowCompactSensorValue;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  char nodeName[20];
  uint8_t sensorId;
  char sensorName[ESPNOW_SENSOR_NAME_LEN];
  uint8_t sensorType;
  uint32_t sequence;
  uint32_t timestampMs;
  bool sensorOk;
  uint8_t valueCount;
  EspNowSensorValue values[ESPNOW_MAX_SENSOR_VALUES];
  uint16_t checksum;
} EspNowSensorPacket;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t sensorType;
  uint32_t sequence;
  uint32_t timestampMs;
  bool sensorOk;
  uint8_t valueCount;
  EspNowCompactSensorValue values[ESPNOW_MAX_FAST_VALUES];
  uint16_t checksum;
} EspNowFastSensorPacket;

typedef struct __attribute__((packed)) {
  uint8_t valueType;
  char key[12];
  char unit[8];
} EspNowSensorDiscoveryValue;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  char nodeName[20];
  uint8_t sensorId;
  char sensorName[ESPNOW_SENSOR_NAME_LEN];
  char sensorRole[ESPNOW_SENSOR_ROLE_LEN];
  uint8_t sensorType;
  char firmwareVersion[12];
  uint8_t valueCount;
  EspNowSensorDiscoveryValue values[ESPNOW_MAX_FAST_VALUES];
  uint16_t checksum;
} EspNowSensorDiscoveryPacket;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  uint32_t uptimeMs;
  uint32_t freeHeap;
  int8_t rssiDbm;
  uint32_t sendOkCount;
  uint32_t sendFailCount;
  uint32_t receivedCount;
  uint32_t lostPackets;
  uint8_t lastError;
  uint16_t checksum;
} EspNowDiagnosticPacket;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  char nodeName[20];
  uint8_t roleFlags;
  uint16_t capabilityFlags;
  uint8_t primarySensorType;
  uint8_t mac[6];
  uint32_t sequence;
  uint32_t uptimeMs;
  uint16_t checksum;
} EspNowDiscoveryPacket;

struct EspNowDiscoveredNode {
  bool used = false;
  uint8_t mac[6]{};
  uint8_t nodeId = 0;
  char nodeName[20]{};
  uint8_t roleFlags = ESPNOW_ROLE_NONE;
  uint16_t capabilityFlags = ESPNOW_CAP_NONE;
  uint8_t primarySensorType = SENSOR_UNKNOWN;
  uint32_t lastSeenMs = 0;
  uint32_t lastSequence = 0;
};

static_assert(sizeof(EspNowSensorPacket) <= 250, "EspNowSensorPacket depasse la limite ESP-NOW de 250 octets.");
static_assert(sizeof(EspNowFastSensorPacket) <= 250, "EspNowFastSensorPacket depasse la limite ESP-NOW de 250 octets.");
static_assert(sizeof(EspNowSensorDiscoveryPacket) <= 250, "EspNowSensorDiscoveryPacket depasse la limite ESP-NOW de 250 octets.");
static_assert(sizeof(EspNowDiagnosticPacket) <= 250, "EspNowDiagnosticPacket depasse la limite ESP-NOW de 250 octets.");
static_assert(sizeof(EspNowDiscoveryPacket) <= 250, "EspNowDiscoveryPacket depasse la limite ESP-NOW de 250 octets.");

inline void espNowCopyFixedText(char *dest, size_t destSize, const char *src) {
  if (!dest || destSize == 0) return;
  strncpy(dest, src ? src : "", destSize - 1);
  dest[destSize - 1] = '\0';
}

inline uint16_t espNowChecksumBytes(const uint8_t *bytes, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) sum = static_cast<uint16_t>(sum + bytes[i]);
  return sum;
}

inline uint16_t espNowCalculateChecksum(const EspNowSensorPacket &packet) {
  return espNowChecksumBytes(reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowSensorPacket, checksum));
}

inline uint16_t espNowCalculateChecksum(const EspNowFastSensorPacket &packet) {
  return espNowChecksumBytes(reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowFastSensorPacket, checksum));
}

inline uint16_t espNowCalculateChecksum(const EspNowSensorDiscoveryPacket &packet) {
  return espNowChecksumBytes(reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowSensorDiscoveryPacket, checksum));
}

inline uint16_t espNowCalculateChecksum(const EspNowDiagnosticPacket &packet) {
  return espNowChecksumBytes(reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowDiagnosticPacket, checksum));
}

inline uint16_t espNowCalculateChecksum(const EspNowDiscoveryPacket &packet) {
  return espNowChecksumBytes(reinterpret_cast<const uint8_t *>(&packet), offsetof(EspNowDiscoveryPacket, checksum));
}

inline bool espNowChecksumValid(const EspNowSensorPacket &packet) {
  return packet.checksum == espNowCalculateChecksum(packet);
}

inline bool espNowChecksumValid(const EspNowFastSensorPacket &packet) {
  return packet.checksum == espNowCalculateChecksum(packet);
}

inline bool espNowChecksumValid(const EspNowSensorDiscoveryPacket &packet) {
  return packet.checksum == espNowCalculateChecksum(packet);
}

inline bool espNowChecksumValid(const EspNowDiagnosticPacket &packet) {
  return packet.checksum == espNowCalculateChecksum(packet);
}

inline bool espNowChecksumValid(const EspNowDiscoveryPacket &packet) {
  return packet.checksum == espNowCalculateChecksum(packet);
}

inline const char *espNowSensorTypeText(uint8_t sensorType) {
  switch (sensorType) {
    case SENSOR_LINKY: return "LINKY";
    case SENSOR_JSY: return "JSY";
    case SENSOR_DS18B20: return "DS18B20";
    case SENSOR_TEMP_HUM: return "TEMP_HUM";
    case SENSOR_BATTERY: return "BATTERY";
    case SENSOR_SOLAR: return "SOLAR";
    case SENSOR_RELAY_STATUS: return "RELAY_STATUS";
    case SENSOR_ROUTER: return "ROUTER";
    case SENSOR_CUSTOM: return "CUSTOM";
    default: return "UNKNOWN";
  }
}

inline uint8_t espNowValueTypeFromKey(const char *key) {
  if (!key) return VALUE_UNKNOWN;
  if (strcmp(key, "GRID") == 0) return VALUE_GRID_POWER_W;
  if (strcmp(key, "POWER") == 0 || strcmp(key, "BATP") == 0) return VALUE_POWER_W;
  if (strcmp(key, "VOLT") == 0) return VALUE_VOLTAGE_V;
  if (strcmp(key, "CURR") == 0) return VALUE_CURRENT_A;
  if (strcmp(key, "PAPP") == 0 || strcmp(key, "SINSTS") == 0) return VALUE_APPARENT_POWER_VA;
  if (strcmp(key, "PF") == 0) return VALUE_POWER_FACTOR;
  if (strcmp(key, "FREQ") == 0) return VALUE_FREQUENCY_HZ;
  if (strcmp(key, "TEMP") == 0) return VALUE_TEMPERATURE_C;
  if (strcmp(key, "HUM") == 0) return VALUE_HUMIDITY_PERCENT;
  if (strcmp(key, "ENERGY") == 0 || strcmp(key, "BASE") == 0) return VALUE_ENERGY_KWH;
  if (strcmp(key, "BATV") == 0) return VALUE_BATTERY_VOLTAGE_V;
  if (strcmp(key, "BATA") == 0) return VALUE_BATTERY_CURRENT_A;
  if (strcmp(key, "SOC") == 0) return VALUE_BATTERY_SOC_PERCENT;
  if (strcmp(key, "RELAY") == 0 || strcmp(key, "STATE") == 0) return VALUE_STATE_BOOL;
  return VALUE_CUSTOM;
}
