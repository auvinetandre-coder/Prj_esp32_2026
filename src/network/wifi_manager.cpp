#include "wifi_manager.h"
#include <time.h>

static const char *DEFAULT_AP_SSID = "RouteurSolaire_Config";
static const char *DEFAULT_AP_PASSWORD = "routeur1234";

static bool isWifiPlaceholder(const String &value) {
  if (value.length() == 0) return true;
  String text = value;
  text.toUpperCase();
  return text.indexOf("A_CONFIGURER") >= 0 || text.indexOf("ACONFIGURER") >= 0;
}

void SolarWiFiManager::begin() {
  JsonObject sys = config.system();
  String ssid = sys["wifi"]["ssid"] | sys["wifiSsid"] | "";
  String password = sys["wifi"]["password"] | sys["wifiPassword"] | "";
  bool keepAp = sys["wifi"]["keepFallbackApAlwaysOn"] | true;
  uint32_t connectTimeoutMs = sys["wifi"]["connectTimeoutMs"] | sys["wifiConnectTimeoutMs"] | 8000UL;
  connectTimeoutMs = constrain(connectTimeoutMs, 1000UL, 20000UL);
  state.wifiSsid = ssid;

  if (isWifiPlaceholder(ssid)) {
    Serial.println(F("WiFi maison non configure, demarrage AP local."));
    startFallbackAp();
    return;
  }

  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
  if (keepAp) startLocalAp();
  WiFi.begin(ssid.c_str(), password.c_str());
  state.networkMode = "STA_CONNECTING";
  state.addLog("WiFi connection attempt started");
  Serial.print(F("WiFi: connecting to SSID "));
  Serial.println(ssid);

  const uint32_t started = millis();
  while (millis() - started < connectTimeoutMs) {
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
  Serial.print(F("WiFi connection failed after "));
  Serial.print(connectTimeoutMs);
  Serial.println(F(" ms"));
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
  updateNtp(now);
}

bool SolarWiFiManager::testConnection(const String &ssid, const String &password, uint32_t timeoutMs) {
  String previousMode = state.networkMode;
  String previousSsid = config.system()["wifi"]["ssid"] | config.system()["wifiSsid"] | "";
  String previousPassword = config.system()["wifi"]["password"] | config.system()["wifiPassword"] | "";
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

void SolarWiFiManager::updateNtp(uint32_t now) {
  JsonObject ntp = config.system()["ntp"].as<JsonObject>();
  state.ntpEnabled = ntp.isNull() ? true : (ntp["enabled"] | true);
  if (!state.ntpEnabled) {
    state.ntpSynced = false;
    state.ntpStatus = "NTP_DISABLED";
    ntpStarted = false;
    return;
  }
  if (!state.wifiConnected || WiFi.status() != WL_CONNECTED) {
    state.ntpSynced = false;
    state.ntpStatus = "NTP_WAIT_WIFI";
    ntpStarted = false;
    return;
  }

  if (!ntpStarted) {
    const char *server1 = ntp["server1"] | "pool.ntp.org";
    const char *server2 = ntp["server2"] | "time.nist.gov";
    const char *timezone = ntp["timezone"] | "CET-1CEST,M3.5.0/2,M10.5.0/3";
    configTzTime(timezone, server1, server2);
    ntpStarted = true;
    lastNtpCheckMs = 0;
    state.ntpStatus = "NTP_SYNCING";
    Serial.println(F("NTP: synchronisation lancee."));
  }

  if (now - lastNtpCheckMs < 5000) return;
  lastNtpCheckMs = now;
  time_t currentTime = time(nullptr);
  if (currentTime > 1700000000L) {
    if (!state.ntpSynced) {
      state.logEvent("INFO", "NTP_SYNCED", "Heure NTP synchronisee", "WiFi");
    }
    state.ntpSynced = true;
    state.ntpStatus = "OK";
    state.lastNtpSyncMs = now;
  } else {
    state.ntpSynced = false;
    state.ntpStatus = "NTP_SYNCING";
  }
}

void SolarWiFiManager::startLocalAp() {
  JsonObject ap = config.system()["fallbackAp"];
  String ssid = ap["ssid"] | config.system()["fallbackApSsid"] | DEFAULT_AP_SSID;
  String password = ap["password"] | config.system()["fallbackApPassword"] | DEFAULT_AP_PASSWORD;
  if (isWifiPlaceholder(ssid)) ssid = DEFAULT_AP_SSID;
  if (isWifiPlaceholder(password) || password.length() < 8) password = DEFAULT_AP_PASSWORD;
  IPAddress ip(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(ip, gateway, subnet);
  WiFi.softAP(ssid.c_str(), password.c_str());
  state.apIp = WiFi.softAPIP().toString();
  Serial.print(F("Local AP active. SSID: "));
  Serial.println(ssid);
  Serial.print(F("Local AP IP: "));
  Serial.println(state.apIp);
}
