#include "wifi_manager.h"
#include "../diagnostics/console_serial.h"
#include <esp_wifi.h>

void SolarWiFiManager::recordDiagnosticEvent(WiFiEvent_t event, const WiFiEventInfo_t &info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START: case ARDUINO_EVENT_WIFI_STA_STOP:
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_AP_START: case ARDUINO_EVENT_WIFI_AP_STOP:
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: break;
    default: return;
  }
  const uint32_t now = millis();
  const uint64_t epoch = consoleEpochMs();
  const uint16_t reason = event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ? info.wifi_sta_disconnected.reason : 0;
  const uint8_t channel = event == ARDUINO_EVENT_WIFI_STA_CONNECTED ? info.wifi_sta_connected.channel : 0;
  // Wi-Fi callback task: fixed-size writes only. No Serial, JSON, credentials or filesystem.
  portENTER_CRITICAL(&diagnosticMux);
  WifiDiagnosticSnapshot &d = diagnosticState;
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) d.associated = true;
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event == ARDUINO_EVENT_WIFI_STA_STOP || event == ARDUINO_EVENT_WIFI_STA_LOST_IP) {
    if (d.hasIp && !d.outageActive) { d.downSinceMs = now; d.outageActive = true; }
    d.hasIp = false;
    if (event != ARDUINO_EVENT_WIFI_STA_LOST_IP) d.associated = false;
  }
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    d.disconnectedMs = now; d.hasDisconnect = true; d.lastReason = reason; ++d.disconnects;
    state.wifiLastDisconnectReason = reason; state.wifiLastDisconnectMs = now; ++state.wifiDisconnectCount;
  }
  if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) ++d.lostIp;
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    if (!d.hasIp) { d.gotIpMs = now; ++d.connections; }
    if (d.outageActive) { d.lastOutageMs = uint32_t(now - d.downSinceMs); d.outageActive = false; d.hasCompletedOutage = true; }
    d.associated = true; d.hasIp = true; d.hadIp = true;
  }
  if (event == ARDUINO_EVENT_WIFI_AP_START) d.apActive = true;
  if (event == ARDUINO_EVENT_WIFI_AP_STOP) d.apActive = false;
  WifiDiagnosticEvent &entry = diagnosticEvents[diagnosticHead];
  entry.epochMs = epoch; entry.ms = now; entry.sequence = ++diagnosticTotal; entry.bootId = diagnosticBootId;
  entry.event = static_cast<uint16_t>(event); entry.reason = reason; entry.channel = channel;
  entry.rssiValid = event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED;
  entry.rssi = entry.rssiValid ? info.wifi_sta_disconnected.rssi : 0;
  memset(entry.bssid, 0, sizeof(entry.bssid));
  if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) memcpy(entry.bssid, info.wifi_sta_connected.bssid, sizeof(entry.bssid));
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) memcpy(entry.bssid, info.wifi_sta_disconnected.bssid, sizeof(entry.bssid));
  diagnosticHead = (diagnosticHead + 1) % diagnosticCapacity;
  if (diagnosticCount < diagnosticCapacity) ++diagnosticCount;
  portEXIT_CRITICAL(&diagnosticMux);
}

void SolarWiFiManager::sampleDiagnostics(uint32_t now) {
  if (diagnosticSampled && uint32_t(now - lastDiagnosticSampleMs) < 1000UL) return;
  diagnosticSampled = true; lastDiagnosticSampleMs = now;
  const uint8_t status = WiFi.status(), mode = WiFi.getMode();
  const bool connected = status == WL_CONNECTED;
  const bool ap = mode == WIFI_AP || mode == WIFI_AP_STA;
  const int16_t rssi = connected ? WiFi.RSSI() : 0;
  const uint8_t channel = WiFi.channel(), clients = ap ? WiFi.softAPgetStationNum() : 0;
  const uint32_t ip = connected ? uint32_t(WiFi.localIP()) : 0;
  const uint32_t gateway = connected ? uint32_t(WiFi.gatewayIP()) : 0;
  const uint32_t subnet = connected ? uint32_t(WiFi.subnetMask()) : 0;
  const uint32_t dns1 = connected ? uint32_t(WiFi.dnsIP(0)) : 0, dns2 = connected ? uint32_t(WiFi.dnsIP(1)) : 0;
  uint8_t bssid[6]{};
  if (connected) { const uint8_t *current = WiFi.BSSID(); if (current) memcpy(bssid, current, sizeof(bssid)); }
  portENTER_CRITICAL(&diagnosticMux);
  WifiDiagnosticSnapshot &d = diagnosticState;
  d.sampledMs = now; d.status = status; d.mode = mode; d.channel = channel;
  d.apActive = ap; d.apClients = clients; d.rssi = rssi; d.rssiValid = connected;
  d.ip = ip; d.gateway = gateway; d.subnet = subnet; d.dns1 = dns1; d.dns2 = dns2;
  memcpy(d.bssid, bssid, sizeof(bssid));
  if (connected) {
    if (!d.hasRssiRange) { d.rssiMin = rssi; d.rssiMax = rssi; d.hasRssiRange = true; }
    else { if (rssi < d.rssiMin) d.rssiMin = rssi; if (rssi > d.rssiMax) d.rssiMax = rssi; }
    lastConnectedState = d; lastConnectedAvailable = true; diagnosticLastConnectedBoot = diagnosticBootId;
  }
  portEXIT_CRITICAL(&diagnosticMux);
}

