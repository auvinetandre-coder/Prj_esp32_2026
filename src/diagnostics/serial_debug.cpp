#include "serial_debug.h"
#include "../build_info.h"
#include "../communication/espnow_manager.h"
#include "../network/wifi_manager.h"
#include <WiFi.h>
#include <esp_err.h>
#include <esp_system.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <esp_timer.h>
#include "serial_capture.h"

bool serialDebugQuery(const RuntimeState &state, const char *command, char *out, size_t capacity) {
  if (!strcmp(command, "help")) {
    snprintf(out, capacity, "help | status | heap | uptime | version | wifi | wifi history | debug help");
  } else if (!strcmp(command, "status")) {
    snprintf(out, capacity, "WiFi=%s IP=%.15s ESP-NOW=%s MQTT=%s Safety=%.16s",
             state.wifiConnected ? "OK" : "OFF", state.stationIp.c_str(),
             state.espNowReady ? "OK" : "OFF", state.mqttConnected ? "OK" : "OFF", state.safetyLevel.c_str());
  } else if (!strcmp(command, "heap")) {
    snprintf(out, capacity, "Heap libre=%lu min=%lu blocMax=%lu octets",
             (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
  } else if (!strcmp(command, "uptime")) {
    snprintf(out, capacity, "Uptime=%llu ms", (unsigned long long)(esp_timer_get_time() / 1000));
  } else if (!strcmp(command, "version")) {
    snprintf(out, capacity, "Firmware %s", ROUTEUR_FIRMWARE_VERSION);
  } else {
    return false;
  }
  return true;
}

static const char *debugResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    default: return "OTHER";
  }
}

void SerialDebug::trace(uint8_t kind, int32_t value, uint32_t detail) {
  traces[traceHead] = {millis(), kind, value, detail, consoleEpochMs()};
  traceHead = (traceHead + 1) % traceCapacity;
  if (traceCount < traceCapacity) ++traceCount;
}

void SerialDebug::snapshotEvents() {
  snapshotCount = traceCount;
  for (uint8_t i = 0; i < snapshotCount; ++i) {
    traceSnapshot[i] = traces[(traceHead + traceCapacity - traceCount + i) % traceCapacity];
  }
}

void SerialDebug::observeModule(LoopPerfModule module, uint32_t elapsedMs) {
  if (module >= PERF_COUNT) return;
  if (elapsedMs > recentModuleMax[module]) recentModuleMax[module] = elapsedMs;
  const uint32_t now = millis();
  if (elapsedMs >= state.slowTaskThresholdMs &&
      (!lastSlowTraceMs[module] || uint32_t(now - lastSlowTraceMs[module]) >= 1000UL)) {
    lastSlowTraceMs[module] = now;
    trace(5, module, elapsedMs);
  }
}

void SerialDebug::observe(uint32_t now) {
  if (uint32_t(now - lastObserveMs) < 100UL) return;
  lastObserveMs = now;
  const int wifi = WiFi.status();
  const int channel = WiFi.channel();
  const uint32_t disconnects = state.wifiDisconnectCount;
  if (wifi != previousWifiStatus || disconnects != previousDisconnects) {
    trace(0, wifi, state.wifiLastDisconnectReason);
    previousWifiStatus = wifi;
    previousDisconnects = disconnects;
  }
  if (channel != previousChannel) { trace(1, channel, 0); previousChannel = channel; }
  if (state.mqttConnectAttempts != previousMqttAttempts) {
    trace(2, state.mqttLastConnectResult, state.mqttLastConnectMs);
    previousMqttAttempts = state.mqttConnectAttempts;
  }
  if (previousJsy != state.jsyOnline) {
    trace(3, state.jsyOnline, state.jsyTimeoutCount);
    previousJsy = state.jsyOnline;
  }
  if (previousTic != state.ticAvailable) {
    trace(4, state.ticAvailable, state.ticErrorCount);
    previousTic = state.ticAvailable;
  }
}

void SerialDebug::queue(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(output, sizeof(output) - 2, format, args);
  va_end(args);
  outputLength = strlen(output);
  output[outputLength++] = '\r';
  output[outputLength++] = '\n';
  outputOffset = 0;
}

void SerialDebug::begin() {
  enabled = false;
  queue("[DEBUG] USB/Web : help | debug help | wifi | wifi history (USB 115200, fin de ligne)");
}

