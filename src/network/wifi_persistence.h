#pragma once
#include "wifi_diagnostics.h"
#include <stddef.h>

struct WifiPersistedRecord {
  uint32_t magic = 0x57494649;
  uint32_t schema = 1;
  uint32_t bytes = 0;
  uint32_t generation = 0;
  uint32_t bootId = 0, total = 0, lastConnectedBoot = 0;
  uint8_t count = 0;
  bool hasLastConnected = false;
  WifiDiagnosticSnapshot lastState, lastConnected;
  WifiDiagnosticEvent events[32]{};
  uint32_t crc = 0;
};

inline uint32_t wifiDiagnosticCrc(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}
inline bool wifiGenerationNewer(uint32_t candidate, uint32_t previous) {
  return int32_t(candidate - previous) > 0;
}
inline bool wifiPersistedValid(const WifiPersistedRecord &record, size_t length) {
  return length == sizeof(WifiPersistedRecord) && record.magic == 0x57494649 && record.schema == 1 &&
         record.bytes == sizeof(WifiPersistedRecord) && record.count <= 32 &&
         record.crc == wifiDiagnosticCrc(reinterpret_cast<const uint8_t *>(&record), offsetof(WifiPersistedRecord, crc));
}