uint8_t SolarWiFiManager::copyDiagnostics(WifiDiagnosticSnapshot &snapshot, WifiDiagnosticEvent *events, uint8_t limit, uint32_t &total) {
  // Same core/loop as WebServer and SerialDebug; sampling does not reconfigure Wi-Fi.
  sampleDiagnostics(millis());
  portENTER_CRITICAL(&diagnosticMux);
  snapshot = diagnosticState; total = diagnosticTotal;
  uint8_t count = events ? (diagnosticCount < limit ? diagnosticCount : limit) : 0;
  for (uint8_t i = 0; i < count; ++i) events[i] = diagnosticEvents[(diagnosticHead + diagnosticCapacity - count + i) % diagnosticCapacity];
  portEXIT_CRITICAL(&diagnosticMux);
  return count;
}

bool SolarWiFiManager::copyLastConnected(WifiDiagnosticSnapshot &snapshot) {
  portENTER_CRITICAL(&diagnosticMux);
  const bool available = lastConnectedAvailable;
  if (available) snapshot = lastConnectedState;
  portEXIT_CRITICAL(&diagnosticMux);
  return available;
}

const char *SolarWiFiManager::diagnosticEventName(uint16_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START: return "STA_DEMARREE";
    case ARDUINO_EVENT_WIFI_STA_STOP: return "STA_ARRETEE";
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: return "ASSOCIE_AU_POINT_ACCES";
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: return "DECONNECTE";
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: return "IP_OBTENUE";
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: return "IP_PERDUE";
    case ARDUINO_EVENT_WIFI_AP_START: return "AP_SECOURS_DEMARRE";
    case ARDUINO_EVENT_WIFI_AP_STOP: return "AP_SECOURS_ARRETE";
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: return "CLIENT_AP_CONNECTE";
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: return "CLIENT_AP_DECONNECTE";
    default: return "AUTRE";
  }
}
const char *SolarWiFiManager::diagnosticReasonName(uint16_t reason) {
  switch (reason) {
    case 0: return "AUCUN";
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIREE";
    case WIFI_REASON_BEACON_TIMEOUT: return "BALISES_NON_RECUES";
    case WIFI_REASON_NO_AP_FOUND: return "POINT_ACCES_INTROUVABLE";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_ECHOUEE";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOCIATION_ECHOUEE";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "NEGOCIATION_EXPIREE";
    case WIFI_REASON_ASSOC_LEAVE: return "DEPART_STATION";
    default: return "AUTRE_CODE_PILOTE";
  }
}

