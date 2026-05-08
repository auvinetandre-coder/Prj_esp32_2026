#include "web_ui.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_system.h>

static const char *wifiQualityLabel(int rssi) {
  if (rssi == 0) return "N/A";
  if (rssi >= -55) return "Excellent";
  if (rssi >= -67) return "Bon";
  if (rssi >= -75) return "Moyen";
  return "Faible";
}

static const char *resetReasonLabel(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "Power on";
    case ESP_RST_EXT: return "Reset externe";
    case ESP_RST_SW: return "Reset logiciel";
    case ESP_RST_PANIC: return "Panic / crash";
    case ESP_RST_INT_WDT: return "Watchdog interruption";
    case ESP_RST_TASK_WDT: return "Watchdog tache";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_DEEPSLEEP: return "Sortie deep sleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO";
    default: return "N/A";
  }
}

void WebUi::begin() {
  routes();
  server.begin();
  state.addLog("Web UI started");
}

void WebUi::loop() {
  server.handleClient();
}

void WebUi::routes() {
  server.on("/", HTTP_GET, [this]() { server.send(200, "text/html; charset=utf-8", homePage()); });
  server.on("/lite", HTTP_GET, [this]() { server.send(200, "text/html; charset=utf-8", litePage()); });
  server.on("/app", HTTP_GET, [this]() {
    if (!streamLittleFsFile("/www/index.html", "text/html; charset=utf-8")) server.send(200, "text/html; charset=utf-8", litePage());
  });
  server.on("/www/app.js", HTTP_GET, [this]() {
    if (!streamLittleFsFile("/www/app.js", "application/javascript; charset=utf-8")) server.send(404, "text/plain", "app.js absent");
  });
  server.on("/www/style.css", HTTP_GET, [this]() {
    if (!streamLittleFsFile("/www/style.css", "text/css; charset=utf-8")) {
      server.send(200, "text/css; charset=utf-8", fallbackStyleCss());
    }
  });
  server.on("/full", HTTP_GET, [this]() {
    server.send(200, "text/html; charset=utf-8", F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Interface complete</title><style>body{background:#111827;color:#F9FAFB;font-family:system-ui;padding:18px}a{color:#93C5FD}</style></head><body><h1>Interface complete desactivee</h1><p>La page complete embarquee etait trop lourde pour rester fiable. Utilise <a href=\"/lite\">/lite</a> ou televerse la WebUI LittleFS puis ouvre <a href=\"/app\">/app</a>.</p></body></html>"));
  });
  server.on("/api/ping", HTTP_GET, [this]() {
    String out = "{\"ok\":true,\"networkMode\":\"" + state.networkMode + "\",\"localIp\":\"" + state.localIp + "\"}";
    server.send(200, "application/json", out);
  });
  server.on("/api/fs", HTTP_GET, [this]() { sendFsListJson(); });
  server.on("/fs", HTTP_GET, [this]() { server.send(200, "text/html; charset=utf-8", fsPage()); });
  server.on("/api/status", HTTP_GET, [this]() {
    DynamicJsonDocument doc(6144);
    JsonObject out = doc.to<JsonObject>();
    state.toJson(out, false);
    JsonObject device = config.device();
    JsonObject system = config.system();
    out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["role"] = device["role"] | RuntimeState::roleToString(state.role);
    out["firmwareVersion"] = device["firmwareVersion"] | "unknown";
    out["simulationMode"] = state.simulationMode;
    out["simulationType"] = system["simulation"]["mode"] | state.simulationType.c_str();
    out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
    sendJson(doc);
  });
  server.on("/api/status-lite", HTTP_GET, [this]() { sendStatusLite(); });
  server.on("/api/diagnostic", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonObject out = doc.to<JsonObject>();
    state.toJson(out, true);
    JsonObject device = config.device();
    JsonObject system = config.system();
    out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["role"] = device["role"] | RuntimeState::roleToString(state.role);
    out["firmwareVersion"] = device["firmwareVersion"] | "unknown";
    out["simulationMode"] = state.simulationMode;
    out["simulationType"] = system["simulation"]["mode"] | state.simulationType.c_str();
    out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
    sendJson(doc);
  });
  server.on("/api/system-info", HTTP_GET, [this]() { sendSystemInfo(); });
  server.on("/api/device", HTTP_GET, [this]() { sendConfig("device"); });
  server.on("/api/system", HTTP_GET, [this]() { sendConfig("system"); });
  server.on("/api/sensors", HTTP_GET, [this]() { sendConfig("sensors"); });
  server.on("/api/actuators", HTTP_GET, [this]() { sendConfig("actuators"); });
  server.on("/api/rules", HTTP_GET, [this]() { sendConfig("rules"); });
  server.on("/api/config", HTTP_GET, [this]() {
    DynamicJsonDocument doc(24576);
    doc["device"].set(config.deviceDoc().as<JsonObject>());
    doc["system"].set(config.systemDoc().as<JsonObject>());
    doc["sensors"].set(config.sensorsDoc().as<JsonObject>());
    doc["actuators"].set(config.actuatorsDoc().as<JsonObject>());
    doc["rules"].set(config.rulesDoc().as<JsonObject>());
    sendJson(doc);
  });
  server.on("/README.md", HTTP_GET, [this]() {
    if (LittleFS.exists("/README.md")) {
      File file = LittleFS.open("/README.md", "r");
      server.streamFile(file, "text/markdown");
      file.close();
      return;
    }
    server.send(200, "text/markdown", F("# RouteurSolaireESP32\n\nREADME embarque non trouve dans LittleFS.\n\nPour l'afficher completement depuis l'ESP32, placer README.md dans le dossier data puis televerser les donnees LittleFS.\n\nSections principales : presentation, roles MASTER/BACKUP/NODE, capteurs, actionneurs, interface Web, securites, simulation, cablage, installation Arduino IDE, diagnostic et avertissement 230 V.\n"));
  });
  server.on("/api/device", HTTP_POST, [this]() { saveConfig("device", "/config/device.json"); });
  server.on("/api/system", HTTP_POST, [this]() { saveConfig("system", "/config/system.json"); });
  server.on("/api/sensors", HTTP_POST, [this]() { saveConfig("sensors", "/config/sensors.json"); });
  server.on("/api/actuators", HTTP_POST, [this]() { saveConfig("actuators", "/config/actuators.json"); });
  server.on("/api/rules", HTTP_POST, [this]() { saveConfig("rules", "/config/rules.json"); });
  server.on("/api/config/device", HTTP_POST, [this]() { saveConfig("device", "/config/device.json"); });
  server.on("/api/config/system", HTTP_POST, [this]() { saveConfig("system", "/config/system.json"); });
  server.on("/api/config/sensors", HTTP_POST, [this]() { saveConfig("sensors", "/config/sensors.json"); });
  server.on("/api/config/actuators", HTTP_POST, [this]() { saveConfig("actuators", "/config/actuators.json"); });
  server.on("/api/config/rules", HTTP_POST, [this]() { saveConfig("rules", "/config/rules.json"); });
  server.on("/api/actuator/command", HTTP_POST, [this]() {
    if (state.safetyTripped) {
      server.send(423, "application/json", "{\"ok\":false,\"error\":\"Safety CRITICAL\"}");
      return;
    }
    String id = server.arg("id");
    String command = server.arg("command");
    float value = server.arg("value").toFloat();
    bool ok = actuators.command(id, command.length() ? command : "setPower", value);
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/safety/manual-stop", HTTP_POST, [this]() {
    safety.triggerManualStop();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/logs/export", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonArray events = doc["events"].to<JsonArray>();
    state.eventsToJson(events);
    sendJson(doc);
  });
  server.on("/api/logs/clear", HTTP_POST, [this]() {
    state.clearEvents();
    state.logEvent("INFO", "CONFIG_CHANGED", "Logs effaces depuis l interface Web", "WebUi");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation", HTTP_GET, [this]() {
    DynamicJsonDocument doc(4096);
    simulation.toJson(doc.to<JsonObject>());
    sendJson(doc);
  });
  server.on("/api/simulation/start", HTTP_POST, [this]() {
    simulation.enable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/stop", HTTP_POST, [this]() {
    actuators.forceAllOff();
    simulation.disable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/enable", HTTP_POST, [this]() {
    simulation.enable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/disable", HTTP_POST, [this]() {
    actuators.forceAllOff();
    simulation.disable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/mode", HTTP_POST, [this]() {
    simulation.setMode(server.arg("mode").length() ? server.arg("mode") : server.arg("plain"));
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/scenario", HTTP_POST, [this]() {
    simulation.setScenario(server.arg("scenario").length() ? server.arg("scenario") : server.arg("plain"));
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/randomize", HTTP_POST, [this]() {
    simulation.enable();
    simulation.setMode("random");
    simulation.randomize();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/values", HTTP_POST, [this]() {
    String error;
    bool ok = simulation.setValuesFromJson(server.arg("plain"), error);
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : String("{\"ok\":false,\"error\":\"") + error + "\"}");
  });
  server.on("/api/simulation/set-values", HTTP_POST, [this]() {
    DynamicJsonDocument doc(1536);
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON invalide\"}");
      return;
    }
    DynamicJsonDocument normalized(2048);
    JsonObject root = normalized.to<JsonObject>();
    JsonObject jsy = root["jsy"].to<JsonObject>();
    jsy["available"] = doc["jsyAvailable"] | true;
    jsy["gridPowerW"] = doc["gridPowerW"] | state.gridPowerW;
    jsy["voltageV"] = doc["voltageV"] | state.gridVoltageV;
    jsy["currentA"] = doc["currentA"] | state.gridCurrentA;
    jsy["powerFactor"] = doc["powerFactor"] | state.gridPowerFactor;
    jsy["frequencyHz"] = doc["frequencyHz"] | state.gridFrequencyHz;
    JsonObject tic = root["tic"].to<JsonObject>();
    tic["available"] = doc["ticAvailable"] | true;
    tic["apparentPowerVA"] = doc["apparentPowerVA"] | state.ticApparentPowerVA;
    tic["currentA"] = doc["ticCurrentA"] | state.ticCurrentA;
    tic["tariff"] = "BASE";
    JsonArray ds = root["ds18b20"].to<JsonArray>();
    const char *ids[] = {"sonde1", "sonde2", "sonde3"};
    const char *keys[] = {"temperatureTop", "temperatureMiddle", "temperatureBottom"};
    for (uint8_t i = 0; i < 3; i++) {
      JsonObject item = ds.add<JsonObject>();
      item["id"] = ids[i];
      item["available"] = true;
      item["temperatureC"] = doc[keys[i]] | state.ds18b20Temps[i];
    }
    String body;
    serializeJson(normalized, body);
    String error;
    bool ok = simulation.setValuesFromJson(body, error);
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : String("{\"ok\":false,\"error\":\"") + error + "\"}");
  });
  server.on("/api/rules/validate", HTTP_GET, [this]() { server.send(200, "application/json", rules.validateRulesJson()); });
  server.on("/api/ds18b20", HTTP_GET, [this]() { server.send(200, "application/json", sensors.detectedDs18b20Json()); });
  server.on("/api/ds18b20/status", HTTP_GET, [this]() { server.send(200, "application/json", sensors.ds18b20StatusJson()); });
  server.on("/api/ds18b20/assign", HTTP_POST, [this]() {
    String sensorId = server.arg("sensorId");
    String address = server.arg("address");
    String plain = server.arg("plain");

    // Certains navigateurs/environnements ESP32 ne remplissent pas toujours
    // server.arg() comme attendu. On accepte donc aussi JSON et corps formulaire.
    if ((sensorId.length() == 0 || address.length() == 0) && plain.length()) {
      DynamicJsonDocument doc(256);
      if (!deserializeJson(doc, plain)) {
        sensorId = doc["sensorId"] | sensorId;
        address = doc["address"] | address;
      } else {
        int sPos = plain.indexOf("sensorId=");
        int aPos = plain.indexOf("address=");
        if (sPos >= 0) {
          int end = plain.indexOf('&', sPos);
          sensorId = plain.substring(sPos + 9, end >= 0 ? end : plain.length());
        }
        if (aPos >= 0) {
          int end = plain.indexOf('&', aPos);
          address = plain.substring(aPos + 8, end >= 0 ? end : plain.length());
        }
      }
    }

    sensorId.trim();
    address.trim();
    if (!sensorId.length() || !address.length()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"sensorId ou address manquant\"}");
      return;
    }
    bool ok = sensors.assignDs18b20(sensorId, address);
    if (ok) {
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      String error = config.lastError();
      if (!error.length()) error = "sonde inconnue, adresse invalide ou sauvegarde LittleFS impossible";
      DynamicJsonDocument out(384);
      out["ok"] = false;
      out["error"] = error;
      sendJson(out);
    }
  });
  server.on("/api/wifi/test", HTTP_POST, [this]() {
    bool ok = wifi.testConnection(server.arg("ssid"), server.arg("password"));
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow/peer", HTTP_POST, [this]() {
    bool ok = espnow.addPeer(server.arg("mac"));
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/restart", HTTP_POST, [this]() {
    server.send(200, "application/json", "{\"ok\":true}");
    wifi.restart();
  });
  server.on("/api/system/reboot", HTTP_POST, [this]() {
    server.send(200, "application/json", "{\"ok\":true}");
    wifi.restart();
  });
}

