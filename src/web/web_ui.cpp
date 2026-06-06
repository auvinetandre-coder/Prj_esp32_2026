#include "web_ui.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_arduino_version.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <time.h>
#include "../build_info.h"

static const char *ROUTEUR_ACTIVE_FIRMWARE_MARKER = ROUTEUR_FIRMWARE_MARKER;
static const char *GITHUB_VERSION_URL = "https://github.com/auvinetandre-coder/Prj_esp32_2026/releases/latest/download/version.json";
static const char *GITHUB_FIRMWARE_URL = "https://github.com/auvinetandre-coder/Prj_esp32_2026/releases/latest/download/firmware.bin";
static const char *GITHUB_LITTLEFS_URL = "https://github.com/auvinetandre-coder/Prj_esp32_2026/releases/latest/download/littlefs.bin";
static const char *GITHUB_RELEASE_URL = "https://github.com/auvinetandre-coder/Prj_esp32_2026/releases/latest";

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

static const char *partitionSubtypeLabel(uint8_t subtype) {
  switch (subtype) {
    case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "factory";
    case ESP_PARTITION_SUBTYPE_APP_OTA_0: return "app0";
    case ESP_PARTITION_SUBTYPE_APP_OTA_1: return "app1";
    default: return "app";
  }
}

static String findRouteurVersionInPartition(const esp_partition_t *part) {
  if (!part) return "N/A";
  const char *prefix = "RS32_VERSION:";
  const size_t prefixLen = strlen(prefix);
  const size_t chunkSize = 512;
  const size_t overlap = 40;
  uint8_t buffer[chunkSize + overlap + 1];
  size_t carry = 0;

  for (size_t offset = 0; offset < part->size; offset += chunkSize) {
    const size_t toRead = min(chunkSize, static_cast<size_t>(part->size - offset));
    if (esp_partition_read(part, offset, buffer + carry, toRead) != ESP_OK) return "N/A";
    const size_t total = carry + toRead;
    buffer[total] = 0;

    for (size_t i = 0; i + prefixLen < total; i++) {
      if (memcmp(buffer + i, prefix, prefixLen) != 0) continue;
      String version;
      for (size_t j = i + prefixLen; j < total && version.length() < 24; j++) {
        const char c = static_cast<char>(buffer[j]);
        if (c == ';') return version.length() ? version : "N/A";
        if ((c >= '0' && c <= '9') || c == '-') version += c;
        else break;
      }
    }

    carry = min(overlap, total);
    memmove(buffer, buffer + total - carry, carry);
  }

  return "N/A";
}

static void appendOtaPartitionJson(JsonObject dst, const esp_partition_t *part, const esp_partition_t *running, const esp_partition_t *boot) {
  if (!part) {
    dst["present"] = false;
    dst["valid"] = false;
    dst["label"] = "N/A";
    dst["version"] = "N/A";
    return;
  }

  dst["present"] = true;
  dst["label"] = part->label;
  dst["type"] = part->type;
  dst["subtype"] = part->subtype;
  dst["slot"] = partitionSubtypeLabel(part->subtype);
  dst["address"] = part->address;
  dst["size"] = part->size;
  dst["running"] = running && part->address == running->address;
  dst["boot"] = boot && part->address == boot->address;
  dst["routeurVersion"] = findRouteurVersionInPartition(part);

  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(part, &desc) == ESP_OK) {
    dst["valid"] = true;
    dst["version"] = desc.version[0] ? desc.version : "N/A";
    dst["projectName"] = desc.project_name[0] ? desc.project_name : "N/A";
    dst["compileDate"] = desc.date[0] ? desc.date : "N/A";
    dst["compileTime"] = desc.time[0] ? desc.time : "N/A";
    if (running && part->address == running->address && String(dst["routeurVersion"].as<const char *>()) == "N/A") {
      dst["routeurVersion"] = ROUTEUR_FIRMWARE_VERSION;
      dst["routeurBuildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
    }
  } else {
    dst["valid"] = false;
    dst["version"] = "N/A";
    dst["projectName"] = "N/A";
    dst["compileDate"] = "N/A";
    dst["compileTime"] = "N/A";
  }
}

static String readLittleFsTextFile(const char *path) {
  if (!LittleFS.exists(path)) return "N/A";
  File file = LittleFS.open(path, "r");
  if (!file) return "N/A";
  String value = file.readString();
  file.close();
  value.trim();
  return value.length() ? value : "N/A";
}

void WebUi::begin() {
  routes();
  server.begin();
  state.addLog("Web UI started");
}

void WebUi::loop() {
  server.handleClient();
}

bool WebUi::authEnabled() {
  JsonObject webAuth = config.system()["webAuth"].as<JsonObject>();
  if (webAuth.isNull()) return true;
  return webAuth["enabled"] | true;
}

bool WebUi::isAuthenticated() {
  if (!authEnabled()) return true;
  JsonObject webAuth = config.system()["webAuth"].as<JsonObject>();
  String username = webAuth["username"] | "admin";
  String password = webAuth["password"] | "routeur1234";
  if (!username.length()) username = "admin";
  if (!password.length()) password = "routeur1234";
  return server.authenticate(username.c_str(), password.c_str());
}

bool WebUi::requireAuth() {
  if (isAuthenticated()) return true;
  server.requestAuthentication(BASIC_AUTH, "RouteurSolaireESP32", "Connexion requise");
  return false;
}

