#pragma once
#include <Arduino.h>

// POD diagnostics only: no credentials, String, heap allocation or flash writes.
struct WifiDiagnosticEvent {
  uint64_t epochMs = 0;
  uint32_t ms = 0;
  uint32_t sequence = 0;
  uint32_t bootId = 0;
  uint16_t event = 0;
  uint16_t reason = 0;
  uint8_t channel = 0;
  int16_t rssi = 0;
  bool rssiValid = false;
  uint8_t bssid[6]{};
};
struct WifiDiagnosticSnapshot {
  uint32_t sampledMs = 0;
  uint32_t gotIpMs = 0, disconnectedMs = 0, lastOutageMs = 0;
  uint32_t downSinceMs = 0;
  bool outageActive = false, hasCompletedOutage = false;
  uint32_t connections = 0, disconnects = 0, lostIp = 0;
  uint32_t ip = 0, gateway = 0, subnet = 0, dns1 = 0, dns2 = 0;
  uint16_t lastReason = 0;
  int16_t rssi = 0, rssiMin = 0, rssiMax = 0;
  uint8_t bssid[6]{};
  uint8_t status = 0, mode = 0, channel = 0, apClients = 0;
  bool associated = false, hasIp = false, hadIp = false, hasDisconnect = false;
  bool apActive = false, rssiValid = false, hasRssiRange = false;
};