void WebUi::sendJson(DynamicJsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void WebUi::sendStatusLite() {
  DynamicJsonDocument doc(4096);
  JsonObject out = doc.to<JsonObject>();
  auto setNumber = [&out](const char *key, float value) {
    if (isnan(value) || isinf(value)) out[key] = nullptr;
    else out[key] = value;
  };

  out["ok"] = true;
  JsonObject device = config.device();
  JsonObject system = config.system();
  out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
  out["deviceId"] = state.deviceId;
  out["role"] = device["role"] | RuntimeState::roleToString(state.role);
  out["firmwareVersion"] = device["firmwareVersion"] | "unknown";
  out["arduinoCore"] = String(ESP_ARDUINO_VERSION_MAJOR) + "." + String(ESP_ARDUINO_VERSION_MINOR) + "." + String(ESP_ARDUINO_VERSION_PATCH);
  out["idfVersion"] = ESP.getSdkVersion();
  out["chipModel"] = ESP.getChipModel();
  out["chipRevision"] = ESP.getChipRevision();
  out["cpuMhz"] = ESP.getCpuFreqMHz();
  out["flashBytes"] = ESP.getFlashChipSize();
  out["littleFsTotal"] = LittleFS.totalBytes();
  out["littleFsUsed"] = LittleFS.usedBytes();
  out["networkMode"] = state.networkMode;
  out["localIp"] = state.localIp;
  out["stationIp"] = state.stationIp;
  out["apIp"] = state.apIp;
  out["wifiConnected"] = state.wifiConnected;
  out["wifiSsid"] = state.wifiSsid;
  out["rssi"] = state.rssi;
  out["safetyTripped"] = state.safetyTripped;
  out["safetyLevel"] = state.safetyLevel;
  out["safetyReason"] = state.safetyReason;
  out["simulationMode"] = state.simulationMode;
  out["simulationType"] = system["simulation"]["mode"] | state.simulationType.c_str();
  out["simulationRemainingMs"] = state.simulationRemainingMs;
  out["jsyOnline"] = state.jsyOnline;
  out["ticAvailable"] = state.ticAvailable;
  out["ds18b20CriticalMissing"] = state.ds18b20CriticalMissing;
  JsonArray dsAvailable = out["ds18b20Available"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; i++) dsAvailable.add(state.ds18b20Available[i]);
  JsonArray ds = out["ds18b20"].to<JsonArray>();
  uint8_t dsIndex = 0;
  for (JsonObject cfg : config.sensorsDoc()["ds18b20"].as<JsonArray>()) {
    if (dsIndex >= 3) break;
    JsonObject item = ds.add<JsonObject>();
    String fallbackId = String("sonde") + String(dsIndex + 1);
    String id = cfg["id"] | fallbackId.c_str();
    item["id"] = id;
    item["name"] = cfg["name"] | id.c_str();
    item["role"] = cfg["role"] | "autre";
    item["enabled"] = cfg["enabled"] | true;
    item["critical"] = cfg["critical"] | false;
    item["available"] = state.ds18b20Available[dsIndex];
    if (isnan(state.ds18b20Temps[dsIndex]) || isinf(state.ds18b20Temps[dsIndex])) item["temperatureC"] = nullptr;
    else item["temperatureC"] = state.ds18b20Temps[dsIndex];
    item["lastReadMs"] = state.ds18b20LastReadMs[dsIndex];
    item["errorCount"] = state.ds18b20ErrorCount[dsIndex];
    dsIndex++;
  }
  setNumber("gridPowerW", state.gridPowerW);
  out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
  setNumber("jsyGridPowerW", state.jsyGridPowerW);
  setNumber("ticGridPowerW", state.ticGridPowerW);
  setNumber("activePowerW1", state.activePowerW1);
  setNumber("activePowerW2", state.activePowerW2);
  setNumber("currentA1", state.currentA1);
  setNumber("currentA2", state.currentA2);
  setNumber("injectionW", state.injectionW);
  setNumber("consumptionW", state.consumptionW);
  setNumber("surplusW", state.surplusW);
  setNumber("tankTopC", state.tankTopC);
  setNumber("tankMiddleC", state.tankMiddleC);
  setNumber("tankBottomC", state.tankBottomC);
  setNumber("ssr1PowerPct", state.ssr1PowerPct);
  setNumber("ssr2PowerPct", state.ssr2PowerPct);
  setNumber("robotDynPowerPct", state.robotDynPowerPct);
  out["heapFree"] = ESP.getFreeHeap();
  sendJson(doc);
}

void WebUi::sendSystemInfo() {
  DynamicJsonDocument doc(8192);
  JsonObject out = doc.to<JsonObject>();
  auto setNumber = [&out](const char *key, float value) {
    if (isnan(value) || isinf(value)) out[key] = nullptr;
    else out[key] = value;
  };

  JsonObject device = config.device();
  JsonObject system = config.system();
  JsonObject router = system["router"].as<JsonObject>();
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const int rssi = wifiOk ? WiFi.RSSI() : 0;
  const float outputPercent = max(max(state.ssr1PowerPct, state.ssr2PowerPct), state.robotDynPowerPct);
  unsigned long lastMeasureMs = 0;
  if (state.lastJsyReadMs > lastMeasureMs) lastMeasureMs = state.lastJsyReadMs;
  if (state.lastTicReadMs > lastMeasureMs) lastMeasureMs = state.lastTicReadMs;
  for (uint8_t i = 0; i < 3; i++) {
    if (state.ds18b20LastReadMs[i] > lastMeasureMs) lastMeasureMs = state.ds18b20LastReadMs[i];
  }
  const unsigned long lastMeasureAge = lastMeasureMs ? (millis() - lastMeasureMs) / 1000UL : 4294967295UL;
  float temperature = NAN;
  for (uint8_t i = 0; i < 3; i++) {
    if (state.ds18b20Available[i] && !isnan(state.ds18b20Temps[i]) && !isinf(state.ds18b20Temps[i])) {
      temperature = state.ds18b20Temps[i];
      break;
    }
  }

  out["deviceName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
  out["firmwareVersion"] = device["firmwareVersion"] | "N/A";
  out["buildDate"] = String(__DATE__) + " " + String(__TIME__);
  out["uptime"] = millis();
  out["uptimeSeconds"] = millis() / 1000UL;
  out["ip"] = wifiOk ? WiFi.localIP().toString() : (state.localIp.length() ? state.localIp : "N/A");
  out["mac"] = WiFi.macAddress();
  out["ssid"] = wifiOk ? WiFi.SSID() : (state.wifiSsid.length() ? state.wifiSsid : "N/A");
  if (wifiOk) out["rssi"] = rssi;
  else out["rssi"] = nullptr;
  out["wifiQuality"] = wifiQualityLabel(rssi);
  out["freeHeap"] = ESP.getFreeHeap();
  out["minFreeHeap"] = ESP.getMinFreeHeap();
  out["cpuFreqMHz"] = ESP.getCpuFreqMHz();
  out["resetReason"] = resetReasonLabel(esp_reset_reason());
  out["networkMode"] = state.networkMode.length() ? state.networkMode : "N/A";
  out["stationIp"] = state.stationIp.length() ? state.stationIp : "N/A";
  out["apIp"] = state.apIp.length() ? state.apIp : "N/A";
  out["role"] = RuntimeState::roleToString(state.role);
  out["safetyLevel"] = state.safetyLevel.length() ? state.safetyLevel : "N/A";
  out["safetyReason"] = state.safetyReason.length() ? state.safetyReason : "N/A";
  out["simulationMode"] = state.simulationMode;

  JsonObject storage = out["storage"].to<JsonObject>();
  storage["type"] = "LittleFS";
  storage["total"] = LittleFS.totalBytes();
  storage["used"] = LittleFS.usedBytes();
  storage["status"] = state.littleFsOk || LittleFS.totalBytes() > 0 ? "OK" : "Erreur";

  JsonObject services = out["services"].to<JsonObject>();
  services["wifi"] = wifiOk ? "OK" : (state.networkMode == "AP" || state.networkMode == "AP_STA" ? "Attention" : "Erreur");
  services["ntp"] = "N/A";
  services["mqtt"] = "N/A";
  bool anySensorOk = state.jsyOnline || state.ticAvailable;
  for (uint8_t i = 0; i < 3; i++) anySensorOk = anySensorOk || state.ds18b20Available[i];
  services["sensors"] = anySensorOk ? "OK" : "Attention";
  services["espnow"] = state.espNowReady ? "OK" : "N/A";
  services["safety"] = state.safetyTripped ? "Erreur" : "OK";

  JsonObject solar = out["solarRouter"].to<JsonObject>();
  solar["mode"] = router["mode"] | "Auto";
  setNumber("power", state.gridPowerW);
  solar["power"] = out["power"];
  out.remove("power");
  solar["outputPercent"] = outputPercent;
  if (isnan(temperature) || isinf(temperature)) solar["temperature"] = "N/A";
  else solar["temperature"] = temperature;
  if (lastMeasureAge == 4294967295UL) solar["lastMeasureAge"] = "N/A";
  else solar["lastMeasureAge"] = lastMeasureAge;
  solar["ssr1Percent"] = state.ssr1PowerPct;
  solar["ssr2Percent"] = state.ssr2PowerPct;
  solar["robotDynPercent"] = state.robotDynPowerPct;
  solar["gridPowerSource"] = router["gridPowerSource"] | state.gridPowerSource.c_str();

  sendJson(doc);
}

void WebUi::appendJsonNumber(String &out, float value, uint8_t decimals) {
  if (isnan(value) || isinf(value)) out += F("null");
  else out += String(value, static_cast<unsigned int>(decimals));
}

bool WebUi::streamLittleFsFile(const char *path, const char *contentType) {
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  if (file.size() == 0) {
    file.close();
    return false;
  }
  server.streamFile(file, contentType);
  file.close();
  return true;
}

String WebUi::fallbackStyleCss() {
  String css;
  css.reserve(5200);
  css += F("body{margin:0;background:#070708;color:#f9fafb;font-family:system-ui,Segoe UI,Arial,sans-serif}");
  css += F("nav{position:fixed;left:0;top:0;bottom:0;width:260px;box-sizing:border-box;display:flex;flex-direction:column;gap:8px;padding:18px 14px;background:#161719;border-right:1px solid #26272b;z-index:10}");
  css += F(".brand{display:block;margin:0 0 22px;color:#f6c74a;font-size:28px;font-weight:900;line-height:1.25}.brand span{color:#f9fafb}.brand small{display:block;margin-top:2px;color:#b88f31;font-size:10px;font-weight:800;text-transform:uppercase}");
  css += F("nav button,nav a{width:100%;box-sizing:border-box;text-align:left;display:flex;align-items:center;gap:8px;border:0;background:transparent;color:#e5e7eb;font-size:14px;font-weight:650;min-height:36px;text-decoration:none;padding:9px 11px;border-radius:8px}nav button.active{background:#26272b;color:#fff}.navIcon{display:none}.navSpacer{flex:1}");
  css += F(".navStatus{display:flex;align-items:center;gap:10px;min-height:40px;padding:0 12px;border:1px solid #26272b;border-radius:8px;background:#111214;color:#d1d5db;font-weight:700}.navStatus i{width:9px;height:9px;border-radius:999px;background:#9ca3af}.navStatus.ok i{background:#22c55e}.navStatus.warn i{background:#ff9800}.navStatus.bad i{background:#f44336}");
  css += F("main{margin-left:260px;padding:0 18px 18px;min-height:100vh}button,a{color:#f9fafb;background:#1f2937;border:1px solid #374151;border-radius:8px;padding:9px 11px;text-decoration:none}button.danger{border-color:#f44336;background:#3b1111}h1,h2{margin-top:0}");
  css += F(".yasTopbar{min-height:74px;display:flex;align-items:center;justify-content:space-between;gap:18px;padding:20px 4px 12px;border-bottom:1px solid #1f2024}.yasTopbar span{color:#9ca3af;font-size:13px;font-weight:800}.yasTopbar h1{margin:6px 0 0;font-size:32px;line-height:1.1}.topPills{display:flex;gap:8px;flex-wrap:wrap}.pill{background:#1b1c20;border:1px solid #2a2b30;border-radius:999px;padding:8px 10px;color:#d1d5db}.pill.ok{color:#22c55e}.pill.warn{color:#ff9800}.pill.bad{color:#f44336}.pill.info{color:#74c7f0}.pill.muted{color:#9ca3af}");
  css += F(".updateStrip,.panel,.wideChart,.yasCard,.controlCard{background:#1b1c20;border:1px solid #2a2b30;border-radius:8px}.updateStrip{display:flex;justify-content:space-between;align-items:center;gap:16px;margin:16px 0 24px;padding:18px 20px}.dashSectionTitle{margin:26px 0 12px;padding-bottom:12px;border-bottom:1px solid #1f2024}.dashSectionTitle h2{font-size:20px}");
  css += F(".controlsGrid,.yasCardGrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:14px}.yasCardGrid.gridTwo{grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}.yasCard,.controlCard{min-height:68px;display:flex;align-items:center;gap:16px;padding:14px}.roundIcon{width:48px;height:48px;border-radius:999px;display:inline-flex;align-items:center;justify-content:center;background:rgba(255,193,7,.16);color:#ffc107;font-weight:900}.ok .roundIcon{background:rgba(34,197,94,.2);color:#22c55e}.info .roundIcon{background:rgba(33,150,243,.18);color:#74c7f0}.warn .roundIcon{background:rgba(255,152,0,.18);color:#ff9800}.bad .roundIcon{background:rgba(244,67,54,.18);color:#ff6b7a}.muted .roundIcon{background:rgba(156,163,175,.12);color:#9ca3af}");
  css += F(".yasCard b,.controlCard b{display:block;color:#d1d5db;font-size:13px}.yasCard strong,.controlCard strong{display:block;margin-top:4px;font-size:18px}.yasCard small,.controlCard small{display:block;margin-top:3px;color:#9ca3af;font-size:12px}.solar strong,.solar{color:#22c55e}.consume strong,.consume{color:#ff9800}.sun strong,.sun{color:#ffc107}.heat strong,.heat{color:#f97316}.bad strong,.bad{color:#ff6b7a}.ok strong,.ok{color:#22c55e}");
  css += F(".wideCharts{display:grid;gap:16px}.wideChart{padding:16px}.wideChartHead{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}.wideChartHead b{font-size:14px}.wideChart small{display:block;margin-top:8px;color:#9ca3af}.chartWithScale{display:grid;grid-template-columns:72px 1fr;gap:10px;align-items:stretch}.chartScale{display:flex;flex-direction:column;justify-content:space-between;color:#7b808a;font-size:11px;text-align:right}.sparkline{width:100%;height:150px;background:#191a1e;border-radius:6px;overflow:hidden}.sparkline .grid line{stroke:#2a2b30;stroke-width:.6}.sparkline polyline{fill:none;stroke:#3b82f6;stroke-width:2}.sparkline .dots circle{fill:#3b82f6;stroke:#0b1220;stroke-width:.5}.zeroLine{stroke:#4b5563!important}");
  css += F(".banner,.sim,.warnBox,.pendingBox{margin:10px 0;padding:12px;border-radius:8px;border:1px solid #7f1d1d;background:#3b1111;color:#fecaca}.sim{border-color:#92400e;background:#3a2608;color:#fde68a}.help,.muted,small{color:#9ca3af}.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0}.settingsGrid,.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}.card,.miniState{background:#1f2937;border:1px solid #374151;border-radius:8px;padding:12px}table{width:100%;border-collapse:collapse;background:#1f2937}td,th{padding:8px;border-bottom:1px solid #374151;text-align:left}.badge{display:inline-block;border-radius:999px;padding:3px 8px;background:#374151}.badge.ok{background:#14532d;color:#bbf7d0}.badge.bad{background:#7f1d1d;color:#fecaca}.badge.warn{background:#78350f;color:#fde68a}.badge.muted{background:#374151;color:#d1d5db}");
  css += F("@media(max-width:760px){nav{position:static;width:auto;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));padding:10px}.brand{grid-column:1/-1;margin-bottom:4px}main{margin-left:0;padding:12px}.yasTopbar{display:block}.controlsGrid,.yasCardGrid,.yasCardGrid.gridTwo{grid-template-columns:1fr}table{display:block;overflow-x:auto}}");
  return css;
}

void WebUi::sendFsListJson() {
  String out;
  out.reserve(2048);
  out += F("{\"mounted\":true,\"totalBytes\":");
  out += String(LittleFS.totalBytes());
  out += F(",\"usedBytes\":");
  out += String(LittleFS.usedBytes());
  out += F(",\"files\":[");
  appendFsListJson(out, "/");
  out += F("]}");
  server.send(200, "application/json", out);
}

void WebUi::appendFsListJson(String &out, const char *dirname) {
  File root = LittleFS.open(dirname);
  if (!root || !root.isDirectory()) return;
  File file = root.openNextFile();
  bool first = out.endsWith("[");
  while (file) {
    if (!first) out += ",";
    first = false;
    String name = file.name();
    if (!name.startsWith("/")) name = String("/") + name;
    out += F("{\"name\":\"");
    out += name;
    out += F("\",\"size\":");
    out += String(file.size());
    out += F(",\"dir\":");
    out += file.isDirectory() ? F("true") : F("false");
    out += F("}");
    if (file.isDirectory()) appendFsListJson(out, name.c_str());
    file = root.openNextFile();
  }
}

String WebUi::fsPage() {
  String html;
  html.reserve(1400);
  html += F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LittleFS</title><style>body{background:#111827;color:#F9FAFB;font-family:system-ui;padding:18px}a{color:#93C5FD}pre{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:12px;white-space:pre-wrap}</style></head><body><h1>LittleFS</h1><p><a href=\"/\">Retour</a> <a href=\"/api/fs\">JSON</a></p><pre id=\"out\">Chargement...</pre><script>fetch('/api/fs').then(r=>r.text()).then(t=>{try{out.textContent=JSON.stringify(JSON.parse(t),null,2)}catch(e){out.textContent=t}}).catch(e=>out.textContent=e.message)</script></body></html>");
  return html;
}

void WebUi::sendConfig(const char *name) {
  String out;
  if (strcmp(name, "device") == 0) serializeJson(config.deviceDoc(), out);
  if (strcmp(name, "system") == 0) serializeJson(config.systemDoc(), out);
  if (strcmp(name, "sensors") == 0) serializeJson(config.sensorsDoc(), out);
  if (strcmp(name, "actuators") == 0) serializeJson(config.actuatorsDoc(), out);
  if (strcmp(name, "rules") == 0) serializeJson(config.rulesDoc(), out);
  server.send(200, "application/json", out);
}

void WebUi::saveConfig(const char *name, const char *path) {
  if (strcmp(name, "rules") == 0) {
    String errors;
    if (!rules.validateRulesPayload(server.arg("plain"), errors)) {
      server.send(400, "application/json", errors);
      return;
    }
  }
  if (strcmp(name, "sensors") == 0 || strcmp(name, "actuators") == 0) {
    DynamicJsonDocument check(24576);
    DeserializationError err = deserializeJson(check, server.arg("plain"));
    if (err) {
      DynamicJsonDocument out(384);
      out["ok"] = false;
      out["error"] = String("JSON invalide: ") + err.c_str();
      sendJson(out);
      return;
    }
    if (check.overflowed()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON trop volumineux pour validation\"}");
      return;
    }
    if (strcmp(name, "sensors") == 0) {
      int busGpio = check["oneWireBus"]["gpio"] | 13;
      if (busGpio < 0 || busGpio > 39) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"GPIO OneWire invalide\"}");
        return;
      }
    }
    JsonArray arr = strcmp(name, "sensors") == 0 ? check["sensors"].as<JsonArray>() : check["actuators"].as<JsonArray>();
    for (JsonObject item : arr) {
      int pins[] = {item["gpio"] | -1, item["rx"] | -1, item["tx"] | -1, item["zeroCross"] | -1, item["control"] | -1};
      for (uint8_t i = 0; i < 5; i++) {
        if (pins[i] != -1 && (pins[i] < 0 || pins[i] > 39)) {
          server.send(400, "application/json", "{\"ok\":false,\"error\":\"GPIO invalide\"}");
          return;
        }
      }
    }
  }
  bool ok = config.replaceFile(path, server.arg("plain"));
  if (ok && strcmp(name, "device") == 0) state.begin(config);
  if (ok && strcmp(name, "system") == 0) {
    state.simulationType = config.system()["simulation"]["mode"] | state.simulationType.c_str();
    state.simulationScenario = config.system()["simulation"]["scenario"] | state.simulationScenario.c_str();
    state.gridPowerSource = config.system()["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
  }
  if (ok && strcmp(name, "sensors") == 0) sensors.reloadConfiguration();
  if (ok) {
    server.send(200, "application/json", "{\"ok\":true}");
  } else {
    DynamicJsonDocument out(512);
    out["ok"] = false;
    out["error"] = config.lastError().length() ? config.lastError() : "Sauvegarde refusee";
    sendJson(out);
  }
}

String WebUi::homePage() {
  String html;
  html.reserve(2200);
  html += F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>RouteurSolaireESP32</title>");
  html += F("<style>body{margin:0;background:#111827;color:#F9FAFB;font-family:system-ui,Arial,sans-serif;padding:18px}a,button{display:inline-block;margin:6px 6px 6px 0;background:#1F2937;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:10px 12px;text-decoration:none}.card{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:14px;margin:10px 0}.muted{color:#9CA3AF}.ok{color:#22C55E}.warn{color:#FF9800}</style>");
  html += F("</head><body><h1>RouteurSolaireESP32</h1>");
  html += F("<div class=\"card\"><b class=\"ok\">Serveur Web OK</b><p class=\"muted\">Cette page est volontairement simple, sans JavaScript. Si elle s'affiche, le WiFi et le serveur HTTP fonctionnent.</p></div>");
  html += F("<div class=\"card\"><h2>Acces</h2>");
  html += F("<a href=\"/lite\">Interface Lite</a><a href=\"/app\">WebUI LittleFS</a><a href=\"/full\">Interface complete</a><a href=\"/fs\">Voir LittleFS</a><a href=\"/api/status-lite\">API status-lite</a><a href=\"/api/status\">API status</a><a href=\"/api/diagnostic\">API diagnostic</a><a href=\"/README.md\">README</a>");
  html += F("</div><div class=\"card\"><h2>Reseau</h2><p>Mode: ");
  html += state.networkMode;
  html += F("</p><p>IP box: ");
  html += state.stationIp;
  html += F("</p><p>IP AP: ");
  html += state.apIp;
  html += F("</p><p class=\"warn\">Si tu es connecte au point d'acces local RouteurSolaire_Config, utilise http://192.168.4.1/</p></div>");
  html += F("</body></html>");
  return html;
}

String WebUi::litePage() {
  return R"HTML(<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RouteurSolaireESP32 Lite</title>
<style>
body{margin:0;background:#111827;color:#F9FAFB;font-family:system-ui,Segoe UI,sans-serif}nav{background:#050505;padding:12px;display:flex;gap:8px;flex-wrap:wrap;position:sticky;top:0}button{background:#1F2937;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:10px 12px}main{padding:14px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}.card{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:12px}.label{color:#9CA3AF;font-size:12px}.value{font-size:22px;margin-top:6px}.ok{color:#22C55E}.bad{color:#F44336}.warn{color:#FF9800}table{width:100%;border-collapse:collapse;background:#1F2937;margin-top:10px}td,th{padding:8px;border-bottom:1px solid #374151;text-align:left}input,select{width:100%;background:#0B1220;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:9px}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}.banner{background:#7f1d1d;border:1px solid #F44336;border-radius:8px;padding:10px;margin:8px 0}.sim{background:#2a1b08;border:1px solid #FF9800;border-radius:8px;padding:10px;margin:8px 0}
</style></head><body>
<nav><button onclick="show('dash')">Dashboard</button><button onclick="show('sim')">Simulation</button><button onclick="show('diag')">Diagnostic</button><button onclick="show('wifi')">WiFi</button><button onclick="location.href='/full'">Interface complete</button></nav>
<main id="v">Chargement...</main>
<script>
let status={}, current='dash';
function q(s){return document.querySelector(s)}
function esc(v){return String(v==null?'':v).replace(/[&<>"']/g,function(m){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]})}
function fmt(v){return typeof v==='number'?Math.round(v*10)/10:v}
async function api(p,o){let r=await fetch(p,o);let t=await r.text();if(!r.ok)throw new Error('HTTP '+r.status+' '+t);try{return JSON.parse(t)}catch(e){return t}}
function card(l,v,u,c){return '<div class="card"><div class="label">'+l+'</div><div class="value '+(c||'')+'">'+esc(fmt(v))+' '+(u||'')+'</div></div>'}
function emptyStatus(){return {networkMode:'inconnu',localIp:'-',stationIp:'-',apIp:'192.168.4.1',wifiConnected:false,wifiSsid:'-',rssi:0,injectionW:0,gridPowerW:0,ssr1PowerPct:0,ssr2PowerPct:0,robotDynPowerPct:0,tankTopC:0,tankMiddleC:0,tankBottomC:0,heapFree:0,events:[]}}
async function load(diag){try{status=await api(diag?'/api/diagnostic':'/api/status-lite')}catch(e){status=emptyStatus();status.loadError=e.message}}
function top(){return (status.safetyTripped?'<div class="banner">SECURITE: '+esc(status.safetyReason)+'</div>':'')+(status.simulationMode?'<div class="sim">MODE SIMULATION ACTIF - sorties puissance OFF</div>':'')}
async function show(p){current=p;await render()}
async function render(){q('#v').innerHTML='<h2>RouteurSolaireESP32</h2><p>Chargement API...</p>';await load(current==='diag');let h='<h2>RouteurSolaireESP32</h2>'+(status.loadError?'<div class="banner">Erreur API: '+esc(status.loadError)+'<br>Teste /api/ping puis /api/status</div>':'')+top();if(current==='dash')h+=dash();if(current==='sim')h+=sim();if(current==='diag')h+=diag();if(current==='wifi')h+=wifi();q('#v').innerHTML=h}
function dash(){return '<div class="grid">'+card('Mode reseau',status.networkMode,'')+card('IP box',status.stationIp||status.localIp,'')+card('IP AP',status.apIp||'192.168.4.1','')+card('Injection',status.injectionW,'W','ok')+card('Reseau',status.gridPowerW,'W')+card('SSR1',status.ssr1PowerPct,'%')+card('SSR2',status.ssr2PowerPct,'%')+card('RobotDyn',status.robotDynPowerPct,'%')+card('Sonde1',status.tankTopC,'C')+card('Sonde2',status.tankMiddleC,'C')+card('Sonde3',status.tankBottomC,'C')+card('Heap',status.heapFree,'o')+'</div>'}
function sim(){return '<h3>Simulation sans capteurs</h3><div class="row"><div><label>gridPowerW</label><input id="g" type="number" value="'+(status.gridPowerW||-800)+'"></div><div><label>Tension</label><input id="vlt" type="number" value="'+(status.gridVoltageV||231)+'"></div><div><label>Courant</label><input id="cur" type="number" value="'+(status.gridCurrentA||4)+'"></div><div><label>Sonde1</label><input id="t1" type="number" value="'+(status.tankTopC||45)+'"></div><div><label>Sonde2</label><input id="t2" type="number" value="'+(status.tankMiddleC||42)+'"></div><div><label>Sonde3</label><input id="t3" type="number" value="'+(status.tankBottomC||38)+'"></div></div><p><button onclick="simOn()">Activer</button> <button onclick="simValues()">Appliquer</button> <button onclick="simRand()">Aleatoire</button> <button onclick="simOff()">Desactiver</button></p>'+dash()}
async function simOn(){await fetch('/api/simulation/enable',{method:'POST'});await render()}
async function simOff(){await fetch('/api/simulation/disable',{method:'POST'});await render()}
async function simRand(){await fetch('/api/simulation/randomize',{method:'POST'});await render()}
async function simValues(){let body={jsy:{available:true,gridPowerW:Number(q('#g').value),voltageV:Number(q('#vlt').value),currentA:Number(q('#cur').value),powerFactor:.96,frequencyHz:50},tic:{available:true,apparentPowerVA:900,currentA:Number(q('#cur').value),tariff:'BASE'},ds18b20:[{id:'sonde1',available:true,temperatureC:Number(q('#t1').value)},{id:'sonde2',available:true,temperatureC:Number(q('#t2').value)},{id:'sonde3',available:true,temperatureC:Number(q('#t3').value)}]};await fetch('/api/simulation/values',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});await render()}
function diag(){let ev=status.events||[];return dash()+'<h3>Evenements</h3><table><tr><th>ms</th><th>Niveau</th><th>Code</th><th>Message</th></tr>'+ev.map(function(e){return '<tr><td>'+e.timestampMs+'</td><td>'+esc(e.level)+'</td><td>'+esc(e.code)+'</td><td>'+esc(e.message)+'</td></tr>'}).join('')+'</table>'}
function wifi(){return '<div class="grid">'+card('Mode',status.networkMode,'')+card('WiFi connecte',status.wifiConnected,'')+card('SSID',status.wifiSsid,'')+card('RSSI',status.rssi,'')+card('IP box',status.stationIp,'')+card('IP AP',status.apIp,'')+'</div><p>Si tu es connecte au point d\\'acces local RouteurSolaire_Config, ouvre http://192.168.4.1</p>'}
setInterval(function(){if(current==='dash')render()},5000);render();
</script></body></html>
)HTML";
}

String WebUi::page() {
  return R"HTML(<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RouteurSolaireESP32</title>
<style>
:root{--bg:#111827;--menu:#050505;--card:#1F2937;--text:#F9FAFB;--muted:#9CA3AF;--ok:#22C55E;--info:#2196F3;--warn:#FF9800;--bad:#F44336;--sun:#FFC107}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,Segoe UI,sans-serif}
nav{position:fixed;left:0;top:0;bottom:0;width:220px;background:var(--menu);padding:18px 12px;display:flex;flex-direction:column;gap:8px}
nav h1{font-size:17px;margin:0 0 14px}button{background:#263244;color:var(--text);border:1px solid #374151;border-radius:8px;padding:10px 12px;text-align:left;cursor:pointer}
button.active,button:hover{border-color:var(--info)}main{margin-left:220px;padding:20px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:14px}
.card{background:var(--card);border:1px solid #374151;border-radius:8px;padding:14px;min-height:86px}.label{color:var(--muted);font-size:13px}.value{font-size:24px;margin-top:8px}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.sun{color:var(--sun)}
.dash{display:grid;gap:14px}.hero{display:grid;grid-template-columns:1.4fr .8fr .8fr;gap:14px}.panel{background:linear-gradient(180deg,#243044,#1F2937);border:1px solid #374151;border-radius:8px;padding:16px;box-shadow:0 12px 28px #0005}.panel h3{margin:0 0 12px;font-size:15px;color:var(--muted);font-weight:600}.big{font-size:32px;font-weight:750;line-height:1}.sub{color:var(--muted);font-size:13px;margin-top:8px}.statusline{display:flex;align-items:center;justify-content:space-between;gap:10px;margin:9px 0;font-size:13px}.statusline b{font-size:13px}.pill{display:inline-flex;align-items:center;gap:7px;border:1px solid #45556f;border-radius:999px;padding:5px 9px;font-size:12px;color:var(--text);background:#111827}.dot{width:9px;height:9px;border-radius:50%;background:var(--muted);box-shadow:0 0 14px currentColor}.dot.ok{background:var(--ok)}.dot.bad{background:var(--bad)}.dot.warn{background:var(--warn)}.dot.info{background:var(--info)}.bar{height:9px;background:#111827;border-radius:999px;overflow:hidden;border:1px solid #374151}.fill{height:100%;width:0;background:var(--info);border-radius:999px}.fill.ok{background:var(--ok)}.fill.warn{background:var(--warn)}.fill.bad{background:var(--bad)}.fill.sun{background:var(--sun)}.mini{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px}.metric{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin:10px 0;font-size:13px}.metric b{font-size:15px}.spark{height:48px;border-radius:8px;background:linear-gradient(90deg,#132033,#182235);position:relative;overflow:hidden}.spark:after{content:"";position:absolute;inset:24px -20px auto -20px;height:2px;background:linear-gradient(90deg,transparent,var(--sun),var(--ok),transparent);box-shadow:20px -10px 0 #2196F388,80px 8px 0 #22C55E99,150px -5px 0 #FFC10799}
textarea,input,select{width:100%;background:#0B1220;color:var(--text);border:1px solid #374151;border-radius:8px;padding:10px}textarea{min-height:420px;font-family:ui-monospace,Consolas,monospace;font-size:13px}.readmeBox{white-space:pre-wrap;background:#0B1220;border:1px solid #374151;border-radius:8px;padding:16px;line-height:1.48;color:var(--text);overflow:auto}
.sensorTop{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:0 0 14px}.sensorStat{background:#172033;border:1px solid #334155;border-radius:8px;padding:12px}.sensorStat b{display:block;font-size:22px;margin-top:4px}.sensorPage{display:grid;grid-template-columns:1fr 340px;gap:14px;align-items:start}.sensorTableWrap{background:var(--card);border:1px solid #374151;border-radius:8px;overflow:auto}.sensorTable{width:100%;border-collapse:collapse;background:transparent;min-width:860px}.sensorTable th{font-size:12px;color:var(--muted);font-weight:600;background:#172033;position:sticky;top:0}.sensorTable td,.sensorTable th{padding:10px;border-bottom:1px solid #374151;text-align:left;vertical-align:middle}.sensorTable tr.off{opacity:.55}.sensorName{font-weight:700;font-size:14px}.sensorId{font-size:12px;color:var(--muted);margin-top:2px}.sensorBadge{display:inline-flex;font-size:11px;border:1px solid #45556f;border-radius:999px;padding:4px 8px;background:#111827;color:var(--muted);white-space:nowrap}.sensorValue{font-size:15px;font-weight:750}.sensorActions{display:flex;gap:6px;flex-wrap:nowrap}.sensorActions button,.compactBtn{padding:7px 9px;font-size:12px;white-space:nowrap}.formPanel{position:sticky;top:16px;background:linear-gradient(180deg,#243044,#1F2937);border:1px solid #374151;border-radius:8px;padding:14px}.formPanel label{display:block;font-size:12px;color:var(--muted);margin:9px 0 4px}.formGrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.dsList{display:grid;gap:6px;margin-top:8px}.dsChip{display:flex;justify-content:space-between;gap:8px;align-items:center;border:1px solid #374151;background:#111827;border-radius:8px;padding:8px;font-size:12px}
.actuatorTop{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:0 0 14px}.actuatorStat{background:#172033;border:1px solid #334155;border-radius:8px;padding:12px}.actuatorStat b{display:block;font-size:22px;margin-top:4px}.actuatorPage{display:grid;grid-template-columns:1fr 340px;gap:14px;align-items:start}.actuatorTableWrap{background:var(--card);border:1px solid #374151;border-radius:8px;overflow:auto}.actuatorTable{width:100%;border-collapse:collapse;background:transparent;min-width:900px}.actuatorTable th{font-size:12px;color:var(--muted);font-weight:600;background:#172033;position:sticky;top:0}.actuatorTable td,.actuatorTable th{padding:10px;border-bottom:1px solid #374151;text-align:left;vertical-align:middle}.actuatorTable tr.off{opacity:.55}
.logicTop{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:0 0 14px}.logicStat{background:#172033;border:1px solid #334155;border-radius:8px;padding:12px}.logicStat b{display:block;font-size:22px;margin-top:4px}.logicPage{display:grid;grid-template-columns:1fr 420px;gap:14px;align-items:start}.logicTableWrap{background:var(--card);border:1px solid #374151;border-radius:8px;overflow:auto}.logicTable{width:100%;border-collapse:collapse;background:transparent;min-width:980px}.logicTable th{font-size:12px;color:var(--muted);font-weight:600;background:#172033;position:sticky;top:0}.logicTable td,.logicTable th{padding:10px;border-bottom:1px solid #374151;text-align:left;vertical-align:top}.logicTable tr.off{opacity:.55}.ruleSummary{display:grid;gap:6px}.ruleLine{font-size:12px;color:var(--muted)}.ruleLine b{color:var(--text)}.ruleRows{display:grid;gap:8px;margin-top:8px}.ruleRow{display:grid;grid-template-columns:1fr 1fr .72fr .85fr .45fr auto;gap:6px;align-items:center}.ruleActionRow{display:grid;grid-template-columns:1fr 1fr .75fr .85fr auto;gap:6px;align-items:center}.ruleSourceRow{display:grid;grid-template-columns:1fr 1fr;gap:6px;grid-column:1/-1}.ruleRow input,.ruleRow select,.ruleActionRow input,.ruleActionRow select,.ruleSourceRow input,.ruleSourceRow select{padding:7px;font-size:12px}.unitCell{font-size:12px;color:var(--muted);text-align:center}.invalidBox{margin:8px 0;color:var(--warn);font-size:12px}
.settingsGrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px}.settingsPanel{background:var(--card);border:1px solid #374151;border-radius:8px;padding:14px}.settingsPanel h3{margin:0 0 10px;font-size:15px;color:var(--muted)}.settingsPanel label{display:block;font-size:12px;color:var(--muted);margin:9px 0 4px}.switchRow{display:flex;align-items:center;justify-content:space-between;gap:12px;border-bottom:1px solid #374151;padding:8px 0}.switchRow span{font-size:13px}.switchRow select{width:92px;padding:7px}.settingsPanel input,.settingsPanel select{padding:8px}
.safetyBanner{background:#7f1d1d;border:1px solid var(--bad);color:#fff;border-radius:8px;padding:12px 14px;margin:0 0 14px;font-weight:700}.danger{border-color:var(--bad);background:#3b1111}.warnBox{border:1px solid var(--warn);background:#2a1b08;border-radius:8px;padding:9px;margin:8px 0;color:#ffd18a}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.toolbar{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}.small{font-size:12px;color:var(--muted)}table{width:100%;border-collapse:collapse;background:var(--card)}td,th{padding:9px;border-bottom:1px solid #374151;text-align:left}
@media(max-width:980px){.sensorPage,.actuatorPage,.logicPage{grid-template-columns:1fr}.formPanel{position:static}.hero{grid-template-columns:1fr}}@media(max-width:760px){nav{position:static;width:auto;display:grid;grid-template-columns:repeat(2,1fr)}main{margin-left:0}.row{grid-template-columns:1fr}.big{font-size:32px}}
</style></head><body><nav><h1>RouteurSolaireESP32</h1>
<button onclick="show('dashboard')" class="active">Dashboard</button><button onclick="show('sensors')">Capteurs</button><button onclick="show('actuators')">Actionneurs</button>
<button onclick="show('logic')">Logique</button><button onclick="show('diag')">Diagnostic</button><button onclick="show('settings')">Parametres</button><button onclick="show('install')">Installation</button><button onclick="show('readme')">README</button>
</nav><main><section id="view"></section></main>
<script>
let current='dashboard', status={}, cache={};
const cards=[['gridPowerW','puissance reseau','W'],['injectionW','injection reseau','W'],['consumptionW','consommation maison','W'],['productionW','production solaire','W'],['ssr1PowerPct','puissance chauffe-eau','%'],['tankTopC','temperature ballon haut','C'],['tankMiddleC','temperature ballon milieu','C'],['tankBottomC','temperature ballon bas','C'],['ssr1PowerPct','etat SSR1','%'],['ssr2PowerPct','etat SSR2','%'],['robotDynPowerPct','etat RobotDyn','%'],['systemMode','mode systeme',''],['wifiConnected','WiFi status',''],['espNowReady','ESP-NOW status',''],['uptime','uptime','s'],['heapFree','heap libre','o']];
function q(s){return document.querySelector(s)} function fmt(v){return typeof v==='number'?Math.round(v*10)/10:v}
function esc(v){return String(v??'').replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]))}
async function api(p,o){let r=await fetch(p,o);return r.headers.get('content-type')?.includes('json')?r.json():r.text()}
async function refresh(){status=await api(current==='diag'?'/api/diagnostic':'/api/status'); if(current==='dashboard'||current==='diag') render()}
function show(v){current=v;document.querySelectorAll('nav button').forEach(b=>b.classList.remove('active'));event?.target?.classList.add('active');render()}
function card(k,l,u){let v=status[k];let c=(k.includes('temp')&&v>=70)||status.safetyTripped?'bad':(v===true?'ok':'');return `<div class="card"><div class="label">${l}</div><div class="value ${c}">${fmt(v)} ${u}</div></div>`}
function pct(v,max){return Math.max(0,Math.min(100,(Number(v)||0)/max*100))}
function pill(label,ok,warn){let c=ok?'ok':(warn?'warn':'bad');return `<span class="pill"><i class="dot ${c}"></i>${label}</span>`}
function meter(label,value,unit,max,cls='info'){let p=pct(value,max);return `<div class="metric"><span>${label}</span><b>${fmt(value)} ${unit}</b></div><div class="bar"><div class="fill ${cls}" style="width:${p}%"></div></div>`}
function panel(title,body){return `<section class="panel"><h3>${title}</h3>${body}</section>`}
function simPanel(){let scenarios=[['normal','Retour normal'],['production_low','Production faible'],['injection_medium','Injection moyenne'],['injection_high','Forte injection'],['tank_almost_hot','Ballon presque chaud'],['tank_overheat','Surchauffe ballon'],['critical_sensor_lost','Perte capteur critique'],['jsy_lost','Perte JSY']];return `<div class="statusline"><span>Etat</span><b>${status.simulationMode?'ACTIF':'inactif'} / ${status.simulationType||'manual'} / ${status.simulationScenario||'normal'}</b></div><div class="formGrid"><div><label>Mode</label><select id="simMode"><option value="manual" ${status.simulationType==='manual'?'selected':''}>manuel</option><option value="random" ${status.simulationType==='random'?'selected':''}>automatique aleatoire</option><option value="scenario" ${status.simulationType==='scenario'?'selected':''}>scenario</option></select></div><div><label>Scenario</label><select id="simScenario">${scenarios.map(s=>`<option value="${s[0]}" ${status.simulationScenario===s[0]?'selected':''}>${s[1]}</option>`).join('')}</select></div><div><label>Puissance reseau W</label><input id="simGrid" type="number" value="${status.gridPowerW||-800}"></div><div><label>Tension V</label><input id="simVoltage" type="number" value="${status.gridVoltageV||231}"></div><div><label>Courant A</label><input id="simCurrent" type="number" step="0.1" value="${status.gridCurrentA||3.7}"></div><div><label>Facteur puissance</label><input id="simPf" type="number" step="0.01" value="${status.gridPowerFactor||0.96}"></div><div><label>Frequence Hz</label><input id="simFreq" type="number" step="0.1" value="${status.gridFrequencyHz||50}"></div><div><label>TIC VA</label><input id="simTicVa" type="number" value="${status.ticApparentPowerVA||900}"></div><div><label>Sonde 1 C</label><input id="simTop" type="number" step="0.1" value="${status.tankTopC||45}"></div><div><label>Sonde 2 C</label><input id="simMiddle" type="number" step="0.1" value="${status.tankMiddleC||42}"></div><div><label>Sonde 3 C</label><input id="simBottom" type="number" step="0.1" value="${status.tankBottomC||38}"></div><div><label>Capteurs disponibles</label><select id="simAvailable"><option value="true">oui</option><option value="false">non JSY</option></select></div></div><div class="toolbar"><button onclick="startSimulation()">Activer</button><button onclick="setSimulationMode()">Mode</button><button onclick="applyScenario()">Appliquer scenario</button><button onclick="randomSimulation()">Generer valeurs</button><button onclick="applySimulation()">Appliquer manuel</button><button onclick="stopSimulation()">Desactiver</button></div><div class="small">Commandes simulees: SSR1 ${fmt(status.ssr1PowerPct)}%, SSR2 ${fmt(status.ssr2PowerPct)}%, RobotDyn ${fmt(status.robotDynPowerPct)}%. Les GPIO 230 V restent OFF.</div>`}
function safetyBanner(){let h=status.safetyTripped?`<div class="safetyBanner">SECURITE CRITIQUE - ${esc(status.safetyReason||'sorties coupees')}</div>`:'';if(status.simulationMode)h+=`<div class="warnBox">SIMULATION ACTIVE - les commandes sont calculees mais les GPIO restent OFF.</div>`;return h}
function renderDashboard(){
let safe=!status.safetyTripped, wifi=!!status.wifiConnected, ap=status.networkMode==='AP_FALLBACK';
let dsCfg=(cache.sensors?.ds18b20)||[], dsLive=status.ds18b20||[];
let tempMax=dsLive.reduce((m,s)=>Math.max(m,Number(s.temperatureC)||0),0);
let actionPower=(Number(status.ssr1PowerPct)||0)+(Number(status.ssr2PowerPct)||0)+(Number(status.robotDynPowerPct)||0);
let dsPanel=dsCfg.map((s,i)=>{let live=dsLive.find(x=>x.id===s.id)||dsLive[i]||{};let value=live.available?live.temperatureC:0;let cls=s.critical&&!live.available?'bad':(Number(value)>=70?'bad':'ok');return `${meter(`${s.id} - ${s.role||'autre'}${s.critical?' *':''}`,live.available?value:'absent',live.available?'C':'',80,cls)}`}).join('')||'<div class="small">Aucune sonde configuree</div>';
return `${safetyBanner()}<div class="dash">
<div class="hero">
${panel('Energie solaire',`<div class="big sun">${fmt(status.injectionW)} W</div><div class="sub">Surplus disponible - reseau ${fmt(status.gridPowerW)} W</div><div class="spark"></div><div class="statusline">${pill(status.systemMode||'AUTO',true)}${pill(safe?'Securite OK':'Securite active',safe,!safe)}</div><div class="toolbar"><button class="danger" onclick="manualStop()">Arret urgence</button><button onclick="restart()">Redemarrer</button></div>`)}
${panel('ESP32',`<div class="statusline"><span>Role</span><b>${status.role}</b></div><div class="statusline"><span>Simulation</span><b>${status.simulationMode?'Oui':'Non'}</b></div><div class="statusline"><span>Uptime</span><b>${fmt(status.uptime)} s</b></div>${meter('Heap libre',status.heapFree,'o',327680,'ok')}`)}
${panel('Reseau',`<div class="statusline"><span>Mode</span><b>${status.networkMode}</b></div><div class="statusline"><span>SSID</span><b>${status.wifiSsid||'-'}</b></div><div class="statusline"><span>IP box</span><b>${status.stationIp||status.localIp}</b></div><div class="statusline"><span>IP AP</span><b>${status.apIp||'192.168.4.1'}</b></div><div class="statusline">${pill(wifi?'WiFi connecte':(ap?'AP fallback':'WiFi attente'),wifi,ap)}${pill(status.espNowReady?'ESP-NOW pret':'ESP-NOW off',status.espNowReady,false)}</div>`)}
</div>
<div class="mini">
${panel('Sondes DS18B20',dsPanel)}
${panel('Actionneurs puissance',`${meter('SSR1 chauffe-eau',status.ssr1PowerPct,'%',100,'sun')}${meter('SSR2 auxiliaire',status.ssr2PowerPct,'%',100,'info')}${meter('RobotDyn triac',status.robotDynPowerPct,'%',100,'warn')}<div class="statusline">${pill(actionPower>0?'Puissance active':'Sorties au repos',actionPower>0,false)}</div>`)}
${panel('Flux maison',`${meter('Injection',status.injectionW,'W',3000,'ok')}${meter('Consommation',status.consumptionW,'W',3000,'warn')}${meter('Production',status.productionW,'W',5000,'sun')}<div class="statusline"><span>JSY</span>${pill(status.jsyOnline?'JSY en ligne':'JSY absent',status.jsyOnline,false)}</div><div class="statusline"><span>TIC</span>${pill(status.ticAvailable?'TIC OK':(status.ticStatus||'TIC absent'),status.ticAvailable,false)}</div><div class="statusline"><span>${fmt(status.gridVoltageV)} V / ${fmt(status.gridCurrentA)} A</span><b>PF ${fmt(status.gridPowerFactor)}</b></div>`)}
${panel('Redondance',`<div class="statusline"><span>Etat</span><b>${status.redundancyState||'-'}</b></div><div class="statusline"><span>Master actif</span><b>${status.activeMasterId||'-'}</b></div><div class="statusline"><span>Dernier heartbeat</span><b>${status.lastMasterHeartbeatAgeMs??'-'} ms</b></div><div class="statusline"><span>Epoch</span><b>${status.epoch??0}</b></div><div class="statusline">${pill(status.splitBrainDetected?'Split brain':(status.masterAlive||status.isActiveMaster?'Redondance OK':'Backup en attente'),!status.splitBrainDetected&&(status.masterAlive||status.isActiveMaster),true)}</div>`)}
${panel('Simulation sans capteurs',simPanel())}
</div></div>`}
const sensorTemplates=['JSY-MK-194T','TIC Linky','DS18B20','Entree analogique','Entree digitale','Capteur virtuel'];
const actuatorTemplates=['SSR','RobotDyn Triac','Relais','PWM','Sortie digitale','Actionneur virtuel'];
const actuatorModes=['OFF','ON_OFF','BURST_FIRE','TRAIN_ONDES_ENTIERES','ZERO_CROSS_BURST','LOW_FREQ_PWM','PHASE_ANGLE','MANUAL_SAFE'];
function sensorLive(s){let id=s.id;if(id==='jsy_grid')return `${fmt(status.activePowerW1)} W / ${fmt(status.activePowerW2)} W`;if(id==='tic_linky')return status.ticAvailable?`${fmt(status.ticApparentPowerVA)} VA / ${fmt(status.ticCurrentA)} A`:(status.ticStatus||'absent');if(id.startsWith('sonde')){let live=(status.ds18b20||[]).find(x=>x.id===id);return live?.available?`${fmt(live.temperatureC)} C`:'absent'}return s.enabled?'pret':'off'}
function sensorPins(s){let p=[];if(s.gpio!==undefined)p.push('GPIO '+s.gpio);if(s.rx!==undefined)p.push('RX '+s.rx);if(s.tx!==undefined)p.push('TX '+s.tx);if(s.address)p.push(s.address);if(s.mac)p.push(s.mac);return p.length?p.map(x=>`<span class="sensorBadge">${esc(x)}</span>`).join(' '):'<span class="small">-</span>'}
function sensorState(s){if(s.id==='jsy_grid')return status.jsyOnline?'OK':'Erreur';if(s.id==='tic_linky')return status.ticAvailable?'OK':(status.ticStatus||'Erreur');if(s.id?.startsWith('sonde')){let live=(status.ds18b20||[]).find(x=>x.id===s.id);return live?.available?'OK':'Absent'}return s.enabled!==false?'OK':'Inactif'}
function sensorLastRead(s){if(s.id==='jsy_grid')return status.lastJsyReadMs?`${Math.max(0,Date.now()*0+status.uptime*1000-status.lastJsyReadMs)} ms`:'-';if(s.id==='tic_linky')return status.lastTicReadMs?`${Math.max(0,status.uptime*1000-status.lastTicReadMs)} ms`:'-';if(s.id?.startsWith('sonde')){let live=(status.ds18b20||[]).find(x=>x.id===s.id);return live?.lastReadMs?`${Math.max(0,status.uptime*1000-live.lastReadMs)} ms`:'-'}return '-'}
function sensorUnit(s){if(s.type==='DS18B20'||s.id?.startsWith('sonde'))return 'C';if(s.id==='tic_linky')return 'VA/A';if(s.id==='jsy_grid')return 'W';return ''}
function sensorRow(s,i){let online=s.enabled!==false;let local=(s.source||'local')==='local';let st=sensorState(s);let critical=s.critical?'<span class="sensorBadge">oui</span>':'<span class="small">non</span>';let actions=s._group==='ds18b20'?'<span class="small">Config via Mode JSON</span>':`<div class="sensorActions"><button onclick="editSensor(${s._index})">Modifier</button><button onclick="toggleSensor(${s._index})">${online?'Off':'On'}</button><button onclick="alert('${esc(st)}')">Tester</button><button onclick="copySensor(${s._index})">Copier</button><button onclick="deleteSensor(${s._index})">Suppr.</button></div>`;return `<tr class="${online?'':'off'}"><td><div class="sensorName">${esc(s.name||s.id)}</div><div class="sensorId">${esc(s.id)}</div></td><td><span class="sensorBadge">${esc(s.type||'DS18B20')}</span></td><td class="small">${esc(s.role||'')}</td><td>${sensorPins(s)}</td><td><span class="sensorValue">${esc(sensorLive(s))}</span></td><td>${sensorUnit(s)}</td><td>${pill(st,st==='OK',st!=='Absent'&&st!=='Erreur')}</td><td class="small">${sensorLastRead(s)}</td><td>${critical}</td><td>${actions}</td></tr>`}
function allSensorsForUi(){let base=(cache.sensors?.sensors||[]).map((s,i)=>Object.assign({type:s.type||'Capteur',_group:'sensors',_index:i},s));let ds=(cache.sensors?.ds18b20||[]).map((s,i)=>Object.assign({type:'DS18B20',source:'local',gpio:cache.sensors?.oneWireBus?.gpio||13,_group:'ds18b20',_index:i},s));return base.concat(ds)}
async function renderSensorsPage(load=true){if(load||!cache.sensors)cache.sensors=await api('/api/sensors');let arr=allSensorsForUi();let ok=arr.filter(s=>sensorState(s)==='OK').length;let err=arr.length-ok;let critMissing=!!status.ds18b20CriticalMissing;let active=arr.filter(s=>s.enabled!==false).length;q('#view').innerHTML=`<h2>Capteurs</h2>${safetyBanner()}${critMissing?'<div class="warnBox">Attention: sonde critique absente</div>':''}<div class="sensorTop"><div class="sensorStat"><span class="label">Total</span><b>${arr.length}</b></div><div class="sensorStat"><span class="label">OK</span><b class="ok">${ok}</b></div><div class="sensorStat"><span class="label">Erreur</span><b class="bad">${err}</b></div><div class="sensorStat"><span class="label">Actifs</span><b>${active}</b></div></div><div class="toolbar"><button onclick="newSensor()">Ajouter capteur</button><button onclick="detectDs()">Scanner OneWire</button><button onclick="saveSensorsUi()">Sauvegarder</button><button onclick="editor('sensors')">Mode JSON</button></div><div class="sensorPage"><div class="sensorTableWrap"><table class="sensorTable"><thead><tr><th>Nom</th><th>Type</th><th>Role</th><th>GPIO / bus</th><th>Valeur</th><th>Unite</th><th>Etat</th><th>Derniere lecture</th><th>Critique</th><th>Actions</th></tr></thead><tbody>${arr.map(sensorRow).join('')}</tbody></table></div><aside class="formPanel" id="sensorForm">${sensorForm()}</aside></div><div id="ds"></div>`}
function sensorForm(i=-1){let s=i>=0?(cache.sensors.sensors||[])[i]:{id:'',name:'',type:'DS18B20',source:'local',enabled:true,gpio:4,role:''};return `<h3>${i>=0?'Modifier':'Nouveau capteur'}</h3><input id="sensorIndex" type="hidden" value="${i}"><label>Nom</label><input id="sensorName" value="${esc(s.name)}"><label>ID</label><input id="sensorId" value="${esc(s.id)}"><label>Type</label><select id="sensorType">${sensorTemplates.map(t=>`<option ${s.type===t?'selected':''}>${t}</option>`).join('')}</select><div class="formGrid"><div><label>Source</label><select id="sensorSource"><option ${s.source!=='espnow'?'selected':''}>local</option><option ${s.source==='espnow'?'selected':''}>espnow</option></select></div><div><label>Active</label><select id="sensorEnabled"><option value="true" ${s.enabled!==false?'selected':''}>oui</option><option value="false" ${s.enabled===false?'selected':''}>non</option></select></div></div><div class="formGrid"><div><label>GPIO</label><input id="sensorGpio" type="number" value="${s.gpio??''}"></div><div><label>RX</label><input id="sensorRx" type="number" value="${s.rx??''}"></div></div><div class="formGrid"><div><label>TX</label><input id="sensorTx" type="number" value="${s.tx??''}"></div><div><label>Adresse MAC</label><input id="sensorMac" value="${esc(s.mac)}"></div></div><label>Adresse DS18B20</label><input id="sensorAddress" value="${esc(s.address)}"><label>Role</label><input id="sensorRole" value="${esc(s.role)}"><div class="toolbar"><button onclick="applySensorForm()">Appliquer</button><button onclick="newSensor()">Vider</button></div>`}
function readSensorForm(){let n=v=>q(v).value;let s={id:n('#sensorId'),name:n('#sensorName'),type:n('#sensorType'),source:n('#sensorSource'),enabled:n('#sensorEnabled')==='true'};let gpio=n('#sensorGpio'),rx=n('#sensorRx'),tx=n('#sensorTx'),mac=n('#sensorMac'),addr=n('#sensorAddress'),role=n('#sensorRole');if(gpio!=='')s.gpio=Number(gpio);if(rx!=='')s.rx=Number(rx);if(tx!=='')s.tx=Number(tx);if(mac)s.mac=mac;if(addr)s.address=addr;if(role)s.role=role;return s}
function editSensor(i){q('#sensorForm').innerHTML=sensorForm(i)}
function newSensor(){q('#sensorForm').innerHTML=sensorForm(-1)}
function applySensorForm(){let i=Number(q('#sensorIndex').value);let s=readSensorForm();if(!s.id){alert('ID capteur obligatoire');return}if(i>=0)cache.sensors.sensors[i]=s;else cache.sensors.sensors.push(s);renderSensorsPage(false)}
function toggleSensor(i){cache.sensors.sensors[i].enabled=cache.sensors.sensors[i].enabled===false;renderSensorsPage(false)}
function copySensor(i){let s=JSON.parse(JSON.stringify(cache.sensors.sensors[i]));s.id=s.id+'_copy';s.name=s.name+' copie';cache.sensors.sensors.push(s);renderSensorsPage(false)}
function deleteSensor(i){if(confirm('Supprimer ce capteur ?')){cache.sensors.sensors.splice(i,1);renderSensorsPage(false)}}
async function saveSensorsUi(){let r=await fetch('/api/sensors',{method:'POST',body:JSON.stringify(cache.sensors)});alert(r.ok?'Capteurs sauvegardes':'Sauvegarde refusee: '+await r.text())}
function actuatorLive(a){if(a.id==='ssr1_water_heater')return `${fmt(status.ssr1PowerPct)} %`;if(a.id==='ssr2_aux')return `${fmt(status.ssr2PowerPct)} %`;if(a.id==='robotdyn_triac')return `${fmt(status.robotDynPowerPct)} %`;return a.enabled!==false?'pret':'off'}
function actuatorPins(a){let p=[];if(a.gpio!==undefined)p.push('GPIO '+a.gpio);if(a.zeroCross!==undefined)p.push('ZC '+a.zeroCross);if(a.control!==undefined)p.push('CTRL '+a.control);if(a.mac)p.push(a.mac);return p.length?p.map(x=>`<span class="sensorBadge">${esc(x)}</span>`).join(' '):'<span class="small">-</span>'}
function actuatorLocked(a){return status.safetyTripped&&a.critical}
function actuatorModeWarning(a){if(a.type==='SSR'&&a.mode==='PHASE_ANGLE')return 'Mode incompatible SSR';if(a.type==='Relais'&&(a.mode==='LOW_FREQ_PWM'||a.mode==='PHASE_ANGLE'))return 'Mode incompatible relais';return ''}
function actuatorRow(a,i){let online=a.enabled!==false,locked=actuatorLocked(a),warn=actuatorModeWarning(a);return `<tr class="${online?'':'off'}"><td><div class="sensorName">${esc(a.name||a.id)}</div><div class="sensorId">${esc(a.id)}</div>${warn?`<div class="small warn">${warn}</div>`:''}</td><td><span class="sensorBadge">${esc(a.type||'-')}</span></td><td>${actuatorPins(a)}</td><td><span class="sensorBadge">${esc(a.mode||'-')}</span></td><td><span class="sensorValue">${esc(actuatorLive(a))}</span></td><td>${Number((actuatorLive(a).match(/[-\\d.]+/)||[0])[0])>0?'ON':'OFF'}</td><td>${a.maxPowerW!==undefined?`${a.maxPowerW} W`:'-'}</td><td>${a.cycleMs??'-'}</td><td>${locked?pill('Verrouille',false,false):(a.critical?pill('Critique',true,false):pill('Standard',true,false))}</td><td class="small">timeout</td><td><div class="sensorActions"><button onclick="editActuator(${i})">Modifier</button><button onclick="toggleActuator(${i})">${online?'Off':'On'}</button><button onclick="manualActuatorTest('${esc(a.id)}')">Test</button><button onclick="forceActuatorOff('${esc(a.id)}')">OFF</button><button onclick="deleteActuator(${i})">Suppr.</button></div></td></tr>`}
async function renderActuatorsPage(load=true){if(load||!cache.actuators)cache.actuators=await api('/api/actuators');let arr=cache.actuators.actuators||[];let active=arr.filter(a=>a.enabled!==false).length;let locked=arr.filter(actuatorLocked).length;let power=Math.round((Number(status.ssr1PowerPct)||0)+(Number(status.ssr2PowerPct)||0)+(Number(status.robotDynPowerPct)||0));q('#view').innerHTML=`<h2>Actionneurs</h2>${safetyBanner()}<div class="actuatorTop"><div class="actuatorStat"><span class="label">Total</span><b>${arr.length}</b></div><div class="actuatorStat"><span class="label">Actifs</span><b class="ok">${active}</b></div><div class="actuatorStat"><span class="label">Verrouilles</span><b class="bad">${locked}</b></div><div class="actuatorStat"><span class="label">Commande</span><b class="sun">${power}%</b></div></div><div class="toolbar"><button onclick="newActuator()">Ajouter actionneur</button><button onclick="saveActuatorsUi()">Sauvegarder</button><button onclick="editor('actuators')">Mode JSON</button></div><div class="actuatorPage"><div class="actuatorTableWrap"><table class="actuatorTable"><thead><tr><th>Nom</th><th>Type</th><th>GPIO</th><th>Mode</th><th>Commande %</th><th>Etat sortie</th><th>Puissance max</th><th>Cycle ms</th><th>Securite</th><th>Derniere commande</th><th>Actions</th></tr></thead><tbody>${arr.map(actuatorRow).join('')}</tbody></table></div><aside class="formPanel" id="actuatorForm">${actuatorForm()}</aside></div>`}
function actuatorForm(i=-1){let a=i>=0?(cache.actuators.actuators||[])[i]:{id:'',name:'',type:'SSR',source:'local',enabled:true,gpio:5,mode:'BURST_FIRE',maxPowerW:1000,critical:false};let modes=[...actuatorModes];if(a.mode&&!modes.includes(a.mode))modes.push(a.mode);return `<h3>${i>=0?'Modifier':'Nouvel actionneur'}</h3><input id="actuatorIndex" type="hidden" value="${i}"><label>Nom</label><input id="actuatorName" value="${esc(a.name)}"><label>ID</label><input id="actuatorId" value="${esc(a.id)}"><label>Type</label><select id="actuatorType">${actuatorTemplates.map(t=>`<option ${a.type===t?'selected':''}>${t}</option>`).join('')}</select><div class="formGrid"><div><label>Source</label><select id="actuatorSource"><option ${a.source!=='espnow'?'selected':''}>local</option><option ${a.source==='espnow'?'selected':''}>espnow</option></select></div><div><label>Active</label><select id="actuatorEnabled"><option value="true" ${a.enabled!==false?'selected':''}>oui</option><option value="false" ${a.enabled===false?'selected':''}>non</option></select></div></div><div class="formGrid"><div><label>GPIO</label><input id="actuatorGpio" type="number" value="${a.gpio??''}"></div><div><label>Zero-cross</label><input id="actuatorZc" type="number" value="${a.zeroCross??''}"></div></div><div class="formGrid"><div><label>Control</label><input id="actuatorControl" type="number" value="${a.control??''}"></div><div><label>Puissance max W</label><input id="actuatorMax" type="number" value="${a.maxPowerW??''}"></div></div><label>Mode</label><select id="actuatorMode">${modes.map(m=>`<option ${a.mode===m?'selected':''}>${m}</option>`).join('')}</select><label>Adresse MAC ESP-NOW</label><input id="actuatorMac" value="${esc(a.mac)}"><label>Critique</label><select id="actuatorCritical"><option value="true" ${a.critical?'selected':''}>oui</option><option value="false" ${!a.critical?'selected':''}>non</option></select><div class="toolbar"><button onclick="applyActuatorForm()">Appliquer</button><button onclick="newActuator()">Vider</button></div>`}
function readActuatorForm(){let n=v=>q(v).value;let a={id:n('#actuatorId'),name:n('#actuatorName'),type:n('#actuatorType'),source:n('#actuatorSource'),enabled:n('#actuatorEnabled')==='true',critical:n('#actuatorCritical')==='true'};let gpio=n('#actuatorGpio'),zc=n('#actuatorZc'),ctrl=n('#actuatorControl'),max=n('#actuatorMax'),mode=n('#actuatorMode'),mac=n('#actuatorMac');if(gpio!=='')a.gpio=Number(gpio);if(zc!=='')a.zeroCross=Number(zc);if(ctrl!=='')a.control=Number(ctrl);if(max!=='')a.maxPowerW=Number(max);if(mode)a.mode=mode;if(mac)a.mac=mac;return a}
function editActuator(i){q('#actuatorForm').innerHTML=actuatorForm(i)}
function newActuator(){q('#actuatorForm').innerHTML=actuatorForm(-1)}
function applyActuatorForm(){let i=Number(q('#actuatorIndex').value);let a=readActuatorForm();if(!a.id){alert('ID actionneur obligatoire');return}if(i>=0)cache.actuators.actuators[i]=a;else cache.actuators.actuators.push(a);renderActuatorsPage(false)}
function toggleActuator(i){cache.actuators.actuators[i].enabled=cache.actuators.actuators[i].enabled===false;renderActuatorsPage(false)}
function copyActuator(i){let a=JSON.parse(JSON.stringify(cache.actuators.actuators[i]));a.id=a.id+'_copy';a.name=a.name+' copie';cache.actuators.actuators.push(a);renderActuatorsPage(false)}
function deleteActuator(i){if(confirm('Supprimer cet actionneur ?')){cache.actuators.actuators.splice(i,1);renderActuatorsPage(false)}}
async function saveActuatorsUi(){await fetch('/api/actuators',{method:'POST',body:JSON.stringify(cache.actuators)});alert('Actionneurs sauvegardes')}
async function manualActuatorTest(id){if(status.safetyTripped){alert('Test refuse: Safety CRITICAL');return}let pct=Number(prompt('Pourcentage de test limite par timeout commande', '10')||0);if(!confirm(`Tester ${id} a ${pct}% ?`))return;let r=await api('/api/actuator/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:id,command:'setPower',value:String(pct)})});alert(r.ok?'Test envoye':'Test refuse')}
async function forceActuatorOff(id){if(!confirm(`Forcer ${id} OFF ?`))return;await api('/api/actuator/command',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:id,command:'stop',value:'0'})});alert('Commande OFF envoyee')}
const operators=['>','<','>=','<=','==','!='];
// Catalogue UI des sources et mesures. Le serveur garde la validation finale.
const ruleSources=[
{id:'JSY-MK-194T',label:'JSY-MK-194T',measures:[['gridPowerW','number','W'],['injectionW','number','W'],['consumptionW','number','W'],['surplusW','number','W'],['voltageV','number','V'],['currentA','number','A'],['activePowerW','number','W'],['activePowerW1','number','W'],['activePowerW2','number','W'],['powerFactor','number',''],['frequencyHz','number','Hz'],['available','boolean','']]},
{id:'TIC Linky',label:'TIC Linky',measures:[['apparentPowerVA','number','VA'],['currentA','number','A'],['tariff','text',''],['available','boolean',''],['lastValidReadAgeMs','number','ms']]},
{id:'sonde1',label:'sonde1',measures:[['temperatureC','number','C'],['available','boolean',''],['lastValidReadAgeMs','number','ms']]},
{id:'sonde2',label:'sonde2',measures:[['temperatureC','number','C'],['available','boolean',''],['lastValidReadAgeMs','number','ms']]},
{id:'sonde3',label:'sonde3',measures:[['temperatureC','number','C'],['available','boolean',''],['lastValidReadAgeMs','number','ms']]},
{id:'Systeme',label:'Systeme',measures:[['simulationMode','boolean',''],['wifiStatus','enum','', ['CONNECTED','AP_FALLBACK','DISCONNECTED']],['uptimeMs','number','ms'],['freeHeap','number','B'],['role','enum','',['MASTER','BACKUP','NODE_SENSOR','NODE_ACTUATOR','NODE_MIXED']]]},
{id:'Securite',label:'Securite',measures:[['safetyLevel','enum','',['OK','WARNING','DEGRADED','CRITICAL']],['safetyReason','text',''],['isCritical','boolean','']]},
{id:'Redondance',label:'Redondance',measures:[['activeRole','enum','',['MASTER','BACKUP','NODE_SENSOR','NODE_ACTUATOR','NODE_MIXED']],['isActiveMaster','boolean',''],['activeMasterId','text',''],['epoch','number',''],['lastHeartbeatAgeMs','number','ms']]}
];
function sourceDef(id){return ruleSources.find(s=>s.id===id)||ruleSources[0]}
function sourceLabel(s){let ds=cache.sensors?.ds18b20||[];if(s.id==='sonde1')return `sonde1 - ${(ds[0]?.role)||'role non defini'}`;if(s.id==='sonde2')return `sonde2 - ${(ds[1]?.role)||'role non defini'}`;if(s.id==='sonde3')return `sonde3 - ${(ds[2]?.role)||'role non defini'}`;return s.label}
function measureDef(source,measure){let s=sourceDef(source);return s.measures.find(m=>m[0]===measure)||s.measures[0]}
function opsForType(t){return t==='number'?['>','>=','<','<=','==','!=']:['==','!=']}
function condText(c){return `<span class="ruleLine"><b>${esc(c.source||c.sensorId)}</b> ${esc(c.measure||c.variable)} ${esc(c.operator)} ${esc(c.value)} ${esc(c.unit||'')}</span>`}
function actionText(a){return `<span class="ruleLine"><b>${esc(a.actuatorId)}</b> ${esc(a.command)} ${a.value!==undefined?esc(a.value):''}</span>`}
function ruleRow(r,i){let enabled=r.enabled!==false;let conds=(r.conditions||[]).slice(0,3).map(condText).join('');let acts=(r.actions||[]).slice(0,3).map(actionText).join('');return `<tr class="${enabled?'':'off'}"><td>${pill(enabled?'Active':'Inactive',enabled,!enabled)}</td><td><div class="sensorName">${esc(r.name||r.id)}</div><div class="sensorId">${esc(r.id)}</div></td><td><span class="sensorBadge">${esc(r.logic||'AND')}</span></td><td><span class="sensorBadge">P${r.priority??0}</span></td><td><div class="ruleSummary">${conds||'<span class="small">Aucune condition</span>'}</div></td><td><div class="ruleSummary">${acts||'<span class="small">Aucune action</span>'}</div></td><td><div class="sensorActions"><button onclick="editRule(${i})">Modifier</button><button onclick="toggleRule(${i})">${enabled?'Off':'On'}</button><button onclick="copyRule(${i})">Copier</button><button onclick="deleteRule(${i})">Suppr.</button></div></td></tr>`}
async function renderLogicPage(load=true){if(load||!cache.rules)cache.rules=await api('/api/rules');if(!cache.actuators)cache.actuators=await api('/api/actuators');if(!cache.sensors)cache.sensors=await api('/api/sensors');let arr=cache.rules.rules||[];let active=arr.filter(r=>r.enabled!==false).length;let maxPriority=arr.reduce((m,r)=>Math.max(m,Number(r.priority)||0),0);let conds=arr.reduce((n,r)=>n+(r.conditions||[]).length,0);let acts=arr.reduce((n,r)=>n+(r.actions||[]).length,0);q('#view').innerHTML=`<h2>Logique</h2><div class="logicTop"><div class="logicStat"><span class="label">Regles</span><b>${arr.length}</b></div><div class="logicStat"><span class="label">Actives</span><b class="ok">${active}</b></div><div class="logicStat"><span class="label">Priorite max</span><b class="warn">${maxPriority}</b></div><div class="logicStat"><span class="label">Conditions</span><b>${conds}</b></div><div class="logicStat"><span class="label">Actions</span><b>${acts}</b></div></div><div class="toolbar"><button onclick="newRule()">Ajouter regle</button><button onclick="validateRules()">Valider regles</button><button onclick="saveRulesUi()">Sauvegarder</button><button onclick="editor('rules')">Mode JSON</button></div><div id="validation" class="invalidBox"></div><div class="logicPage"><div class="logicTableWrap"><table class="logicTable"><thead><tr><th>Etat</th><th>Regle</th><th>Logique</th><th>Priorite</th><th>SI</th><th>ALORS</th><th>Actions</th></tr></thead><tbody>${arr.map(ruleRow).join('')}</tbody></table></div><aside class="formPanel" id="ruleForm">${ruleForm()}</aside></div>`}
function ruleForm(i=-1){let r=i>=0?(cache.rules.rules||[])[i]:{id:'',name:'',enabled:true,priority:10,logic:'AND',conditions:[],actions:[]};return `<h3>${i>=0?'Modifier':'Nouvelle regle'}</h3><input id="ruleIndex" type="hidden" value="${i}"><label>Nom</label><input id="ruleName" value="${esc(r.name)}"><label>ID</label><input id="ruleId" value="${esc(r.id)}"><div class="formGrid"><div><label>Etat</label><select id="ruleEnabled"><option value="true" ${r.enabled!==false?'selected':''}>active</option><option value="false" ${r.enabled===false?'selected':''}>inactive</option></select></div><div><label>Priorite</label><input id="rulePriority" type="number" value="${r.priority??10}"></div></div><label>Logique conditions</label><select id="ruleLogic"><option ${r.logic!=='OR'?'selected':''}>AND</option><option ${r.logic==='OR'?'selected':''}>OR</option></select><h3>SI</h3><div class="ruleRows" id="condRows">${(r.conditions||[]).map((c,k)=>conditionFormRow(c,k)).join('')}</div><button class="compactBtn" onclick="addConditionRow()">Ajouter condition</button><h3>ALORS</h3><div class="ruleRows" id="actionRows">${(r.actions||[]).map((a,k)=>actionFormRow(a,k)).join('')}</div><button class="compactBtn" onclick="addActionRow()">Ajouter action</button><div class="toolbar"><button onclick="applyRuleForm()">Appliquer</button><button onclick="newRule()">Vider</button></div>`}
function valueControl(def,value){let type=def[1], enums=def[3]||[];if(type==='boolean')return `<select data-c="value"><option value="true" ${value===true||value==='true'?'selected':''}>true</option><option value="false" ${value===false||value==='false'?'selected':''}>false</option></select>`;if(type==='enum')return `<select data-c="value">${enums.map(v=>`<option ${value===v?'selected':''}>${v}</option>`).join('')}</select>`;if(type==='number')return `<input data-c="value" type="number" step="any" value="${esc(value??0)}">`;return `<input data-c="value" value="${esc(value??'')}">`}
function conditionFormRow(c={},k=0){let source=c.source||'JSY-MK-194T';let sd=sourceDef(source);let measure=c.measure||sd.measures[0][0];let md=measureDef(source,measure);let ops=opsForType(md[1]);let op=ops.includes(c.operator)?c.operator:ops[0];return `<div class="ruleRow"><select data-c="source" onchange="updateConditionRow(this)">${ruleSources.map(s=>`<option value="${s.id}" ${source===s.id?'selected':''}>${esc(sourceLabel(s))}</option>`).join('')}</select><select data-c="measure" onchange="updateConditionRow(this)">${sd.measures.map(m=>`<option ${measure===m[0]?'selected':''}>${m[0]}</option>`).join('')}</select><select data-c="operator">${ops.map(o=>`<option ${op===o?'selected':''}>${o}</option>`).join('')}</select><span class="valueHost">${valueControl(md,c.value)}</span><span class="unitCell" data-c="unit">${esc(md[2]||'')}</span><button onclick="this.parentElement.remove()">x</button><input data-c="type" type="hidden" value="${md[1]}"><input data-c="id" type="hidden" value="${esc(c.id||('cond_'+Date.now()))}"></div>`}
function updateConditionRow(el){let row=el.closest('.ruleRow');let source=row.querySelector('[data-c="source"]').value;let sd=sourceDef(source);let measureSel=row.querySelector('[data-c="measure"]');let current=measureSel.value;if(el.dataset.c==='source'||!sd.measures.some(m=>m[0]===current)){current=sd.measures[0][0];measureSel.innerHTML=sd.measures.map(m=>`<option ${current===m[0]?'selected':''}>${m[0]}</option>`).join('')}let md=measureDef(source,current);let ops=opsForType(md[1]);row.querySelector('[data-c="operator"]').innerHTML=ops.map(o=>`<option>${o}</option>`).join('');row.querySelector('.valueHost').innerHTML=valueControl(md,md[1]==='boolean'?true:(md[3]?.[0]??0));row.querySelector('[data-c="unit"]').textContent=md[2]||'';row.querySelector('[data-c="type"]').value=md[1]}
function actuatorOptions(selected){let arr=cache.actuators?.actuators||[];return arr.map(a=>`<option value="${esc(a.id)}" ${selected===a.id?'selected':''}>${esc(a.name||a.id)}</option>`).join('')}
function commandOptions(selected){let cmds=['setPower','setPowerWatts','setPowerFromSurplus','setMode','stop','on','off','toggle','safetyShutdown','setSafetyWarning','logEvent'];return cmds.map(c=>`<option ${selected===c?'selected':''}>${c}</option>`).join('')}
function actionFormRow(a={},k=0){let actuatorId=a.actuatorId||((cache.actuators?.actuators||[])[0]?.id||'');let command=a.command||'setPower';return `<div class="ruleActionRow"><select data-a="actuatorId">${actuatorOptions(actuatorId)}</select><select data-a="command" onchange="updateActionRow(this)">${commandOptions(command)}</select><span class="actionValueHost">${actionValueControl(a)}</span><button onclick="this.parentElement.remove()">x</button><div class="ruleSourceRow"><select data-a="sourceSensorId"><option value="">source optionnelle</option><option value="jsy_grid" ${a.sourceSensorId==='jsy_grid'?'selected':''}>JSY-MK-194T</option></select><select data-a="sourceVariable"><option value="">mesure source</option><option ${a.sourceVariable==='activePowerW1'?'selected':''}>activePowerW1</option><option ${a.sourceVariable==='activePowerW2'?'selected':''}>activePowerW2</option><option ${a.sourceVariable==='injectionW'?'selected':''}>injectionW</option><option ${a.sourceVariable==='surplusW'?'selected':''}>surplusW</option></select></div></div>`}
function actionValueControl(a={}){let command=a.command||'setPower';if(command==='setMode')return `<select data-a="mode">${actuatorModes.map(m=>`<option ${a.mode===m||a.value===m?'selected':''}>${m}</option>`).join('')}</select><input data-a="value" type="hidden" value="${esc(a.value??'')}">`;if(command==='logEvent'||command==='setSafetyWarning')return `<input data-a="message" value="${esc(a.message??'Evenement regle')}"><input data-a="value" type="hidden" value="0">`;if(command==='setPowerFromSurplus')return `<input data-a="maxHeaterPowerW" type="number" value="${esc(a.maxHeaterPowerW??1500)}" placeholder="max W"><input data-a="value" type="hidden" value="0"><span class="small">surplus proportionnel</span>`;if(command==='stop'||command==='off'||command==='on'||command==='toggle'||command==='safetyShutdown')return `<input data-a="value" type="hidden" value="${esc(a.value??0)}"><span class="small">auto</span>`;return `<input data-a="value" type="number" step="any" value="${esc(a.value??0)}" placeholder="${command==='setPowerWatts'?'W':'%'}">`}
function updateActionRow(el){let row=el.closest('.ruleActionRow');let a={command:row.querySelector('[data-a="command"]').value,value:row.querySelector('[data-a="value"]')?.value??0};row.querySelector('.actionValueHost').innerHTML=actionValueControl(a)}
function addConditionRow(){q('#condRows').insertAdjacentHTML('beforeend',conditionFormRow())}
function addActionRow(){q('#actionRows').insertAdjacentHTML('beforeend',actionFormRow())}
function readRuleForm(){let n=v=>q(v).value;let r={id:n('#ruleId'),name:n('#ruleName'),enabled:n('#ruleEnabled')==='true',priority:Number(n('#rulePriority')||0),logic:n('#ruleLogic'),conditions:[],actions:[]};document.querySelectorAll('#condRows .ruleRow').forEach(row=>{let c={};row.querySelectorAll('[data-c]').forEach(el=>c[el.dataset.c]=el.value);let md=measureDef(c.source,c.measure);c.type=md[1];c.unit=md[2]||'';if(c.type==='number')c.value=Number(c.value);if(c.type==='boolean')c.value=c.value==='true';if(c.source&&c.measure)r.conditions.push(c)});document.querySelectorAll('#actionRows .ruleActionRow').forEach(row=>{let a={};row.querySelectorAll('[data-a]').forEach(el=>a[el.dataset.a]=el.value);if(a.value!=='')a.value=Number(a.value);else delete a.value;if(!a.sourceSensorId)delete a.sourceSensorId;if(!a.sourceVariable)delete a.sourceVariable;if(a.actuatorId&&a.command)r.actions.push(a)});return r}
function editRule(i){q('#ruleForm').innerHTML=ruleForm(i)}
function newRule(){q('#ruleForm').innerHTML=ruleForm(-1)}
function applyRuleForm(){let i=Number(q('#ruleIndex').value);let r=readRuleForm();if(!r.id){alert('ID regle obligatoire');return}if(i>=0)cache.rules.rules[i]=r;else cache.rules.rules.push(r);renderLogicPage(false)}
function toggleRule(i){cache.rules.rules[i].enabled=cache.rules.rules[i].enabled===false;renderLogicPage(false)}
function copyRule(i){let r=JSON.parse(JSON.stringify(cache.rules.rules[i]));r.id=r.id+'_copy';r.name=r.name+' copie';cache.rules.rules.push(r);renderLogicPage(false)}
function deleteRule(i){if(confirm('Supprimer cette regle ?')){cache.rules.rules.splice(i,1);renderLogicPage(false)}}
async function saveRulesUi(){let r=await fetch('/api/rules',{method:'POST',body:JSON.stringify(cache.rules)});if(!r.ok){let e=await r.json();q('#validation').textContent='Sauvegarde refusee: '+e.join(' | ');return}alert('Regles sauvegardees')}
async function editor(name){cache[name]=await api('/api/'+name);q('#view').innerHTML=`<h2>${name}</h2><div class="toolbar"><button onclick="save('${name}')">Sauvegarder</button><button onclick="location.reload()">Recharger</button></div><textarea id="json">${JSON.stringify(cache[name],null,2)}</textarea>`}
async function save(name){let r=await fetch('/api/'+name,{method:'POST',body:q('#json').value});if(!r.ok){alert('Sauvegarde refusee: '+await r.text());return}alert('Configuration sauvegardee')}
function yn(id,label,val){return `<div class="switchRow"><span>${label}</span><select id="${id}"><option value="true" ${val!==false?'selected':''}>oui</option><option value="false" ${val===false?'selected':''}>non</option></select></div>`}
async function renderSettingsPage(){cache.system=await api('/api/system');let s=cache.system,w=s.wifi||(s.wifi={ssid:s.wifiSsid||'',password:s.wifiPassword||'',keepFallbackApAlwaysOn:true}),ap=s.fallbackAp||(s.fallbackAp={ssid:s.fallbackApSsid||'',password:s.fallbackApPassword||'',ip:s.fallbackIp||'192.168.4.1'}),r=s.router||(s.router={}),sf=s.safety||(s.safety={});q('#view').innerHTML=`<h2>Parametres</h2>${safetyBanner()}<div class="toolbar"><button onclick="saveSettingsUi()">Sauvegarder</button><button onclick="wifiTest()">Tester WiFi</button><button onclick="restart()">Redemarrer ESP32</button><button onclick="editor('system')">Mode JSON</button></div><div class="small">IP ${status.localIp} - RSSI ${status.rssi} - mode ${status.networkMode}</div><div class="settingsGrid"><section class="settingsPanel"><h3>WiFi</h3><label>SSID maison</label><input id="setWifiSsid" value="${esc(w.ssid||s.wifiSsid||'')}"><label>Mot de passe maison</label><input id="setWifiPwd" type="password" placeholder="laisser vide pour conserver">${yn('setKeepAp','Garder AP local actif avec WiFi maison',w.keepFallbackApAlwaysOn)}<label>SSID AP local</label><input id="setApSsid" value="${esc(ap.ssid||s.fallbackApSsid||'')}"><label>Mot de passe AP local</label><input id="setApPwd" type="password" placeholder="laisser vide pour conserver"><label>IP AP local</label><input id="setApIp" value="${esc(ap.ip||s.fallbackIp||'192.168.4.1')}"></section><section class="settingsPanel"><h3>Routeur solaire</h3><div class="formGrid"><div><label>Seuil injection W</label><input id="setInj" type="number" value="${r.injectionThresholdW??-200}"></div><div><label>Hysteresis W</label><input id="setHyst" type="number" value="${r.hysteresisW??40}"></div><div><label>Temp max ballon C</label><input id="setTankMax" type="number" value="${r.tankMaxC??65}"></div><div><label>Temp securite C</label><input id="setTempSafe" type="number" value="${r.tempSafetyMaxC??r.tankSafetyC??70}"></div><div><label>Cycle SSR ms</label><input id="setSsrCycle" type="number" value="${r.ssrCycleMs??1000}"></div><div><label>Timeout commande ms</label><input id="setCmdTimeout" type="number" value="${r.commandTimeoutMs??5000}"></div></div></section><section class="settingsPanel"><h3>Puissance et PID</h3><div class="formGrid"><div><label>SSR1 max W</label><input id="setSsr1" type="number" value="${r.ssr1MaxW??1500}"></div><div><label>SSR2 max W</label><input id="setSsr2" type="number" value="${r.ssr2MaxW??1000}"></div><div><label>RobotDyn max W</label><input id="setRobot" type="number" value="${r.robotDynMaxW??1000}"></div><div><label>Kp</label><input id="setKp" type="number" step="any" value="${r.kp??0.08}"></div><div><label>Ki</label><input id="setKi" type="number" step="any" value="${r.ki??0.01}"></div><div><label>Kd</label><input id="setKd" type="number" step="any" value="${r.kd??0}"></div></div></section><section class="settingsPanel"><h3>Securites</h3>${yn('setSafetyEnabled','SafetyManager actif',sf.enabled)}${yn('setBlockDs','Couper si DS18B20 critique absente',sf.blockOnMissingDs18b20)}${yn('setBlockTop','Couper si sonde ballon haut absente',sf.blockOnMissingTopSensor)}${yn('setBlockJsy','Couper si JSY absent',sf.blockOnMissingJsy)}${yn('setBlockBoth','Couper si JSY et TIC absents',sf.blockOnMissingJsyAndTic)}${yn('setWarnOnly','Absences capteurs en alerte seulement',sf.warningOnlyOnMissingSensors)}<div class="formGrid"><div><label>Timeout JSY ms</label><input id="setJsyTimeout" type="number" value="${r.jsyTimeoutMs??3000}"></div><div><label>Timeout TIC ms</label><input id="setTicTimeout" type="number" value="${r.ticTimeoutMs??10000}"></div></div></section><section class="settingsPanel"><h3>Systeme</h3>${yn('setSimulation','Mode simulation',s.simulationMode)}${yn('setDebug','Debug',s.debug)}<label>Mode</label><select id="setMode"><option ${r.mode!=='MANUAL'?'selected':''}>AUTO</option><option ${r.mode==='MANUAL'?'selected':''}>MANUAL</option></select><label>Heartbeat ms</label><input id="setHeartbeat" type="number" value="${s.heartbeatIntervalMs??300}"><label>Takeover ms</label><input id="setTakeover" type="number" value="${s.takeoverTimeoutMs??1000}"></section></div>`}
async function saveSettingsUi(){let s=cache.system||await api('/api/system');s.wifi=s.wifi||{};s.fallbackAp=s.fallbackAp||{};s.router=s.router||{};s.safety=s.safety||{};let n=id=>q(id).value,b=id=>q(id).value==='true';s.wifi.ssid=n('#setWifiSsid');s.wifi.keepFallbackApAlwaysOn=b('#setKeepAp');let wp=n('#setWifiPwd');if(wp)s.wifi.password=wp;s.wifiSsid=s.wifi.ssid;s.wifiPassword=s.wifi.password;s.fallbackAp.ssid=n('#setApSsid');let apw=n('#setApPwd');if(apw)s.fallbackAp.password=apw;s.fallbackAp.ip=n('#setApIp');s.fallbackApSsid=s.fallbackAp.ssid;s.fallbackApPassword=s.fallbackAp.password;s.fallbackIp=s.fallbackAp.ip;s.router.mode=n('#setMode');s.router.injectionThresholdW=Number(n('#setInj'));s.router.hysteresisW=Number(n('#setHyst'));s.router.tankMaxC=Number(n('#setTankMax'));s.router.tankSafetyC=Number(n('#setTempSafe'));s.router.tempSafetyMaxC=Number(n('#setTempSafe'));s.router.ssrCycleMs=Number(n('#setSsrCycle'));s.router.commandTimeoutMs=Number(n('#setCmdTimeout'));s.router.ssr1MaxW=Number(n('#setSsr1'));s.router.ssr2MaxW=Number(n('#setSsr2'));s.router.robotDynMaxW=Number(n('#setRobot'));s.router.kp=Number(n('#setKp'));s.router.ki=Number(n('#setKi'));s.router.kd=Number(n('#setKd'));s.router.jsyTimeoutMs=Number(n('#setJsyTimeout'));s.router.ticTimeoutMs=Number(n('#setTicTimeout'));s.safety.enabled=b('#setSafetyEnabled');s.safety.blockOnMissingDs18b20=b('#setBlockDs');s.safety.blockOnMissingTopSensor=b('#setBlockTop');s.safety.blockOnMissingJsy=b('#setBlockJsy');s.safety.blockOnMissingJsyAndTic=b('#setBlockBoth');s.safety.warningOnlyOnMissingSensors=b('#setWarnOnly');s.simulationMode=b('#setSimulation');s.debug=b('#setDebug');s.heartbeatIntervalMs=Number(n('#setHeartbeat'));s.takeoverTimeoutMs=Number(n('#setTakeover'));let r=await fetch('/api/system',{method:'POST',body:JSON.stringify(s)});alert(r.ok?'Parametres sauvegardes':'Sauvegarde refusee')}
async function addTemplate(kind){let name=kind==='sensors'?'Capteur virtuel':'Actionneur virtuel';let obj=kind==='sensors'?{id:'new_sensor',name,type:name,source:'local',enabled:true}:{id:'new_actuator',name,type:name,source:'local',enabled:true};cache[kind][kind].push(obj);q('#json').value=JSON.stringify(cache[kind],null,2)}
function eventRows(){let level=q('#eventFilter')?.value||'ALL';let events=status.events||[];if(level!=='ALL')events=events.filter(e=>e.level===level);return events.map(e=>`<tr><td>${fmt(e.timestampMs)} ms</td><td><span class="sensorBadge ${e.level==='CRITICAL'?'bad':e.level==='WARNING'?'warn':'ok'}">${esc(e.level)}</span></td><td>${esc(e.code)}</td><td>${esc(e.source)}</td><td>${esc(e.message)}</td></tr>`).join('')||'<tr><td colspan="5">Aucun evenement</td></tr>'}
async function exportLogs(){let data=await api('/api/logs/export');q('#diagExport').value=JSON.stringify(data,null,2)}
async function clearLogs(){if(!confirm('Effacer les evenements recents ?'))return;await fetch('/api/logs/clear',{method:'POST'});status=await api('/api/diagnostic');renderDiagnosticPage()}
function renderDiagnosticPage(){let ds=(status.ds18b20||[]).map(s=>`${s.id}: ${s.available?'OK':'absent'} ${s.available?fmt(s.temperatureC)+' C':''}`).join('<br>');let acts=`SSR1 ${fmt(status.ssr1PowerPct)}%<br>SSR2 ${fmt(status.ssr2PowerPct)}%<br>RobotDyn ${fmt(status.robotDynPowerPct)}%`;q('#view').innerHTML=`<h2>Diagnostic</h2>${safetyBanner()}<div class="toolbar"><select id="eventFilter" onchange="q('#eventBody').innerHTML=eventRows()"><option>ALL</option><option>INFO</option><option>WARNING</option><option>ERROR</option><option>CRITICAL</option></select><button onclick="exportLogs()">Exporter logs</button><button onclick="clearLogs()">Reset logs</button><button onclick="restart()">Redemarrer ESP32</button></div><div class="grid">${[
['Securite',status.safetyLevel||'OK',status.safetyReason||'-'],
['Uptime',fmt(status.uptime)+' s','Heap '+fmt(status.heapFree)+' o'],
['LittleFS',status.littleFsOk?'OK':'Erreur','stockage local'],
['WiFi',status.networkMode,status.localIp+' RSSI '+status.rssi],
['ESP-NOW',status.espNowReady?'pret':'off','communication locale'],
['Redondance',status.redundancyState,'role actif '+status.role],
['Master actif',status.activeMasterId||'-','epoch '+status.epoch],
['Heartbeat',status.lastMasterHeartbeatAgeMs===4294967295?'-':fmt(status.lastMasterHeartbeatAgeMs)+' ms','dernier master'],
['JSY-MK-194T',status.jsyOnline?'OK':'absent','grid '+fmt(status.gridPowerW)+' W'],
['TIC Linky',status.ticStatus,status.ticAvailable?'OK':'absente'],
['DS18B20',ds||'-',status.ds18b20CriticalMissing?'critique absente':''],
['Actionneurs',acts,status.safetyTripped?'verrouilles':'pilotables']
].map(x=>`<div class="card"><div class="label">${x[0]}</div><div class="value">${x[1]}</div><div class="small">${x[2]||''}</div></div>`).join('')}</div><h3>Evenements systeme</h3><table><thead><tr><th>Temps</th><th>Niveau</th><th>Code</th><th>Source</th><th>Message</th></tr></thead><tbody id="eventBody">${eventRows()}</tbody></table><textarea id="diagExport" placeholder="Export JSON des logs"></textarea>`}
async function renderInstallPage(){let d=await api('/api/device'),s=await api('/api/system');let r=s.router||(s.router={}),w=s.wifi||(s.wifi={keepFallbackApAlwaysOn:true});q('#view').innerHTML=`<h2>Installation</h2>${safetyBanner()}<div class="toolbar"><button onclick="saveInstall()">Sauvegarder</button><button onclick="wifiTestInstall()">Tester WiFi</button><button onclick="detectDsInstall()">Scanner DS18B20</button><button onclick="restart()">Redemarrer</button></div><div class="settingsGrid"><section class="settingsPanel"><h3>Module</h3><label>Nom appareil</label><input id="n" value="${esc(d.name||d.deviceName||'')}"><label>Role</label><select id="r">${['MASTER','BACKUP','NODE_SENSOR','NODE_ACTUATOR','NODE_MIXED'].map(x=>`<option ${x===d.role?'selected':''}>${x}</option>`).join('')}</select><div class="small">MAC ${status.deviceId}</div><label>Mode simulation</label><select id="instSimulation"><option value="false" ${!s.simulationMode?'selected':''}>non</option><option value="true" ${s.simulationMode?'selected':''}>oui</option></select></section><section class="settingsPanel"><h3>WiFi et ESP-NOW</h3><label>SSID WiFi</label><input id="ssid" value="${esc(w.ssid||s.wifiSsid||'')}"><label>Mot de passe WiFi</label><input id="pwd" type="password" placeholder="laisser vide pour conserver"><label>AP local toujours actif</label><select id="instKeepAp"><option value="true" ${w.keepFallbackApAlwaysOn!==false?'selected':''}>oui</option><option value="false" ${w.keepFallbackApAlwaysOn===false?'selected':''}>non</option></select><label>ESP-NOW actif</label><select id="instEspNow"><option value="true" ${s.espNowEnabled!==false?'selected':''}>oui</option><option value="false" ${s.espNowEnabled===false?'selected':''}>non</option></select><label>Peer ESP-NOW</label><input id="peer" placeholder="AA:BB:CC:DD:EE:FF"><button onclick="addPeer()">Ajouter peer ESP-NOW</button></section><section class="settingsPanel"><h3>Routeur solaire</h3><div class="formGrid"><div><label>Puissance chauffe-eau W</label><input id="instHeater" type="number" value="${r.ssr1MaxW??1500}"></div><div><label>Seuil demarrage injection W</label><input id="instStart" type="number" value="${r.minInjectionStartW??r.injectionThresholdW??200}"></div><div><label>Seuil arret injection W</label><input id="instStop" type="number" value="${r.stopBelowInjectionW??50}"></div><div><label>Temp max ballon C</label><input id="instTank" type="number" value="${r.tankMaxC??65}"></div><div><label>Temp securite ballon C</label><input id="instSafe" type="number" value="${r.tempSafetyMaxC??r.tankSafetyC??70}"></div></div></section><section class="settingsPanel"><h3>DS18B20</h3><div id="instDs" class="small">Scanner pour afficher les adresses detectees.</div></section></div>`}
async function renderReadmePage(){let txt=await fetch('/README.md').then(r=>r.text()).catch(e=>'README indisponible');q('#view').innerHTML=`<h2>README</h2><div class="toolbar"><button onclick="renderReadmePage()">Recharger</button></div><pre class="readmeBox">${esc(txt)}</pre>`}
async function render(){let v=q('#view'); if(current==='dashboard'){if(!cache.sensors)cache.sensors=await api('/api/sensors');v.innerHTML=`<h2>Dashboard</h2>${renderDashboard()}`}
if(current==='diag'){renderDiagnosticPage()}
if(current==='sensors'){await renderSensorsPage()}
if(current==='actuators'){await renderActuatorsPage()}
if(current==='logic'){await renderLogicPage()}
if(current==='settings'){await renderSettingsPage()}
if(current==='install'){await renderInstallPage()}
if(current==='readme'){await renderReadmePage()}}
async function detectDs(){q('#ds').textContent=JSON.stringify(await api('/api/ds18b20'))}
async function validateRules(){let errors=await api('/api/rules/validate');q('#validation').textContent=errors.length?('Erreurs: '+errors.join(' | ')):'Regles valides'}
async function wifiTest(){let ssid=q('#setWifiSsid')?.value,pwd=q('#setWifiPwd')?.value;if(!ssid){let s=JSON.parse(q('#json').value);ssid=s.wifi?.ssid||s.wifiSsid;pwd=s.wifi?.password||s.wifiPassword}alert((await api('/api/wifi/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:ssid,password:pwd})})).ok?'Connexion OK':'Echec connexion')}
async function wifiTestInstall(){alert((await api('/api/wifi/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:q('#ssid').value,password:q('#pwd').value})})).ok?'Connexion OK':'Echec connexion')}
async function detectDsInstall(){q('#instDs').textContent=JSON.stringify(await api('/api/ds18b20'))}
async function addPeer(){await api('/api/espnow/peer',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({mac:q('#peer').value})});alert('Peer ajoute')}
async function saveInstall(){let d=await api('/api/device'),s=await api('/api/system');s.wifi=s.wifi||{};s.router=s.router||{};d.name=q('#n').value;d.deviceName=q('#n').value;d.role=q('#r').value;d.isConfigured=true;s.wifi.ssid=q('#ssid').value;s.wifi.keepFallbackApAlwaysOn=q('#instKeepAp').value==='true';s.wifiSsid=s.wifi.ssid;let pwd=q('#pwd').value;if(pwd){s.wifi.password=pwd;s.wifiPassword=pwd}s.espNowEnabled=q('#instEspNow').value==='true';s.simulationMode=q('#instSimulation').value==='true';s.router.ssr1MaxW=Number(q('#instHeater').value);s.router.minInjectionStartW=Number(q('#instStart').value);s.router.stopBelowInjectionW=Number(q('#instStop').value);s.router.tankMaxC=Number(q('#instTank').value);s.router.tempSafetyMaxC=Number(q('#instSafe').value);s.router.tankSafetyC=s.router.tempSafetyMaxC;let rd=await fetch('/api/device',{method:'POST',body:JSON.stringify(d)});let rs=await fetch('/api/system',{method:'POST',body:JSON.stringify(s)});alert(rd.ok&&rs.ok?'Installation sauvegardee':'Sauvegarde refusee')}
async function manualStop(){if(!confirm('Confirmer arret urgence manuel ?'))return;await fetch('/api/safety/manual-stop',{method:'POST'});alert('Arret urgence active')}
async function restart(){if(!confirm('Redemarrer ESP32 maintenant ?'))return;await fetch('/api/system/reboot',{method:'POST'});alert('Redemarrage demande')}
async function startSimulation(){await fetch('/api/simulation/start',{method:'POST'});await applySimulation();await refresh();render()}
async function stopSimulation(){if(!confirm('Repasser en reel ? Les sorties seront forcees OFF pendant 2 secondes.'))return;await fetch('/api/simulation/stop',{method:'POST'});await refresh();render()}
async function setSimulationMode(){await fetch('/api/simulation/mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({mode:q('#simMode').value})});await refresh();render()}
async function applyScenario(){await fetch('/api/simulation/scenario',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({scenario:q('#simScenario').value})});await refresh();render()}
async function randomSimulation(){await fetch('/api/simulation/randomize',{method:'POST'});await refresh();render()}
async function applySimulation(){let grid=Number(q('#simGrid').value),available=q('#simAvailable').value==='true';let payload={jsy:{available:available,gridPowerW:grid,voltageV:Number(q('#simVoltage').value),currentA:Number(q('#simCurrent').value),powerFactor:Number(q('#simPf').value),frequencyHz:Number(q('#simFreq').value)},tic:{available:true,apparentPowerVA:Number(q('#simTicVa').value),currentA:Number(q('#simCurrent').value),tariff:'BASE'},ds18b20:[{id:'sonde1',available:true,temperatureC:Number(q('#simTop').value)},{id:'sonde2',available:true,temperatureC:Number(q('#simMiddle').value)},{id:'sonde3',available:true,temperatureC:Number(q('#simBottom').value)}]};let r=await fetch('/api/simulation/values',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});if(!r.ok)alert('Valeurs simulation refusees');else await refresh()}
setInterval(refresh,4000);refresh().then(render);
</script></body></html>
)HTML";
}