void WebUi::routes() {
  server.on("/", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "text/html; charset=utf-8", homePage());
  });
  server.on("/lite", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "text/html; charset=utf-8", litePage());
  });
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
    if (!requireAuth()) return;
    server.sendHeader("Location", "/app", true);
    server.send(302, "text/plain", "Redirection vers /app");
  });
  server.on("/ota/firmware", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = !Update.hasError();
    server.send(200, "text/html; charset=utf-8", ok ? F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>OTA firmware</title></head><body><h1>Firmware OTA OK</h1><p>Redemarrage...</p><script>setTimeout(()=>location.href='/',8000)</script></body></html>") : F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>OTA firmware</title></head><body><h1>Erreur OTA firmware</h1><p>Verifie la partition OTA et le fichier .bin.</p><p><a href=\"/\">Retour secours</a></p></body></html>"));
    if (ok) {
      state.logEvent("WARNING", "OTA_FIRMWARE", "Firmware OTA applique, redemarrage", "WebUi");
      delay(250);
      ESP.restart();
    }
  }, [this]() {
    if (!isAuthenticated()) {
      Update.abort();
      return;
    }
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      state.logEvent("WARNING", "OTA_FIRMWARE", String("Upload firmware: ") + upload.filename, "WebUi");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (!Update.end(true)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
      state.logEvent("WARNING", "OTA_ABORTED", "Upload firmware annule", "WebUi");
    }
  });
  server.on("/ota/littlefs", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = littleFsOtaBackupOk && !Update.hasError();
    server.send(200, "text/html; charset=utf-8", ok ? F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>OTA LittleFS</title></head><body><h1>LittleFS OTA OK</h1><p>Redemarrage...</p><script>setTimeout(()=>location.href='/',8000)</script></body></html>") : F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>OTA LittleFS</title></head><body><h1>Erreur OTA LittleFS</h1><p>Verifie le fichier LittleFS .bin et la taille de partition.</p><p><a href=\"/\">Retour secours</a></p></body></html>"));
    if (ok) {
      state.logEvent("WARNING", "OTA_LITTLEFS", "LittleFS OTA applique, redemarrage", "WebUi");
      delay(250);
      ESP.restart();
    }
    littleFsOtaBackupOk = false;
  }, [this]() {
    if (!isAuthenticated()) {
      Update.abort();
      littleFsOtaBackupOk = false;
      return;
    }
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      state.logEvent("WARNING", "OTA_LITTLEFS", String("Upload LittleFS: ") + upload.filename, "WebUi");
      littleFsOtaBackupOk = config.backupToNvsBeforeLittleFsOta();
      if (!littleFsOtaBackupOk) {
        state.logEvent("ERROR", "CONFIG_BACKUP_FAILED", config.lastError().length() ? config.lastError() : "Sauvegarde config NVS impossible", "WebUi");
        Update.abort();
        return;
      }
      LittleFS.end();
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!littleFsOtaBackupOk) return;
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (!littleFsOtaBackupOk) return;
      if (!Update.end(true)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.abort();
      littleFsOtaBackupOk = false;
      state.logEvent("WARNING", "OTA_ABORTED", "Upload LittleFS annule", "WebUi");
    }
  });
  server.on("/api/ping", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    String out = "{\"ok\":true,\"networkMode\":\"" + state.networkMode + "\",\"localIp\":\"" + state.localIp + "\"}";
    server.send(200, "application/json", out);
  });
  server.on("/api/fs", HTTP_GET, [this]() { sendFsListJson(); });
  server.on("/fs", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "text/html; charset=utf-8", fsPage());
  });
  server.on("/api/status", HTTP_GET, [this]() {
    DynamicJsonDocument doc(6144);
    JsonObject out = doc.to<JsonObject>();
    state.toJson(out, false);
    JsonObject device = config.device();
    JsonObject system = config.system();
    out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["nodeId"] = state.nodeId;
    out["role"] = device["role"] | RuntimeState::roleToString(state.role);
    out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
    out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
    const unsigned long nowMs = millis();
    out["uptimeMs"] = nowMs;
    time_t nowEpoch = time(nullptr);
    out["currentEpochMs"] = nowEpoch > 1600000000 ? static_cast<uint64_t>(nowEpoch) * 1000ULL + (nowMs % 1000UL) : 0;
    out["simulationMode"] = state.simulationMode;
    out["simulationType"] = system["simulation"]["mode"] | state.simulationType.c_str();
    out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
    sendJson(doc);
  });
  server.on("/api/status-lite", HTTP_GET, [this]() { sendStatusLite(); });
  server.on("/api/state", HTTP_GET, [this]() { sendStatusLite(); });
  server.on("/api/actuators/runtime", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonObject out = doc.to<JsonObject>();
    out["ok"] = true;
    actuators.runtimeToJson(out["actuators"].to<JsonArray>());
    sendJson(doc);
  });
  server.on("/api/dashboard/config", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    DynamicJsonDocument doc(8192);
    JsonObject dashboard = config.system()["dashboard"].is<JsonObject>()
                             ? config.system()["dashboard"].as<JsonObject>()
                             : config.system()["dashboard"].to<JsonObject>();
    doc.set(dashboard);
    sendJson(doc);
  });
  server.on("/api/dashboard/config", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err || !doc.is<JsonObject>()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON dashboard invalide\"}");
      return;
    }
    JsonObject dashboard = config.system()["dashboard"].to<JsonObject>();
    dashboard.clear();
    dashboard.set(doc.as<JsonObject>());
    bool ok = config.saveSystem();
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sauvegarde dashboard impossible\"}");
  });
  server.on("/api/realtime", HTTP_GET, [this]() {
    DynamicJsonDocument doc(2048);
    JsonObject out = doc.to<JsonObject>();
    JsonObject router = config.system()["router"].as<JsonObject>();
    auto setNumber = [&out](const char *key, float value) {
      if (isnan(value) || isinf(value)) out[key] = nullptr;
      else out[key] = value;
    };
    out["t"] = millis();
    setNumber("gridPowerRawW", state.gridPowerRawW);
    setNumber("gridPowerFilteredW", state.gridPowerFilteredW);
    setNumber("gridPowerW", state.gridPowerW);
    setNumber("productionW", state.productionW);
    setNumber("injectionW", state.injectionW);
    setNumber("consumptionW", state.consumptionW);
    setNumber("surplusW", state.surplusW);
    setNumber("targetW", router["gridSetpointW"] | 0.0f);
    setNumber("deadbandW", router["deadbandW"] | 30.0f);
    setNumber("pidErrorW", state.pidErrorW);
    setNumber("pidOutputPercent", state.pidOutputPercent);
    setNumber("commandPercent", state.commandPercent);
    setNumber("heaterPowerW", state.heaterPowerW);
    setNumber("ssr1PowerPct", state.ssr1PowerPct);
    setNumber("ssr2PowerPct", state.ssr2PowerPct);
    setNumber("robotDynPowerPct", state.robotDynPowerPct);
    out["ssr1OutputOn"] = state.ssr1OutputOn;
    out["ssr2OutputOn"] = state.ssr2OutputOn;
    out["robotDynOutputOn"] = state.robotDynOutputOn;
    out["ssr1PinHigh"] = state.ssr1PinHigh;
    out["ssr2PinHigh"] = state.ssr2PinHigh;
    out["robotDynPinHigh"] = state.robotDynPinHigh;
    out["ssr1"] = state.ssr1PowerPct > 0.5f;
    out["ssr2"] = state.ssr2PowerPct > 0.5f;
    setNumber("temp1", isnan(state.ds18b20Temps[0]) || isinf(state.ds18b20Temps[0]) ? state.tankTopC : state.ds18b20Temps[0]);
    setNumber("temp2", isnan(state.ds18b20Temps[1]) || isinf(state.ds18b20Temps[1]) ? state.tankMiddleC : state.ds18b20Temps[1]);
    setNumber("temp3", isnan(state.ds18b20Temps[2]) || isinf(state.ds18b20Temps[2]) ? state.tankBottomC : state.ds18b20Temps[2]);
    setNumber("tempSafety", router["tempSafetyMaxC"] | router["tankSafetyC"] | 70.0f);
    out["pidStatus"] = state.pidStatus;
    out["safetyLevel"] = state.safetyLevel;
    sendJson(doc);
  });
  server.on("/api/diagnostic", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonObject out = doc.to<JsonObject>();
    state.toJson(out, true);
    JsonObject device = config.device();
    JsonObject system = config.system();
    out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["role"] = device["role"] | RuntimeState::roleToString(state.role);
    out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
    out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
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
    if (!requireAuth()) return;
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
    if (!requireAuth()) return;
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
    if (!requireAuth()) return;
    safety.triggerManualStop();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/diagnostic/identify", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    statusLed.identify(millis(), 120000UL);
    state.logEvent("INFO", "IDENTIFY", "Clignotement LEDs diagnostic pendant 120 s", "WebUi");
    server.send(200, "application/json", "{\"ok\":true,\"durationMs\":120000}");
  });
  server.on("/api/logs/export", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonArray events = doc["events"].to<JsonArray>();
    state.eventsToJson(events);
    sendJson(doc);
  });
  server.on("/api/logs/clear", HTTP_POST, [this]() {
    if (!requireAuth()) return;
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
    if (!requireAuth()) return;
    simulation.enable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/stop", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    actuators.forceAllOff();
    simulation.disable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/enable", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    simulation.enable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/disable", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    actuators.forceAllOff();
    simulation.disable();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/mode", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    simulation.setMode(server.arg("mode").length() ? server.arg("mode") : server.arg("plain"));
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/scenario", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    simulation.setScenario(server.arg("scenario").length() ? server.arg("scenario") : server.arg("plain"));
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/randomize", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    simulation.enable();
    simulation.setMode("random");
    simulation.randomize();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/simulation/values", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    String error;
    bool ok = simulation.setValuesFromJson(server.arg("plain"), error);
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : String("{\"ok\":false,\"error\":\"") + error + "\"}");
  });
  server.on("/api/simulation/set-values", HTTP_POST, [this]() {
    if (!requireAuth()) return;
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
  server.on("/api/rules/validate", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", rules.validateRulesJson());
  });
  server.on("/api/ds18b20", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", sensors.detectedDs18b20Json());
  });
  server.on("/api/ds18b20/status", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", sensors.ds18b20StatusJson());
  });
  server.on("/api/ds18b20/assign", HTTP_POST, [this]() {
    if (!requireAuth()) return;
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
  server.on("/api/jsy/reconfigure-19200", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = sensors.reconfigureJsyTo19200();
    DynamicJsonDocument out(384);
    out["ok"] = ok;
    out["baudrate"] = state.jsyBaudrate;
    out["error"] = ok ? "" : state.jsyLastError;
    sendJson(out);
  });
  server.on("/api/wifi/test", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = wifi.testConnection(server.arg("ssid"), server.arg("password"));
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow/peer", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = espnow.addPeer(server.arg("mac"));
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow/peer/remove", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = espnow.removePeer(server.arg("mac"));
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", espnow.statusJson());
  });
  server.on("/api/espnow/config", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    JsonObject espnowConfig = config.system()["espnow"].is<JsonObject>()
                                ? config.system()["espnow"].as<JsonObject>()
                                : config.system()["espnow"].to<JsonObject>();
    espnowConfig["debugTransmission"] = server.arg("debugTransmission") == "true" || server.arg("debugTransmission") == "1";
    espnowConfig["debugReception"] = server.arg("debugReception") == "true" || server.arg("debugReception") == "1";
    bool ok = config.saveSystem();
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow/discovery", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", espnow.discoveredNodesJson());
  });
  server.on("/api/espnow/discovery/announce", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool ok = espnow.sendDiscovery();
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });
  server.on("/api/espnow/sensors/announce", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    espnow.sendAllSensorDiscovery();
    espnow.sendAllActuatorDiscovery();
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/restart", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    wifi.restart();
  });
  server.on("/api/system/reboot", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    wifi.restart();
  });
  server.on("/api/ota/github/check", HTTP_GET, [this]() { sendGithubOtaCheck(); });
  server.on("/api/ota/github/firmware", HTTP_POST, [this]() { startGithubFirmwareOta(); });
  server.on("/api/ota/github/littlefs", HTTP_POST, [this]() { startGithubLittleFsOta(); });
  server.on("/api/ota/rollback", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    const esp_partition_t *app1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
    const esp_partition_t *target = nullptr;
    if (running && app0 && running->address != app0->address) target = app0;
    else if (running && app1 && running->address != app1->address) target = app1;

    esp_app_desc_t desc;
    if (!target || esp_ota_get_partition_description(target, &desc) != ESP_OK) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"Aucune partition OTA precedente valide\"}");
      return;
    }
    esp_err_t err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
      server.send(500, "application/json", String("{\"ok\":false,\"error\":\"esp_ota_set_boot_partition: ") + esp_err_to_name(err) + "\"}");
      return;
    }
    state.logEvent("WARNING", "OTA_ROLLBACK", String("Rollback vers ") + target->label, "WebUi");
    server.send(200, "application/json", String("{\"ok\":true,\"target\":\"") + target->label + "\"}");
    delay(250);
    ESP.restart();
  });
}