void SolarWiFiManager::diagnosticsToJson(JsonObject out) {
  WifiDiagnosticSnapshot d;
  WifiDiagnosticEvent events[diagnosticCapacity];
  uint32_t total;
  const uint8_t count = copyDiagnostics(d, events, diagnosticCapacity, total);
  const uint32_t now = millis();
  out["sampleAgeMs"] = uint32_t(now - d.sampledMs);
  out["statusCode"] = d.status; out["modeCode"] = d.mode;
  out["associated"] = d.associated; out["ipAcquired"] = d.hasIp;
  out["connected"] = d.hasIp && d.status == WL_CONNECTED && d.ip != 0;
  out["channel"] = d.channel;
  if (d.rssiValid) out["rssi"] = d.rssi; else out["rssi"] = nullptr;
  if (d.hasRssiRange) { out["rssiMin"] = d.rssiMin; out["rssiMax"] = d.rssiMax; }
  else { out["rssiMin"] = nullptr; out["rssiMax"] = nullptr; }
  char bssid[18]{};
  if (d.rssiValid) snprintf(bssid,sizeof(bssid),"%02X:%02X:%02X:%02X:%02X:%02X",d.bssid[0],d.bssid[1],d.bssid[2],d.bssid[3],d.bssid[4],d.bssid[5]);
  out["bssid"] = bssid;
  out["ip"] = IPAddress(d.ip).toString(); out["gateway"] = IPAddress(d.gateway).toString();
  out["subnet"] = IPAddress(d.subnet).toString(); out["dns1"] = IPAddress(d.dns1).toString(); out["dns2"] = IPAddress(d.dns2).toString();
  out["apActive"] = d.apActive; out["apClients"] = d.apClients;
  out["ipAcquisitions"] = d.connections; out["disconnects"] = d.disconnects; out["lostIpEvents"] = d.lostIp;
  if (d.hasIp) out["connectedForMs"] = uint32_t(now - d.gotIpMs); else out["connectedForMs"] = nullptr;
  out["outageActive"] = d.outageActive;
  if (d.outageActive) out["outageForMs"] = uint32_t(now - d.downSinceMs); else out["outageForMs"] = nullptr;
  if (d.hasCompletedOutage) out["lastOutageMs"] = d.lastOutageMs; else out["lastOutageMs"] = nullptr;
  if (d.hasDisconnect) { out["lastDisconnectAgeMs"] = uint32_t(now - d.disconnectedMs); out["lastReason"] = d.lastReason; out["lastReasonLabel"] = diagnosticReasonName(d.lastReason); }
  else { out["lastDisconnectAgeMs"] = nullptr; out["lastReason"] = nullptr; out["lastReasonLabel"] = "AUCUNE_DECONNEXION"; }
  out["memory"] = "RAM + LittleFS"; out["bootId"] = diagnosticBootId;
  out["persistenceStatus"] = persistenceStatus; out["checkpointGeneration"] = persistedGeneration;
  out["eventsPendingSave"] = diagnosticTotal != persistedTotal;
  if (persistenceAttempted) out["checkpointAttemptAgeMs"] = uint32_t(now - lastPersistMs); else out["checkpointAttemptAgeMs"] = nullptr; out["capacity"] = diagnosticCapacity; out["retained"] = count; out["totalEvents"] = total;
  out["resetsAtBoot"] = false; out["liveCountersResetAtBoot"] = true;
  WifiDiagnosticSnapshot previous;
  if (copyLastConnected(previous)) {
    JsonObject last = out["lastConnectedSnapshot"].to<JsonObject>();
    if (diagnosticLastConnectedBoot == diagnosticBootId) last["ageMs"] = uint32_t(now - previous.sampledMs); else last["ageMs"] = nullptr;
    last["bootId"] = diagnosticLastConnectedBoot; last["uptimeMs"] = previous.sampledMs;
    last["ip"] = IPAddress(previous.ip).toString(); last["channel"] = previous.channel; last["rssi"] = previous.rssi;
    snprintf(bssid,sizeof(bssid),"%02X:%02X:%02X:%02X:%02X:%02X",previous.bssid[0],previous.bssid[1],previous.bssid[2],previous.bssid[3],previous.bssid[4],previous.bssid[5]);
    last["bssid"] = bssid;
  } else out["lastConnectedSnapshot"] = nullptr;
  if (previousBootAvailable) {
    JsonObject prev = out["previousBoot"].to<JsonObject>();
    prev["bootId"] = previousBootId; prev["ipAcquired"] = previousBootState.hasIp;
    prev["ip"] = IPAddress(previousBootState.ip).toString(); prev["rssi"] = previousBootState.rssi;
    prev["channel"] = previousBootState.channel; prev["uptimeMs"] = previousBootState.sampledMs;
    prev["disconnects"] = previousBootState.disconnects; prev["lastReason"] = previousBootState.lastReason;
  } else out["previousBoot"] = nullptr;
  JsonArray history = out["history"].to<JsonArray>();
  for (uint8_t i = 0; i < count; ++i) {
    const WifiDiagnosticEvent &e = events[i]; JsonObject item = history.add<JsonObject>();
    item["sequence"] = e.sequence; item["uptimeMs"] = e.ms; item["bootId"] = e.bootId;
    if (e.bootId == diagnosticBootId) item["ageMs"] = uint32_t(now - e.ms); else item["ageMs"] = nullptr;
    if (e.epochMs) item["epochMs"] = e.epochMs; else item["epochMs"] = nullptr;
    item["event"] = diagnosticEventName(e.event); item["eventCode"] = e.event;
    if (e.event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { item["reason"] = e.reason; item["reasonLabel"] = diagnosticReasonName(e.reason); }
    if (e.channel) item["channel"] = e.channel;
    if (e.rssiValid) item["rssi"] = e.rssi;
    if (e.event == ARDUINO_EVENT_WIFI_STA_CONNECTED || e.rssiValid) {
      snprintf(bssid,sizeof(bssid),"%02X:%02X:%02X:%02X:%02X:%02X",e.bssid[0],e.bssid[1],e.bssid[2],e.bssid[3],e.bssid[4],e.bssid[5]);
      item["bssid"] = bssid;
    }
  }
}
