#include "wifi_manager.h"

void SolarWiFiManager::begin() {
  JsonObject sys = config.system();
  const char *ssid = sys["wifi"]["ssid"] | "WIFI_SSID_A_CONFIGURER";
  const char *password = sys["wifi"]["password"] | "WIFI_PASSWORD_A_CONFIGURER";
  bool keepAp = sys["wifi"]["keepFallbackApAlwaysOn"] | true;
  state.wifiSsid = ssid;

  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
  if (keepAp) startLocalAp();
  WiFi.begin(ssid, password);
  state.networkMode = "STA_CONNECTING";
  state.addLog("WiFi connection attempt started");
  Serial.print(F("WiFi: connecting to SSID "));
  Serial.println(ssid);

  const uint32_t started = millis();
  while (millis() - started < 20000) {
    if (WiFi.status() == WL_CONNECTED) {
      state.wifiConnected = true;
      state.networkMode = keepAp ? "AP_STA" : "STATION";
      state.stationIp = WiFi.localIP().toString();
      state.apIp = keepAp ? WiFi.softAPIP().toString() : "0.0.0.0";
      state.localIp = state.stationIp;
      state.rssi = WiFi.RSSI();
      state.addLog("WiFi connected: " + state.stationIp);
      Serial.print(F("WiFi connected. STA IP: "));
      Serial.println(state.stationIp);
      if (keepAp) {
        Serial.print(F("Local AP still active. AP IP: "));
        Serial.println(state.apIp);
      }
      return;
    }
    yield();
  }
  Serial.println(F("WiFi connection failed after 20 seconds"));
  startFallbackAp();
}

void SolarWiFiManager::loop() {
  const uint32_t now = millis();
  if (now - lastStatusMs < 2000) return;
  lastStatusMs = now;
  if (state.networkMode == "STATION" || state.networkMode == "AP_STA") {
    state.wifiConnected = WiFi.status() == WL_CONNECTED;
    state.stationIp = state.wifiConnected ? WiFi.localIP().toString() : "0.0.0.0";
    state.apIp = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA ? WiFi.softAPIP().toString() : "0.0.0.0";
    state.localIp = state.wifiConnected ? state.stationIp : (state.apIp != "0.0.0.0" ? state.apIp : "0.0.0.0");
    state.rssi = state.wifiConnected ? WiFi.RSSI() : 0;
  } else if (state.networkMode == "AP_FALLBACK") {
    state.apIp = WiFi.softAPIP().toString();
    state.stationIp = "0.0.0.0";
    state.localIp = state.apIp;
    state.rssi = 0;
  }
}

bool SolarWiFiManager::testConnection(const String &ssid, const String &password, uint32_t timeoutMs) {
  String previousMode = state.networkMode;
  String previousSsid = config.system()["wifi"]["ssid"] | "WIFI_SSID_A_CONFIGURER";
  String previousPassword = config.system()["wifi"]["password"] | "WIFI_PASSWORD_A_CONFIGURER";
  bool keepAp = config.system()["wifi"]["keepFallbackApAlwaysOn"] | true;
  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
  if (keepAp) startLocalAp();
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    yield();
  }
  WiFi.disconnect();
  if (previousMode == "AP_FALLBACK") startFallbackAp();
  else {
    WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
    if (keepAp) startLocalAp();
    WiFi.begin(previousSsid.c_str(), previousPassword.c_str());
  }
  return false;
}

void SolarWiFiManager::saveCredentials(const String &ssid, const String &password) {
  JsonObject wifi = config.system()["wifi"];
  wifi["ssid"] = ssid;
  wifi["password"] = password;
  config.saveSystem();
}

void SolarWiFiManager::startFallbackAp() {
  startLocalAp();
  state.wifiConnected = false;
  state.networkMode = "AP_FALLBACK";
  state.apIp = WiFi.softAPIP().toString();
  state.stationIp = "0.0.0.0";
  state.localIp = state.apIp;
  state.addLog("Fallback AP started: " + state.apIp);
  Serial.print(F("Fallback AP IP: "));
  Serial.println(state.apIp);
}

void SolarWiFiManager::startLocalAp() {
  JsonObject ap = config.system()["fallbackAp"];
  IPAddress ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(ap["ssid"] | "AP_SSID_A_CONFIGURER", ap["password"] | "AP_PASSWORD_A_CONFIGURER");
  state.apIp = WiFi.softAPIP().toString();
  Serial.print(F("Local AP active. SSID: "));
  Serial.println(ap["ssid"] | "AP_SSID_A_CONFIGURER");
  Serial.print(F("Local AP IP: "));
  Serial.println(state.apIp);
}