void WebUi::sendJson(DynamicJsonDocument &doc) {
  if (!requireAuth()) return;
  state.webRequestCount++;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

bool WebUi::downloadGithubAssetToUpdate(const String &url, int updateCommand, const char *logCode, String &error, size_t &written, int &httpCode) {
  written = 0;
  httpCode = 0;
  error = "";
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi non connecte";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    error = "Initialisation HTTPS impossible";
    return false;
  }

  httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = String("HTTP ") + httpCode;
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  const size_t updateSize = contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(updateSize, updateCommand)) {
    error = Update.errorString();
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[1024];
  uint32_t lastDataMs = millis();
  while (http.connected() && (contentLength < 0 || written < static_cast<size_t>(contentLength))) {
    size_t available = stream->available();
    if (available) {
      int readLen = stream->readBytes(buffer, min<size_t>(available, sizeof(buffer)));
      if (readLen <= 0) continue;
      if (Update.write(buffer, readLen) != static_cast<size_t>(readLen)) {
        error = Update.errorString();
        Update.abort();
        http.end();
        return false;
      }
      written += readLen;
      lastDataMs = millis();
      delay(1);
    } else {
      if (millis() - lastDataMs > 20000UL) {
        error = "Timeout telechargement";
        Update.abort();
        http.end();
        return false;
      }
      delay(10);
    }
  }

  if (contentLength > 0 && written != static_cast<size_t>(contentLength)) {
    error = String("Telechargement incomplet: ") + written + "/" + contentLength;
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end(true)) {
    error = Update.errorString();
    http.end();
    return false;
  }

  http.end();
  state.logEvent("WARNING", logCode, String("OTA GitHub appliquee, octets=") + written, "WebUi");
  return true;
}