bool SerialDebug::executeCommand(const char *request, char *reply, size_t capacity) {
  if (!reply || !capacity) return false;
  reply[0] = 0;
  const size_t size = request ? strnlen(request, 48) : 48;
  if (size >= 48) { snprintf(reply, capacity, "Commande trop longue (47 caracteres maximum)."); return false; }
  char normalized[48];
  memcpy(normalized, request, size + 1);
  for (size_t i = 0; i < size; ++i) {
    const unsigned char c = normalized[i];
    if (c != '\t' && (c < 32 || c > 126)) { snprintf(reply, capacity, "Commande invalide."); return false; }
    normalized[i] = tolower(c);
  }
  char *text = normalized;
  while (*text == ' ' || *text == '\t') ++text;
  size_t length = strlen(text);
  while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) text[--length] = 0;
  if (!length) return false;
  if (serialDebugQuery(state, text, reply, capacity)) return true;
  if (!strcmp(text, "debug off")) {
    enabled = false; reportLine = -1;
    snprintf(reply, capacity, "[DEBUG] OFF - bilan arrete; historique RAM et logs conserves");
  } else if (!strcmp(text, "debug on")) {
    enabled = true; reportKind = SUMMARY; reportLine = 0; lastReportMs = millis();
    snprintf(reply, capacity, "[DEBUG] ON - bilan toutes les 5 s; arret: debug off");
  } else if (!strcmp(text, "debug status") || !strcmp(text, "debug now")) {
    reportKind = SUMMARY; reportLine = 0; lastReportMs = millis();
    snprintf(reply, capacity, "[DEBUG] bilan ponctuel; periodique=%s", enabled ? "ON" : "OFF");
  } else if (!strcmp(text, "debug events")) {
    snapshotEvents(); reportKind = EVENTS; reportLine = 0;
    snprintf(reply, capacity, "[DEBUG] historique RAM: %u transitions/lenteurs", snapshotCount);
  } else if (!strcmp(text, "debug peers")) {
    reportKind = PEERS; reportLine = 0;
    snprintf(reply, capacity, "[DEBUG] pairs configures: presence, canal, interface et age RX");
  } else if (!strcmp(text, "wifi") || !strcmp(text, "debug wifi") || !strcmp(text, "wifi history") || !strcmp(text, "debug wifi history")) {
    if (!wifi) { snprintf(reply, capacity, "Diagnostic WiFi non initialise."); return false; }
    wifiEventCount = wifi->copyDiagnostics(wifiSnapshot, wifiEvents, 32, wifiTotal);
    wifiLastConnectedValid = wifi->copyLastConnected(wifiLastConnected);
    wifiStorageStatus = wifi->diagnosticStorageStatus(); wifiSavePending = wifi->diagnosticPending();
    wifiBootId = wifi->diagnosticBoot(); wifiLastConnectedBoot = wifi->lastConnectedBoot();
    reportKind = strstr(text, "history") ? WIFI_HISTORY : WIFI_STATE;
    reportLine = 0;
    snprintf(reply, capacity, "[WIFI] capture ponctuelle; %u/%lu evenements en RAM; historique sauvegarde dans LittleFS", wifiEventCount, (unsigned long)wifiTotal);
  } else if (!strcmp(text, "debug help")) {
    snprintf(reply, capacity, "USB/Web: debug on | debug off | debug status (alias debug now) | debug events | debug peers | wifi | wifi history | help");
  } else {
    snprintf(reply, capacity, "Commande inconnue. Tapez help ou debug help."); return false;
  }
  return true;
}

void SerialDebug::command() {
  input[inputLength] = 0;
  char reply[160];
  executeCommand(input, reply, sizeof(reply));
  if (reply[0]) queue("%s", reply);
}

void SerialDebug::loop() {
  // A flood or a partial command must never hold up control/safety work.
  for (uint8_t budget = 0; budget < 64 && Serial.available(); ++budget) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (droppingInput) queue("[DEBUG] commande trop longue/invalide; ignoree");
      else command();
      inputLength = 0;
      droppingInput = false;
    } else if (!droppingInput) {
      if (c == '\b' || c == 127) { if (inputLength) --inputLength; }
      else if (c == '\t' || (c >= 32 && c <= 126)) {
        if (inputLength < sizeof(input) - 1) input[inputLength++] = c;
        else droppingInput = true; // Discard the entire line, never its suffix.
      } else droppingInput = true;
    }
  }
  const uint32_t now = millis();
  observe(now);
  if (outputOffset == outputLength) {
    if (reportLine < 0 && enabled && uint32_t(now - lastReportMs) >= 5000UL) {
      reportKind = SUMMARY;
      reportLine = 0;
      lastReportMs = now;
    }
    if (reportLine >= 0) report(now);
  }
  // One complete line at most per loop, only when the UART has room.
  // TX buffer is 512 bytes; lines are <=192. Avoid splitting around existing logs.
  const int available = Serial.availableForWrite();
  const uint16_t bytes = outputLength - outputOffset;
  if (available < bytes) return;
  if (bytes) outputOffset += Serial.write(reinterpret_cast<const uint8_t *>(output + outputOffset), bytes);
}

