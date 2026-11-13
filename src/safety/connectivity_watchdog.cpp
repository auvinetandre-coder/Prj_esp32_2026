#include "connectivity_watchdog.h"
#include "connectivity_timeout.h"
#include <esp_system.h>

// Retain the cause over this software restart without writing NVS/config.
static RTC_DATA_ATTR uint32_t connectivityRestartMarker = 0;
static constexpr uint32_t restartMarker = 0x57444635;

void ConnectivityWatchdog::begin() {
  if (connectivityRestartMarker == restartMarker && esp_reset_reason() == ESP_RST_SW) {
    state.logEvent("WARNING", "CONNECTIVITY_WATCHDOG_BOOT", "Redemarrage apres 5 min sans WiFi ET sans pairs ESP-NOW", "Watchdog");
  }
  connectivityRestartMarker = 0;
  startedMs = millis();
  lastCheckMs = startedMs;
  state.addLog("Watchdog connectivite: WiFi ET tous les pairs ESP-NOW absents pendant 300 s");
}

void ConnectivityWatchdog::loop() {
  uint32_t now = millis();
  if (uint32_t(now - lastCheckMs) < 1000UL) return;
  lastCheckMs = now;
  String ssid = config.system()["wifi"]["ssid"] | "";
  if (!ssid.length()) ssid = config.system()["wifiSsid"] | "";
  ssid.toUpperCase();
  const bool wifiExpected = ssid.length() && ssid.indexOf("A_CONFIGURER") < 0 && ssid.indexOf("ACONFIGURER") < 0;

  // A new peer set gets a full grace period; no reboot for an unconfigured module.
  uint32_t signature = 2166136261UL;
  uint16_t peerCount = 0;
  uint32_t radioAge = radio.configuredPeerSilenceMs(peerCount, signature);
  now = millis(); // RX callbacks may have run after the first clock sample.
  if (signature != lastPeerSignature) {
    lastPeerSignature = signature;
    startedMs = now;
    wifiDown = false;
  }
  if (radioAge > uint32_t(now - startedMs)) radioAge = now - startedMs;
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp || !wifiExpected) wifiDown = false;
  else if (!wifiDown) { wifiDown = true; wifiDownSinceMs = now; }

  state.connectivityWifiExpected = wifiExpected;
  state.connectivityPeerCount = peerCount;
  state.connectivityWifiDownMs = wifiDown ? now - wifiDownSinceMs : 0;
  state.connectivityRadioSilentMs = peerCount ? radioAge : 0;
  if (!connectivityWatchdogExpired(wifiExpected, wifiUp, peerCount != 0,
                                   state.connectivityWifiDownMs, radioAge)) return;

  state.safetyTripped = true;
  state.safetyLevel = "CRITICAL";
  state.safetyReason = "CONNECTIVITY_WATCHDOG";
  // Cut first, before logging/restarting. No more application work follows.
  actuators.lockAllForSafety(state.safetyReason);
  actuators.forceAllOff();
  connectivityRestartMarker = restartMarker;
  state.logEvent("CRITICAL", "CONNECTIVITY_WATCHDOG_RESTART", "WiFi ET tous les pairs ESP-NOW perdus depuis 300 s; sorties OFF; reboot", "Watchdog");
  ESP.restart();
}