void WebUi::sendGithubOtaCheck() {
  if (!requireAuth()) return;
  DynamicJsonDocument doc(2048);
  JsonObject out = doc.to<JsonObject>();
  out["ok"] = false;
  out["versionUrl"] = GITHUB_VERSION_URL;
  out["releaseUrl"] = GITHUB_RELEASE_URL;
  out["localFirmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["localLittlefsVersion"] = readLittleFsTextFile("/www/littlefs_version.txt");

  if (WiFi.status() != WL_CONNECTED) {
    out["error"] = "WiFi non connecte";
    sendJson(doc);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  if (!http.begin(client, GITHUB_VERSION_URL)) {
    out["error"] = "Initialisation HTTPS impossible";
    sendJson(doc);
    return;
  }

  int httpCode = http.GET();
  out["httpCode"] = httpCode;
  if (httpCode != HTTP_CODE_OK) {
    out["error"] = String("HTTP ") + httpCode;
    http.end();
    sendJson(doc);
    return;
  }

  String payload = http.getString();
  http.end();
  DynamicJsonDocument remote(2048);
  DeserializationError err = deserializeJson(remote, payload);
  if (err || !remote.is<JsonObject>()) {
    out["error"] = String("version.json invalide: ") + err.c_str();
    sendJson(doc);
    return;
  }

  JsonObject src = remote.as<JsonObject>();
  const String remoteFirmware = src["firmwareVersion"] | src["version"] | "";
  const String remoteLittlefs = src["littlefsVersion"] | src["version"] | "";
  const String localLittlefs = out["localLittlefsVersion"].as<String>();
  out["ok"] = true;
  out["project"] = src["project"] | "";
  out["remoteVersion"] = src["version"] | "";
  out["remoteFirmwareVersion"] = remoteFirmware;
  out["remoteLittlefsVersion"] = remoteLittlefs;
  out["firmwareUrl"] = src["firmwareUrl"] | GITHUB_FIRMWARE_URL;
  out["littlefsUrl"] = src["littlefsUrl"] | GITHUB_LITTLEFS_URL;
  out["mandatory"] = src["mandatory"] | false;
  out["notes"] = src["notes"] | "";
  out["firmwareUpdateAvailable"] = remoteFirmware.length() && remoteFirmware != ROUTEUR_FIRMWARE_VERSION;
  out["littlefsUpdateAvailable"] = remoteLittlefs.length() && remoteLittlefs != localLittlefs;
  out["updateAvailable"] = (out["firmwareUpdateAvailable"] | false) || (out["littlefsUpdateAvailable"] | false);
  sendJson(doc);
}

void WebUi::startGithubFirmwareOta() {
  if (!requireAuth()) return;
  String error;
  size_t written = 0;
  int httpCode = 0;
  state.logEvent("WARNING", "OTA_GITHUB_FIRMWARE", "Demarrage OTA firmware GitHub", "WebUi");
  bool ok = downloadGithubAssetToUpdate(GITHUB_FIRMWARE_URL, U_FLASH, "OTA_GITHUB_FIRMWARE", error, written, httpCode);
  DynamicJsonDocument doc(512);
  JsonObject out = doc.to<JsonObject>();
  out["ok"] = ok;
  out["httpCode"] = httpCode;
  out["bytes"] = written;
  if (!ok) out["error"] = error.length() ? error : "OTA firmware GitHub refusee";
  String response;
  serializeJson(doc, response);
  server.send(ok ? 200 : 500, "application/json", response);
  if (ok) {
    delay(250);
    ESP.restart();
  }
}

void WebUi::startGithubLittleFsOta() {
  if (!requireAuth()) return;
  state.logEvent("WARNING", "OTA_GITHUB_LITTLEFS", "Demarrage OTA LittleFS GitHub", "WebUi");
  littleFsOtaBackupOk = config.backupToNvsBeforeLittleFsOta();
  if (!littleFsOtaBackupOk) {
    DynamicJsonDocument doc(384);
    doc["ok"] = false;
    doc["error"] = config.lastError().length() ? config.lastError() : "Sauvegarde config NVS impossible";
    sendJson(doc);
    return;
  }

  LittleFS.end();
  String error;
  size_t written = 0;
  int httpCode = 0;
  bool ok = downloadGithubAssetToUpdate(GITHUB_LITTLEFS_URL, U_SPIFFS, "OTA_GITHUB_LITTLEFS", error, written, httpCode);
  if (!ok) LittleFS.begin(false);

  DynamicJsonDocument doc(512);
  JsonObject out = doc.to<JsonObject>();
  out["ok"] = ok;
  out["httpCode"] = httpCode;
  out["bytes"] = written;
  if (!ok) out["error"] = error.length() ? error : "OTA LittleFS GitHub refusee";
  String response;
  serializeJson(doc, response);
  server.send(ok ? 200 : 500, "application/json", response);
  littleFsOtaBackupOk = false;
  if (ok) {
    delay(250);
    ESP.restart();
  }
}

void WebUi::sendStatusLite() {
  const uint32_t apiStateStartedMs = millis();
  state.apiStateRequestCount++;
  DynamicJsonDocument doc(20480);
  JsonObject out = doc.to<JsonObject>();
  auto setNumber = [&out](const char *key, float value) {
    if (isnan(value) || isinf(value)) out[key] = nullptr;
    else out[key] = value;
  };
  auto setObjectNumber = [](JsonObject obj, const char *key, float value) {
    if (isnan(value) || isinf(value)) obj[key] = nullptr;
    else obj[key] = value;
  };
  const uint32_t nowMs = millis();
  auto sensorAgeMs = [nowMs](uint32_t lastUpdateMs) -> uint32_t {
    return lastUpdateMs && nowMs >= lastUpdateMs ? nowMs - lastUpdateMs : 0;
  };
  auto setSensorRuntime = [&sensorAgeMs](JsonObject item, bool enabled, bool available, uint32_t lastUpdateMs, uint32_t timeoutMs) {
    const bool neverSeen = lastUpdateMs == 0;
    const uint32_t ageMs = sensorAgeMs(lastUpdateMs);
    const bool timedOut = enabled && (neverSeen || ageMs > timeoutMs);
    item["enabled"] = enabled;
    item["ok"] = enabled && available && !timedOut;
    item["timedOut"] = timedOut;
    item["neverSeen"] = neverSeen;
    if (neverSeen) item["ageMs"] = nullptr;
    else item["ageMs"] = ageMs;
    if (lastUpdateMs) {
      item["lastUpdateMs"] = lastUpdateMs;
      item["lastSeenMs"] = lastUpdateMs;
    } else {
      item["lastUpdateMs"] = nullptr;
      item["lastSeenMs"] = nullptr;
    }
  };

  out["ok"] = true;
  JsonObject device = config.device();
  JsonObject system = config.system();
  out["moduleName"] = device["deviceName"] | device["name"] | state.moduleName.c_str();
  out["deviceId"] = state.deviceId;
  out["nodeId"] = state.nodeId;
  out["role"] = device["role"] | RuntimeState::roleToString(state.role);
  out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
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
  out["mqttEnabled"] = state.mqttEnabled;
  out["mqttConnected"] = state.mqttConnected;
  out["mqttStatus"] = state.mqttStatus;
  out["lastMqttPublishAgeMs"] = state.lastMqttPublishMs ? millis() - state.lastMqttPublishMs : 4294967295UL;
  out["jsyOnline"] = state.jsyOnline;
  out["ticAvailable"] = state.ticAvailable;
  out["ticStatus"] = state.ticStatus;
  setNumber("ticApparentPowerVA", state.ticApparentPowerVA);
  setNumber("ticGridPowerW", state.ticGridPowerW);
  setNumber("ticCurrentA", state.ticCurrentA);
  out["ticEnergyWh"] = static_cast<uint32_t>(state.ticEnergyWh);
  out["ticTariff"] = state.ticTariff;
  out["ticPeriod"] = state.ticPeriod;
  out["lastTicReadMs"] = state.lastTicReadMs;
  out["ticErrorCount"] = state.ticErrorCount;
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
  sensors.remoteSensorsToJson(out["remoteSensors"].to<JsonArray>());
  setNumber("gridPowerW", state.gridPowerW);
  setNumber("gridPowerRawW", state.gridPowerRawW);
  setNumber("gridPowerFilteredW", state.gridPowerFilteredW);
  out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
  setNumber("jsyGridPowerW", state.jsyGridPowerW);
  setNumber("ticGridPowerW", state.ticGridPowerW);
  setNumber("gridVoltageV", state.gridVoltageV);
  setNumber("gridCurrentA", state.gridCurrentA);
  setNumber("gridPowerFactor", state.gridPowerFactor);
  setNumber("gridFrequencyHz", state.gridFrequencyHz);
  out["gridEnergyDirection"] = state.gridEnergyDirection;
  setNumber("voltageV1", state.voltageV1);
  setNumber("voltageV2", state.voltageV2);
  setNumber("activePowerW1", state.activePowerW1);
  setNumber("activePowerW2", state.activePowerW2);
  setNumber("currentA1", state.currentA1);
  setNumber("currentA2", state.currentA2);
  setNumber("powerFactor1", state.powerFactor1);
  setNumber("powerFactor2", state.powerFactor2);
  out["energyDirection1"] = state.energyDirection1;
  out["energyDirection2"] = state.energyDirection2;
  out["jsyBaudrate"] = state.jsyBaudrate;
  out["jsyLastError"] = state.jsyLastError;
  out["jsyTimeoutCount"] = state.jsyTimeoutCount;
  out["jsyCrcErrorCount"] = state.jsyCrcErrorCount;
  setNumber("injectionW", state.injectionW);
  setNumber("consumptionW", state.consumptionW);
  setNumber("surplusW", state.surplusW);
  setNumber("tankTopC", state.tankTopC);
  setNumber("tankMiddleC", state.tankMiddleC);
  setNumber("tankBottomC", state.tankBottomC);
  setNumber("ssr1PowerPct", state.ssr1PowerPct);
  setNumber("ssr2PowerPct", state.ssr2PowerPct);
  setNumber("robotDynPowerPct", state.robotDynPowerPct);
  out["ssr1OutputOn"] = state.ssr1OutputOn;
  out["ssr2OutputOn"] = state.ssr2OutputOn;
  out["robotDynOutputOn"] = state.robotDynOutputOn;
  out["ssr1PinHigh"] = state.ssr1PinHigh;
  out["ssr2PinHigh"] = state.ssr2PinHigh;
  out["robotDynPinHigh"] = state.robotDynPinHigh;
  setNumber("heaterPowerW", state.heaterPowerW);
  setNumber("commandPercent", state.commandPercent);
  setNumber("pidOutputPercent", state.pidOutputPercent);
  setNumber("pidErrorW", state.pidErrorW);
  out["pidEnabled"] = state.pidEnabled;
  out["pidStatus"] = state.pidStatus;
  actuators.runtimeToJson(out["actuators"].to<JsonArray>());
  JsonObject router = system["router"].as<JsonObject>();
  setNumber("pidMeasuredW", isnan(state.gridPowerFilteredW) || isinf(state.gridPowerFilteredW) ? state.gridPowerW : state.gridPowerFilteredW);
  setNumber("gridSetpointW", router["gridSetpointW"] | 0.0f);
  setNumber("deadbandW", router["deadbandW"] | 30.0f);
  setNumber("pidKp", router["kp"] | router["pidKp"] | 0.02f);
  setNumber("pidKi", router["ki"] | router["pidKi"] | 0.002f);
  setNumber("pidKd", router["kd"] | router["pidKd"] | 0.0f);
  setNumber("maxOutputRampPercentPerSecond", router["maxOutputRampPercentPerSecond"] | 5.0f);
  setNumber("heaterMaxPowerW", router["heaterMaxPowerW"] | router["ssr1MaxW"] | 1500.0f);
  JsonObject energy = out["energy"].to<JsonObject>();
  setObjectNumber(energy, "productionW", state.productionW);
  setObjectNumber(energy, "gridPowerW", state.gridPowerW);
  setObjectNumber(energy, "heaterPowerW", state.heaterPowerW);
  setObjectNumber(energy, "heaterPercent", state.commandPercent);
  setObjectNumber(energy, "tankTempC", state.tankTopC);
  setObjectNumber(energy, "surplusW", state.surplusW);
  setObjectNumber(energy, "consumptionW", state.consumptionW);

  JsonObject pid = out["pid"].to<JsonObject>();
  pid["mode"] = router["mode"] | (state.pidEnabled ? "AUTO" : "OFF");
  String pidSource = router["gridPowerSource"] | state.gridPowerSource.c_str();
  String pidSourceUpper = pidSource;
  pidSourceUpper.toUpperCase();
  bool sourceIsJsy = pidSourceUpper.indexOf("JSY") >= 0 || (pidSourceUpper == "AUTO" && state.jsyOnline);
  bool sourceIsLinky = pidSourceUpper.indexOf("TIC") >= 0 || pidSourceUpper.indexOf("LINKY") >= 0 || (pidSourceUpper == "AUTO" && state.ticAvailable && !sourceIsJsy);
  bool sourceIsEspNow = false;
  for (JsonObject cfg : config.sensors()) {
    if (!(cfg["enabled"] | true)) continue;
    String source = cfg["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) continue;
    String type = cfg["type"] | "";
    type.toUpperCase();
    if (sourceIsJsy && type.indexOf("JSY") >= 0) sourceIsEspNow = true;
    if (sourceIsLinky && (type.indexOf("TIC") >= 0 || type.indexOf("LINKY") >= 0)) sourceIsEspNow = true;
  }
  if (sourceIsJsy) pid["source"] = sourceIsEspNow ? "ESPNOW_JSY" : "LOCAL_JSY";
  else if (sourceIsLinky) pid["source"] = sourceIsEspNow ? "ESPNOW_LINKY" : "LOCAL_LINKY";
  else pid["source"] = pidSource;
  setObjectNumber(pid, "targetGridW", router["gridSetpointW"] | 0.0f);
  setObjectNumber(pid, "deadbandW", router["deadbandW"] | 30.0f);
  setObjectNumber(pid, "errorW", state.pidErrorW);
  setObjectNumber(pid, "outputPercent", state.pidOutputPercent);
  setObjectNumber(pid, "commandPercent", state.commandPercent);
  pid["status"] = state.pidStatus.length() ? state.pidStatus : "IDLE";
  setObjectNumber(pid, "heaterPowerW", state.heaterPowerW);

  JsonArray activeSensors = out["sensors"].to<JsonArray>();
  for (JsonObject cfg : config.sensors()) {
    const bool enabled = cfg["enabled"] | true;
    if (!enabled) continue;
    String source = cfg["source"] | "local";
    if (source.equalsIgnoreCase("espnow")) continue;
    String type = cfg["type"] | "";
    String id = cfg["id"] | "";
    JsonObject item = activeSensors.add<JsonObject>();
    item["id"] = id;
    item["name"] = cfg["name"] | id.c_str();
    item["type"] = type;
    item["role"] = cfg["role"] | "";
    item["origin"] = "LOCAL";
    if (cfg["channels"].is<JsonArray>()) item["channels"].set(cfg["channels"]);
    JsonObject values = item["values"].to<JsonObject>();
    if (type.indexOf("JSY") >= 0 || id == "jsy_grid") {
      const uint32_t readIntervalMs = cfg["readIntervalMs"] | 350UL;
      const uint32_t timeoutMs = cfg["timeoutMs"] | 300UL;
      setSensorRuntime(item, enabled, state.jsyOnline, state.lastJsyReadMs, readIntervalMs + timeoutMs * 3UL);
      if (state.lastJsyReadMs) {
        setObjectNumber(values, "GRID", state.jsyGridPowerW);
        setObjectNumber(values, "VOLT", state.gridVoltageV);
        setObjectNumber(values, "FREQ", state.gridFrequencyHz);
        setObjectNumber(values, "CH1_CURR", state.currentA1);
        setObjectNumber(values, "CH1_POWER", state.activePowerW1);
        setObjectNumber(values, "CH1_PF", state.powerFactor1);
        setObjectNumber(values, "CH1_ENERGY_POS", state.jsyImportEnergyWh1);
        setObjectNumber(values, "CH1_ENERGY_NEG", state.jsyExportEnergyWh1);
        values["CH1_DIR"] = state.energyDirection1;
        setObjectNumber(values, "CH2_CURR", state.currentA2);
        setObjectNumber(values, "CH2_POWER", state.activePowerW2);
        setObjectNumber(values, "CH2_PF", state.powerFactor2);
        setObjectNumber(values, "CH2_ENERGY_POS", state.jsyImportEnergyWh2);
        setObjectNumber(values, "CH2_ENERGY_NEG", state.jsyExportEnergyWh2);
        values["CH2_DIR"] = state.energyDirection2;
      }
    } else if (type.indexOf("TIC") >= 0 || type.indexOf("Linky") >= 0 || id == "tic_linky") {
      const uint32_t timeoutMs = cfg["timeoutMs"] | 5000UL;
      setSensorRuntime(item, enabled, state.ticAvailable, state.lastTicReadMs, timeoutMs);
      if (state.lastTicReadMs) {
        setObjectNumber(values, "GRID", state.ticGridPowerW);
        setObjectNumber(values, "PAPP", state.ticApparentPowerVA);
        setObjectNumber(values, "IINST", state.ticCurrentA);
      }
    } else {
      setSensorRuntime(item, enabled, true, nowMs, 60000UL);
    }
  }
  uint8_t unifiedDsIndex = 0;
  for (JsonObject cfg : config.sensorsDoc()["ds18b20"].as<JsonArray>()) {
    if (unifiedDsIndex >= 3) break;
    const bool enabled = cfg["enabled"] | true;
    if (!enabled) {
      unifiedDsIndex++;
      continue;
    }
    JsonObject item = activeSensors.add<JsonObject>();
    String fallbackId = String("sonde") + String(unifiedDsIndex + 1);
    String id = cfg["id"] | fallbackId.c_str();
    item["id"] = id;
    item["name"] = cfg["name"] | id.c_str();
    item["type"] = "DS18B20";
    item["role"] = cfg["role"] | "";
    item["origin"] = "LOCAL";
    JsonObject bus = config.sensorsDoc()["oneWireBus"];
    const uint32_t readIntervalMs = bus["readIntervalMs"] | 2000UL;
    setSensorRuntime(item, enabled, state.ds18b20Available[unifiedDsIndex], state.ds18b20LastReadMs[unifiedDsIndex], readIntervalMs * 3UL);
    JsonObject values = item["values"].to<JsonObject>();
    if (state.ds18b20LastReadMs[unifiedDsIndex]) setObjectNumber(values, "TEMP", state.ds18b20Temps[unifiedDsIndex]);
    unifiedDsIndex++;
  }
  sensors.remoteSensorsToJson(activeSensors, true);

  JsonObject health = out["systemHealth"].to<JsonObject>();
  health["wifi"] = state.wifiConnected ? "OK" : state.networkMode;
  health["espnow"] = state.espNowReady ? "OK" : "N/A";
  health["mqtt"] = state.mqttEnabled ? (state.mqttConnected ? "OK" : state.mqttStatus) : "N/A";
  health["safety"] = state.safetyLevel;
  health["uptimeMs"] = millis();
  health["heapFree"] = ESP.getFreeHeap();
  health["lastFault"] = state.safetyReason.length() ? state.safetyReason : "aucun";
  out["heapFree"] = ESP.getFreeHeap();
  out["heapMin"] = ESP.getMinFreeHeap();
  out["heapMaxAlloc"] = ESP.getMaxAllocHeap();
  state.apiStateBuildMs = millis() - apiStateStartedMs;
  state.apiStateJsonBytes = measureJson(doc);
  state.performanceToJson(out["performance"].to<JsonObject>());
  sendJson(doc);
}

void WebUi::sendSystemInfo() {
  DynamicJsonDocument doc(10240);
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
  out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["buildVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
  out["buildNumber"] = ROUTEUR_BUILD_NUMBER;
  out["firmwareMarker"] = ROUTEUR_ACTIVE_FIRMWARE_MARKER;
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
  out["ntpEnabled"] = state.ntpEnabled;
  out["ntpSynced"] = state.ntpSynced;
  out["ntpStatus"] = state.ntpStatus.length() ? state.ntpStatus : "N/A";
  if (state.ntpSynced) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 20)) {
      char buffer[24];
      strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
      out["localDateTime"] = buffer;
    } else {
      out["localDateTime"] = "N/A";
    }
  } else {
    out["localDateTime"] = "N/A";
  }
  out["role"] = RuntimeState::roleToString(state.role);
  out["safetyLevel"] = state.safetyLevel.length() ? state.safetyLevel : "N/A";
  out["safetyReason"] = state.safetyReason.length() ? state.safetyReason : "N/A";
  out["simulationMode"] = state.simulationMode;

  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *app0 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  const esp_partition_t *app1 = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);

  JsonObject ota = out["ota"].to<JsonObject>();
  ota["runningLabel"] = running ? running->label : "N/A";
  ota["runningSlot"] = running ? partitionSubtypeLabel(running->subtype) : "N/A";
  ota["bootLabel"] = boot ? boot->label : "N/A";
  ota["bootSlot"] = boot ? partitionSubtypeLabel(boot->subtype) : "N/A";
  ota["nextUpdateLabel"] = next ? next->label : "N/A";
  ota["nextUpdateSlot"] = next ? partitionSubtypeLabel(next->subtype) : "N/A";
  ota["currentFirmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  ota["sketchSize"] = ESP.getSketchSize();
  ota["freeSketchSpace"] = ESP.getFreeSketchSpace();
  ota["runningSlotSize"] = running ? running->size : 0;
  ota["runningRemainingBytes"] = running && running->size > ESP.getSketchSize() ? running->size - ESP.getSketchSize() : 0;
  appendOtaPartitionJson(ota["app0"].to<JsonObject>(), app0, running, boot);
  appendOtaPartitionJson(ota["app1"].to<JsonObject>(), app1, running, boot);

  JsonObject storage = out["storage"].to<JsonObject>();
  storage["type"] = "LittleFS";
  storage["version"] = readLittleFsTextFile("/www/littlefs_version.txt");
  storage["total"] = LittleFS.totalBytes();
  storage["used"] = LittleFS.usedBytes();
  storage["status"] = state.littleFsOk || LittleFS.totalBytes() > 0 ? "OK" : "Erreur";

  JsonObject githubOta = out["githubOta"].to<JsonObject>();
  githubOta["versionUrl"] = GITHUB_VERSION_URL;
  githubOta["firmwareUrl"] = GITHUB_FIRMWARE_URL;
  githubOta["littlefsUrl"] = GITHUB_LITTLEFS_URL;
  githubOta["releaseUrl"] = GITHUB_RELEASE_URL;
  githubOta["localFirmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  githubOta["localLittlefsVersion"] = storage["version"];

  JsonObject services = out["services"].to<JsonObject>();
  services["wifi"] = wifiOk ? "OK" : (state.networkMode == "AP" || state.networkMode == "AP_STA" ? "Attention" : "Erreur");
  services["ntp"] = !state.ntpEnabled ? "N/A" : (state.ntpSynced ? "OK" : "Attention");
  JsonObject mqtt = config.system()["mqtt"].as<JsonObject>();
  services["mqtt"] = !mqtt.isNull() && (mqtt["enabled"] | false) ? (state.mqttConnected ? "OK" : "Attention") : "N/A";
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
  if (!requireAuth()) return true;
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  if (file.size() == 0) {
    file.close();
    return false;
  }
  state.webRequestCount++;
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
  if (!requireAuth()) return;
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
  if (!requireAuth()) return;
  String out;
  if (strcmp(name, "device") == 0) serializeJson(config.deviceDoc(), out);
  if (strcmp(name, "system") == 0) serializeJson(config.systemDoc(), out);
  if (strcmp(name, "sensors") == 0) serializeJson(config.sensorsDoc(), out);
  if (strcmp(name, "actuators") == 0) serializeJson(config.actuatorsDoc(), out);
  if (strcmp(name, "rules") == 0) serializeJson(config.rulesDoc(), out);
  server.send(200, "application/json", out);
}

void WebUi::saveConfig(const char *name, const char *path) {
  if (!requireAuth()) return;
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
  if (ok && strcmp(name, "sensors") == 0) {
    espnow.sendDiscovery();
    espnow.sendAllSensorDiscovery();
    state.addLog("ESP-NOW exports capteurs annonces apres sauvegarde");
  }
  if (ok && strcmp(name, "actuators") == 0) {
    espnow.sendDiscovery();
    espnow.sendAllActuatorDiscovery();
    state.addLog("ESP-NOW capacites actionneurs annoncees apres sauvegarde");
  }
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
  html.reserve(6200);
  html += F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>RouteurSolaireESP32 Secours</title>");
  html += F("<style>body{margin:0;background:#111827;color:#F9FAFB;font-family:system-ui,Arial,sans-serif;padding:18px}h1{margin:0 0 6px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:12px}.card{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:14px;margin:10px 0}.label{color:#9CA3AF;font-size:12px}.value{font-size:20px;font-weight:700;margin-top:4px}.ok{color:#22C55E}.warn{color:#FF9800}.bad{color:#F44336}.muted{color:#9CA3AF}a,button,input[type=submit]{display:inline-block;margin:6px 6px 6px 0;background:#263244;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:10px 12px;text-decoration:none}input[type=file]{width:100%;background:#0B1220;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:10px;margin:8px 0}.danger{border-color:#F44336;background:#3b1111}.bar{height:8px;background:#0B1220;border-radius:999px;overflow:hidden}.fill{height:100%;background:#2196F3}.note{border-left:3px solid #FF9800;padding-left:10px;color:#FFD18A}</style>");
  html += F("</head><body><h1>RouteurSolaireESP32 - Secours</h1><p class=\"muted\">Page embarquee dans le firmware. Elle fonctionne meme si LittleFS ou /app pose probleme.</p>");
  html += F("<div class=\"grid\"><div class=\"card\"><div class=\"label\">Module</div><div class=\"value\">");
  html += state.moduleName.length() ? state.moduleName : "Routeur solaire ESP32";
  html += F("</div><p class=\"muted\">Role ");
  html += RuntimeState::roleToString(state.role);
  html += F("</p></div><div class=\"card\"><div class=\"label\">Safety</div><div class=\"value ");
  html += state.safetyTripped ? "bad" : "ok";
  html += F("\">");
  html += state.safetyLevel;
  html += F("</div><p class=\"muted\">");
  html += state.safetyReason.length() ? state.safetyReason : "Aucun defaut critique";
  html += F("</p></div><div class=\"card\"><div class=\"label\">WiFi</div><div class=\"value ");
  html += state.wifiConnected ? "ok" : "warn";
  html += F("\">");
  html += state.networkMode;
  html += F("</div><p class=\"muted\">SSID ");
  html += state.wifiSsid.length() ? state.wifiSsid : "-";
  html += F("</p></div><div class=\"card\"><div class=\"label\">LittleFS</div><div class=\"value ");
  html += state.littleFsOk ? "ok" : "bad";
  html += F("\">");
  html += state.littleFsOk ? "OK" : "Erreur";
  html += F("</div><div class=\"bar\"><div class=\"fill\" style=\"width:");
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  uint8_t pct = total ? min<size_t>(100, (used * 100) / total) : 0;
  html += pct;
  html += F("%\"></div></div><p class=\"muted\">");
  html += String(used / 1024);
  html += F(" Ko / ");
  html += String(total / 1024);
  html += F(" Ko</p></div></div>");
  html += F("<div class=\"card\"><h2>Reseau</h2><p>IP box: ");
  html += state.stationIp;
  html += F("</p><p>IP AP: ");
  html += state.apIp;
  html += F("</p><p>RSSI: ");
  html += String(state.rssi);
  html += F(" dBm</p></div>");
  html += F("<div class=\"card\"><h2>Acces rapides</h2>");
  html += F("<a href=\"/app\">Interface principale /app</a><a href=\"/lite\">Secours lite</a><a href=\"/fs\">Fichiers LittleFS</a><a href=\"/api/status-lite\">API status-lite</a><a href=\"/api/diagnostic\">API diagnostic</a><a href=\"/api/system-info\">API systeme</a><a href=\"/README.md\">README</a>");
  html += F("</div><div class=\"card\"><h2>OTA firmware</h2><p class=\"note\">Necessite une partition avec OTA. Ne coupe pas l'alimentation pendant l'envoi.</p><form method=\"POST\" action=\"/ota/firmware\" enctype=\"multipart/form-data\" onsubmit=\"return confirm('Televerser ce firmware et redemarrer ?')\"><input type=\"file\" name=\"firmware\" accept=\".bin\" required><input type=\"submit\" value=\"Televerser firmware .bin\"></form></div>");
  html += F("<div class=\"card\"><h2>OTA LittleFS</h2><p class=\"note\">Envoie l'image LittleFS .bin correspondant a la partition actuelle. Apres upload, l'ESP32 redemarre.</p><form method=\"POST\" action=\"/ota/littlefs\" enctype=\"multipart/form-data\" onsubmit=\"return confirm('Televerser LittleFS et redemarrer ?')\"><input type=\"file\" name=\"littlefs\" accept=\".bin\" required><input type=\"submit\" value=\"Televerser LittleFS .bin\"></form></div>");
  html += F("<div class=\"card\"><h2>Actions</h2><form method=\"POST\" action=\"/api/system/reboot\" onsubmit=\"return confirm('Redemarrer ESP32 ?')\"><input class=\"danger\" type=\"submit\" value=\"Redemarrer ESP32\"></form></div>");
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
<nav><button onclick="show('dash')">Dashboard</button><button onclick="show('sim')">Simulation</button><button onclick="show('diag')">Diagnostic</button><button onclick="show('wifi')">WiFi</button><button onclick="location.href='/app'">Interface principale</button><button onclick="location.href='/'">Secours OTA</button></nav>
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