void SerialDebug::report(uint32_t now) {
  // Values are read progressively from RuntimeState, not an atomic snapshot.
  const int16_t line = reportLine++;
  if (reportKind == WIFI_STATE || reportKind == WIFI_HISTORY) {
    if (reportKind == WIFI_HISTORY) {
      if (line >= wifiEventCount) {
        queue("[WIFI HISTORY END] %u gardes sur %lu; RAM + sauvegarde LittleFS, ordre chronologique", wifiEventCount, (unsigned long)wifiTotal);
        reportLine = -1; return;
      }
      const WifiDiagnosticEvent &entry = wifiEvents[line];
      queue("[WIFI #%lu] boot=%lu boot_ms=%lu %s reason=%u %s canal=%u RSSI_valide=%u RSSI=%d", (unsigned long)entry.sequence,
            (unsigned long)entry.bootId, (unsigned long)entry.ms, SolarWiFiManager::diagnosticEventName(entry.event),
            entry.reason, SolarWiFiManager::diagnosticReasonName(entry.reason), entry.channel, entry.rssiValid, entry.rssi);
      return;
    }
    const WifiDiagnosticSnapshot &d = wifiSnapshot;
    switch (line) {
      case 0: queue("[WIFI] capture_age=%lu ms status=%u mode=%u associe=%u IP_obtenue=%u canal=%u AP=%u clients_AP=%u", (unsigned long)uint32_t(now-d.sampledMs), d.status,d.mode,d.associated,d.hasIp,d.channel,d.apActive,d.apClients); break;
      case 1: queue("[WIFI] RSSI_valide=%u plage_valide=%u actuel=%d min=%d max=%d dBm BSSID=%02X:%02X:%02X:%02X:%02X:%02X",d.rssiValid,d.hasRssiRange,d.rssi,d.rssiMin,d.rssiMax,d.bssid[0],d.bssid[1],d.bssid[2],d.bssid[3],d.bssid[4],d.bssid[5]); break;
      case 2: queue("[WIFI] IP=%s passerelle=%s masque=%s",IPAddress(d.ip).toString().c_str(),IPAddress(d.gateway).toString().c_str(),IPAddress(d.subnet).toString().c_str()); break;
      case 3: queue("[WIFI] DNS1=%s DNS2=%s IP_obtenues=%lu deconnexions=%lu pertes_IP=%lu",IPAddress(d.dns1).toString().c_str(),IPAddress(d.dns2).toString().c_str(),(unsigned long)d.connections,(unsigned long)d.disconnects,(unsigned long)d.lostIp); break;
      case 4: queue("[WIFI] derniere_deconnexion=%u reason=%u %s age=%lu ms",d.hasDisconnect,d.lastReason,SolarWiFiManager::diagnosticReasonName(d.lastReason),(unsigned long)(d.hasDisconnect ? uint32_t(now-d.disconnectedMs) : 0)); break;
      case 5: queue("[WIFI] IP_valide=%u duree_connexion=%lu ms coupure_active=%u duree=%lu ms derniere_coupure=%lu ms",d.hasIp,(unsigned long)(d.hasIp ? uint32_t(now-d.gotIpMs) : 0),d.outageActive,(unsigned long)(d.outageActive ? uint32_t(now-d.downSinceMs) : 0),(unsigned long)d.lastOutageMs); break;
      case 6:
        if (wifiLastConnectedValid) queue("[WIFI DERNIERE PHOTO CONNECTEE] boot=%lu boot_ms=%lu IP=%s canal=%u RSSI=%d dBm",(unsigned long)wifiLastConnectedBoot,(unsigned long)wifiLastConnected.sampledMs,IPAddress(wifiLastConnected.ip).toString().c_str(),wifiLastConnected.channel,wifiLastConnected.rssi);
        else queue("[WIFI] aucune photo connectee observee depuis le boot");
        break;
      case 7: queue("[WIFI SAUVEGARDE] %s nouveaux_evenements_en_attente=%u ; derniere copie valide conservee en cas d'erreur",wifiStorageStatus,wifiSavePending); break;
      default: queue("[WIFI END] min/max depuis boot; historique: wifi history; sauvegardes LittleFS limitees en arriere-plan"); reportLine = -1;
    }
    return;
  }
  if (reportKind == EVENTS) {
    if (line >= snapshotCount) {
      queue("[DBG %lu][EVENTS END] historique non persistant; transitions observees toutes les 100 ms", (unsigned long)now);
      reportLine = -1;
      return;
    }
    const Trace &item = traceSnapshot[line];
    char eventTime[28]{};
    if (item.epochMs) {
      const time_t seconds = item.epochMs / 1000;
      tm utc{};
      if (gmtime_r(&seconds, &utc)) strftime(eventTime, sizeof(eventTime), "[%Y-%m-%dT%H:%M:%SZ]", &utc);
    }
    if (item.kind == 5 && item.value >= 0 && item.value < PERF_COUNT) {
      queue("[TRACE %lu]%s[SLOW] %.32s elapsed=%lu ms", (unsigned long)item.ms, eventTime,
            state.loopPerf[item.value].name, (unsigned long)item.detail);
    } else {
      static const char *labels[] = {"WIFI status/reason", "WIFI channel", "MQTT result/duration_ms", "JSY online/timeouts", "TIC available/errors"};
      queue("[TRACE %lu]%s[%s] value=%ld detail=%lu", (unsigned long)item.ms, eventTime,
            item.kind < 5 ? labels[item.kind] : "UNKNOWN", (long)item.value, (unsigned long)item.detail);
    }
    return;
  }
  if (reportKind == PEERS) {
    EspNowPeerDiagnostic peer{};
    if (line < 20 && radio && radio->peerDiagnostic(line, peer)) {
      queue("[DBG %lu][PEER] %s registered=%u channel=%u iface=%u seen=%u age=%lu ms", (unsigned long)now,
            peer.mac, peer.registered, peer.channel, peer.interfaceId, peer.seen, (unsigned long)peer.ageMs);
    } else {
      queue("[DBG %lu][PEERS END] max=20; channel=0 suit le canal courant; iface=0 STA,1 AP", (unsigned long)now);
      reportLine = -1;
    }
    return;
  }
  switch (line) {
    case 0:
      for (uint8_t i = 0; i < PERF_COUNT; ++i) {
        reportModuleMax[i] = recentModuleMax[i];
        recentModuleMax[i] = 0;
      }
      queue("[DBG %lu][SYS] fw=%s uptime_s=%lu reset=%s", (unsigned long)now,
            ROUTEUR_FIRMWARE_VERSION, (unsigned long)(now / 1000), debugResetReason()); break;
    case 1:
      queue("[DBG %lu][MEM] free=%lu min=%lu largest=%lu bytes", (unsigned long)now,
            (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getMaxAllocHeap()); break;
    case 2:
      queue("[DBG %lu][LOOP] avg=%lu max=%lu ms count=%lu", (unsigned long)now,
            (unsigned long)state.loopDurationMs, (unsigned long)state.maxLoopDurationMs, (unsigned long)state.loopCount); break;
    case 3:
      queue("[DBG %lu][WIFI] status=%d connected=%u mode=%d channel=%d rssi=%ld", (unsigned long)now,
            (int)WiFi.status(), WiFi.status() == WL_CONNECTED, (int)WiFi.getMode(), WiFi.channel(),
            (long)(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0)); break;
    case 4:
      queue("[DBG %lu][WIFI] STA=%.15s AP=%.15s disconnects=%lu reason=%lu age=%lu ms", (unsigned long)now,
            state.stationIp.c_str(), state.apIp.c_str(), (unsigned long)state.wifiDisconnectCount,
            (unsigned long)state.wifiLastDisconnectReason, (unsigned long)(now - state.wifiLastDisconnectMs)); break;
    case 5:
      queue("[DBG %lu][MQTT] enabled=%u connected=%u status=%.32s attempts=%lu failures=%lu", (unsigned long)now,
            state.mqttEnabled, state.mqttConnected, state.mqttStatus.c_str(),
            (unsigned long)state.mqttConnectAttempts, (unsigned long)state.mqttConnectFailures); break;
    case 6:
      queue("[DBG %lu][MQTT] last_connect=%lu max_connect=%lu ms result=%ld", (unsigned long)now,
            (unsigned long)state.mqttLastConnectMs, (unsigned long)state.mqttMaxConnectMs, (long)state.mqttLastConnectResult); break;
    case 7:
      queue("[DBG %lu][ESPNOW] ready=%u raw_rx=%lu tx_callback_ok=%lu failed=%lu", (unsigned long)now,
            state.espNowReady, (unsigned long)state.espNowRawRxCount, (unsigned long)state.espNowTxSuccessCount,
            (unsigned long)state.espNowTxFailureCount); break;
    case 8:
      queue("[DBG %lu][ESPNOW] discovery_attempts=%lu failures=%lu last=%ld %s", (unsigned long)now,
            (unsigned long)state.espNowDiscoveryAttempts, (unsigned long)state.espNowDiscoveryFailures,
            (long)state.espNowDiscoveryResult, state.espNowDiscoveryAttempts ? esp_err_to_name(state.espNowDiscoveryResult) : "NOT_ATTEMPTED"); break;
    case 9:
      queue("[DBG %lu][JSY] online=%u lastValid=%u age=%lu ms baud=%lu timeout=%u crc=%u", (unsigned long)now,
            state.jsyOnline, state.lastJsyReadMs != 0, (unsigned long)(now - state.lastJsyReadMs),
            (unsigned long)state.jsyBaudrate, state.jsyTimeoutCount, state.jsyCrcErrorCount); break;
    case 10:
      queue("[DBG %lu][JSY] P1=%.1f P2=%.1f W error=%.48s", (unsigned long)now,
            state.activePowerW1, state.activePowerW2, state.jsyLastError.c_str()); break;
    case 11:
      queue("[DBG %lu][TIC] available=%u status=%.32s lastValid=%u age=%lu ms errors=%u", (unsigned long)now,
            state.ticAvailable, state.ticStatus.c_str(), state.lastTicReadMs != 0,
            (unsigned long)(now - state.lastTicReadMs), state.ticErrorCount); break;
    case 12:
      queue("[DBG %lu][GRID] source=%.24s power=%.1f W remoteLastValid=%u remoteAge=%lu ms", (unsigned long)now,
            state.gridPowerSource.c_str(), state.gridPowerW, state.lastEspNowSensorReceiveMs != 0,
            (unsigned long)(now - state.lastEspNowSensorReceiveMs)); break;
    case 13:
      queue("[DBG %lu][SAFETY] level=%.16s tripped=%u FS=%u automate=%.24s", (unsigned long)now,
            state.safetyLevel.c_str(), state.safetyTripped, state.littleFsOk, state.automateState.c_str()); break;
    case 14:
      queue("[DBG %lu][WEB] requests=%lu stateBuild=%lu ms json=%lu bytes", (unsigned long)now,
            (unsigned long)state.webRequestCount, (unsigned long)state.apiStateBuildMs, (unsigned long)state.apiStateJsonBytes); break;
    default:
      if (line < 18) {
        const uint8_t i = line - 15;
        queue("[DBG %lu][DS18B20 %u] available=%u temp=%.2f C lastValid=%u age=%lu ms errors=%u", (unsigned long)now,
              i + 1, state.ds18b20Available[i], state.ds18b20Temps[i], state.ds18b20LastReadMs[i] != 0,
              (unsigned long)(now - state.ds18b20LastReadMs[i]), state.ds18b20ErrorCount[i]);
      } else if (line < 18 + PERF_COUNT) {
        const LoopPerfStat &perf = state.loopPerf[line - 18];
        if (line - 18 == PERF_PID) {
          // PID is measured internally, outside recordLoopModule's observer.
          queue("[DBG %lu][PERF] %.32s avg=%lu maxBoot=%lu ms recentMax=N/A calls=%lu", (unsigned long)now,
                perf.name, (unsigned long)perf.avgMs, (unsigned long)perf.maxMs, (unsigned long)perf.count);
        } else {
          queue("[DBG %lu][PERF] %.32s avg=%lu maxBoot=%lu recentMax=%lu ms calls=%lu", (unsigned long)now,
                perf.name, (unsigned long)perf.avgMs, (unsigned long)perf.maxMs,
                (unsigned long)reportModuleMax[line - 18], (unsigned long)perf.count);
        }
      } else if (line == 18 + PERF_COUNT) {
        queue("[DBG %lu][WATCHDOG] WiFi_expected=%u peers=%u wifiDown=%lu radioSilent=%lu ms; AND >=300000", (unsigned long)now,
              state.connectivityWifiExpected, state.connectivityPeerCount, (unsigned long)state.connectivityWifiDownMs,
              (unsigned long)state.connectivityRadioSilentMs);
      } else {
        queue("[DBG %lu][END] debug off pour arreter", (unsigned long)now);
        reportLine = -1;
      }
  }
}
