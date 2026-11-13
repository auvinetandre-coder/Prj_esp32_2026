#include "web_ui.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <time.h>
#include "../build_info.h"
#include "../boot/boot_manager.h"
#include "../diagnostics/serial_capture.h"

static const char *ROUTEUR_ACTIVE_FIRMWARE_MARKER = ROUTEUR_FIRMWARE_MARKER;
static const char *RECOVERY_NVS_NAMESPACE = "recovery";

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

static bool isRecoveryWifiPlaceholder(const String &value) {
  if (value.length() == 0) return true;
  String text = value;
  text.toUpperCase();
  return text.indexOf("A_CONFIGURER") >= 0 || text.indexOf("ACONFIGURER") >= 0;
}

static bool syncRecoveryWifiCredentials(ConfigManager &config) {
  JsonObject sys = config.system();
  const char *modernSsid = sys["wifi"]["ssid"] | "";
  const char *legacySsid = sys["wifiSsid"] | "";
  const char *modernPassword = sys["wifi"]["password"] | "";
  const char *legacyPassword = sys["wifiPassword"] | "";
  String ssid = modernSsid && modernSsid[0] ? modernSsid : legacySsid;
  String password = modernPassword && modernPassword[0] ? modernPassword : legacyPassword;
  if (isRecoveryWifiPlaceholder(ssid) || password.length() == 0) return false;

  Preferences prefs;
  if (!prefs.begin(RECOVERY_NVS_NAMESPACE, false)) return false;
  bool ok = prefs.putString("ssid", ssid) > 0;
  ok = prefs.putString("pass", password) > 0 && ok;
  prefs.end();
  return ok;
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
  consoleRoutes();
  server.on("/www/serial_console.js", HTTP_GET, [this]() {
    if (!streamLittleFsFile("/www/serial_console.js", "application/javascript; charset=utf-8")) {
      server.send(404, "text/plain", "Console serie absente de LittleFS");
    }
  });
  server.on("/", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    server.sendHeader("Location", "/app", true);
    server.send(302, "text/plain", "Redirection vers /app");
  });
  server.on("/recovery", HTTP_GET, [this]() {
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
    state.toJson(out);
    JsonObject device = config.device();
    JsonObject system = config.system();
    const char *deviceName = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["moduleName"] = deviceName;
    JsonObject deviceOut = out["device"].to<JsonObject>();
    deviceOut["name"] = deviceName;
    out["nodeId"] = state.nodeId;
    out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
    out["littleFsVersion"] = readLittleFsTextFile("/www/littlefs_version.txt");
    out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
    const unsigned long nowMs = millis();
    out["uptimeMs"] = nowMs;
    time_t nowEpoch = time(nullptr);
    out["currentEpochMs"] = nowEpoch > 1600000000 ? static_cast<uint64_t>(nowEpoch) * 1000ULL + (nowMs % 1000UL) : 0;
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
    appendActuatorMeasurements(out["actuators"].as<JsonArray>());
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
    setNumber("gridGraphPowerW", gridPowerForGraph());
    setNumber("productionW", state.productionW);
    setNumber("injectionW", state.injectionW);
    setNumber("consumptionW", state.consumptionW);
    setNumber("surplusW", state.surplusW);
    setNumber("targetW", isfinite(state.pidGridSetpointW) ? state.pidGridSetpointW : (router["gridSetpointW"] | 0.0f));
    setNumber("deadbandW", state.pidDeadbandW);
    setNumber("pidErrorW", state.pidErrorW);
    setNumber("pidOutputPercent", state.pidOutputPercent);
    setNumber("commandPercent", state.commandPercent);
    setNumber("outputPowerW", state.outputPowerW);
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
    setNumber("temp1", isnan(state.ds18b20Temps[0]) || isinf(state.ds18b20Temps[0]) ? state.temperature1C : state.ds18b20Temps[0]);
    setNumber("temp2", isnan(state.ds18b20Temps[1]) || isinf(state.ds18b20Temps[1]) ? state.temperature2C : state.ds18b20Temps[1]);
    setNumber("temp3", isnan(state.ds18b20Temps[2]) || isinf(state.ds18b20Temps[2]) ? state.temperature3C : state.ds18b20Temps[2]);
    setNumber("tempSafety", router["tempSafetyMaxC"] | 70.0f);
    out["pidStatus"] = state.pidStatus;
    out["safetyLevel"] = state.safetyLevel;
    sendJson(doc);
  });
  server.on("/api/diagnostic", HTTP_GET, [this]() {
    DynamicJsonDocument doc(8192);
    JsonObject out = doc.to<JsonObject>();
    state.toJson(out);
    JsonObject device = config.device();
    JsonObject system = config.system();
    const char *deviceName = device["deviceName"] | device["name"] | state.moduleName.c_str();
    out["moduleName"] = deviceName;
    JsonObject deviceOut = out["device"].to<JsonObject>();
    deviceOut["name"] = deviceName;
    out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
    out["littleFsVersion"] = readLittleFsTextFile("/www/littlefs_version.txt");
    out["buildTimestamp"] = ROUTEUR_BUILD_TIMESTAMP;
    out["gridPowerSource"] = system["router"]["gridPowerSource"] | state.gridPowerSource.c_str();
    sendJson(doc);
  });
  server.on("/api/diagnostic/wifi", HTTP_GET, [this]() {
    if (!requireAuth()) return;
    DynamicJsonDocument doc(12288);
    wifi.diagnosticsToJson(doc.to<JsonObject>());
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
    server.send(200, "text/markdown", F("# RouteurSolaireESP32\n\nREADME embarque non trouve dans LittleFS.\n\nPour l'afficher completement depuis l'ESP32, placer README.md dans le dossier data puis televerser les donnees LittleFS.\n\nSections principales : presentation, capteurs, actionneurs, interface Web, securites, cablage, installation Arduino IDE, diagnostic et avertissement 230 V.\n"));
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
    String controlMode = server.arg("controlMode");
    if (!id.length()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"actuatorId manquant\"}");
      return;
    }
    if (controlMode.length()) {
      if (controlMode != "manual" && controlMode != "auto") {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Mode de commande invalide\"}");
        return;
      }
      bool found = false;
      bool enabled = false;
      for (JsonObject actuator : config.actuators()) {
        if (id == (actuator["id"] | "")) { found = true; enabled = actuator["enabled"] | false; }
      }
      if (!found || (controlMode == "manual" && !enabled)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Actionneur absent ou desactive\"}");
        return;
      }
      if (controlMode == "manual") {
        String raw = server.arg("value");
        char *end = nullptr;
        value = strtof(raw.c_str(), &end);
        if (!raw.length() || !end || *end || !isfinite(value) || value < 0 || value > 100) {
          server.send(400, "application/json", "{\"ok\":false,\"error\":\"Puissance attendue entre 0 et 100 %\"}");
          return;
        }
        command = "setActuatorPercent";
      } else {
        if (state.manualCommandPersistent && state.manualCommandActuatorId != id) {
          server.send(409, "application/json", "{\"ok\":false,\"error\":\"Un autre actionneur est en commande manuelle\"}");
          return;
        }
        command = "stop";
        value = 0;
      }
    }
    if (controlMode.length() && !actuators.requestSharedControl(id, controlMode, value)) {
      server.send(503, "application/json", "{\"ok\":false,\"error\":\"Commande partagee non envoyee\"}");
      return;
    }
    state.manualCommandPersistent = controlMode == "manual";
    state.manualCommandActuatorId = id;
    state.manualCommand = command.length() ? command : "setPower";
    state.manualCommandPercent = value;
    state.manualCommandSource = "WEB";
    state.manualCommandReason = String("Commande Web ") + id + " " + state.manualCommand + " " + String(value, 1);
    state.manualCommandPending = true;
    state.logEvent("INFO", "MANUAL_COMMAND_REQUEST", state.manualCommandReason, "WebUi");
    server.send(202, "application/json", "{\"ok\":true,\"queued\":true}");
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
    BootManager::setNextBootMain();
    wifi.restart();
  });
  server.on("/api/system/reboot", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    BootManager::setNextBootMain();
    wifi.restart();
  });
  server.on("/api/system/recovery", HTTP_POST, [this]() {
    if (!requireAuth()) return;
    bool wifiSynced = syncRecoveryWifiCredentials(config);
    if (!BootManager::setNextBootFactory()) {
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"factory recovery introuvable\"}");
      return;
    }
    state.logEvent("WARNING", "BOOT_RECOVERY", wifiSynced ? "Passage recovery avec WiFi NVS synchronise" : "Passage recovery sans WiFi NVS", "WebUi");
    server.send(200, "application/json", wifiSynced ? "{\"ok\":true,\"wifiSynced\":true}" : "{\"ok\":true,\"wifiSynced\":false}");
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

void WebUi::appendActuatorMeasurements(JsonArray items) {
  for (JsonObject item : items) {
    const float measured = sensors.jsyRoleValue("load", "POWER", String(item["id"] | ""));
    if (isfinite(measured)) item["measuredPowerW"] = measured;
    else item["measuredPowerW"] = nullptr;
    item["measurementAvailable"] = isfinite(measured);
  }
}

float WebUi::gridPowerForGraph() {
  const bool usesTic = state.gridPowerSource == "TIC" ||
      (state.gridPowerSource == "AUTO" && state.ticAvailable && isfinite(state.ticGridPowerW));
  if (usesTic) return state.gridPowerW;
  const auto sample = sensors.gridPowerSample("grid", "gridPowerW", millis());
  return sample.valid ? sample.watts : NAN;
}

void WebUi::sendStatusLite() {
  const uint32_t apiStateStartedMs = millis();
  sensors.refreshMqttProduction();
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
  const char *deviceName = device["deviceName"] | device["name"] | state.moduleName.c_str();
  out["moduleName"] = deviceName;
  JsonObject deviceOut = out["device"].to<JsonObject>();
  deviceOut["name"] = deviceName;
  out["deviceId"] = state.deviceId;
  out["nodeId"] = state.nodeId;
  out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["littleFsVersion"] = readLittleFsTextFile("/www/littlefs_version.txt");
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
  setNumber("gridGraphPowerW", gridPowerForGraph());
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
  setNumber("temperature1C", state.temperature1C);
  setNumber("temperature2C", state.temperature2C);
  setNumber("temperature3C", state.temperature3C);
  setNumber("ssr1PowerPct", state.ssr1PowerPct);
  setNumber("ssr2PowerPct", state.ssr2PowerPct);
  setNumber("robotDynPowerPct", state.robotDynPowerPct);
  out["ssr1OutputOn"] = state.ssr1OutputOn;
  out["ssr2OutputOn"] = state.ssr2OutputOn;
  out["robotDynOutputOn"] = state.robotDynOutputOn;
  out["ssr1PinHigh"] = state.ssr1PinHigh;
  out["ssr2PinHigh"] = state.ssr2PinHigh;
  out["robotDynPinHigh"] = state.robotDynPinHigh;
  setNumber("outputPowerW", state.outputPowerW);
  setNumber("commandPercent", state.commandPercent);
  out["automateState"] = state.automateState;
  out["previousAutomateState"] = state.previousAutomateState;
  out["automateReason"] = state.automateReason;
  out["activePriority"] = state.activePriority;
  out["commandSource"] = state.commandSource;
  out["pidAllowed"] = state.pidAllowed;
  out["routingAllowed"] = state.routingAllowed;
  out["safetyActive"] = state.safetyActive;
  out["errorActive"] = state.errorActive;
  out["automateLastTransitionMs"] = state.automateLastTransitionMs;
  out["ruleCount"] = state.ruleCount;
  out["activeRule"] = state.activeRule;
  out["manualCommandPending"] = state.manualCommandPending;
  out["manualCommandSource"] = state.manualCommandSource;
  out["manualCommandReason"] = state.manualCommandReason;
  out["manualAllOffPending"] = state.manualAllOffPending;
  JsonObject automate = out["automate"].to<JsonObject>();
  automate["state"] = state.automateState;
  automate["previousState"] = state.previousAutomateState;
  automate["reason"] = state.automateReason;
  automate["activePriority"] = state.activePriority;
  automate["commandSource"] = state.commandSource;
  automate["commandPercent"] = state.commandPercent;
  automate["pidAllowed"] = state.pidAllowed;
  automate["routingAllowed"] = state.routingAllowed;
  automate["safetyActive"] = state.safetyActive;
  automate["errorActive"] = state.errorActive;
  automate["lastTransitionMs"] = state.automateLastTransitionMs;
  automate["ruleCount"] = state.ruleCount;
  automate["activeRule"] = state.activeRule;
  automate["manualCommandPending"] = state.manualCommandPending;
  automate["manualCommandSource"] = state.manualCommandSource;
  automate["manualCommandReason"] = state.manualCommandReason;
  automate["manualAllOffPending"] = state.manualAllOffPending;
  setNumber("pidOutputPercent", state.pidOutputPercent);
  setNumber("pidErrorW", state.pidErrorW);
  out["pidEnabled"] = state.pidEnabled;
  out["pidStatus"] = state.pidStatus;
  actuators.runtimeToJson(out["actuators"].to<JsonArray>());
  const String outputActuatorId = state.commandActuatorId.length() && state.commandActuatorId != "*"
      ? state.commandActuatorId : config.routingActuatorId();
  float displayedOutputW = NAN;
  String outputPowerSource = "UNAVAILABLE";
  for (JsonObject item : out["actuators"].as<JsonArray>()) {
    String id = item["id"] | "";
    const float measured = sensors.jsyRoleValue("load", "POWER", id);
    setObjectNumber(item, "measuredPowerW", measured);
    item["measurementAvailable"] = isfinite(measured);
    if (id != outputActuatorId) continue;
    if (isfinite(measured)) {
      displayedOutputW = measured;
      outputPowerSource = "JSY";
    } else {
      // Estimate from the applied actuator command, not the PID running behind a manual override.
      displayedOutputW = item["estimatedPowerW"].isNull() ? NAN : item["estimatedPowerW"].as<float>();
      outputPowerSource = "ESTIMATED_ACTUATOR";
    }
  }
  setNumber("outputPowerW", displayedOutputW);
  out["outputPowerSource"] = outputPowerSource;
  out["outputPowerActuatorId"] = outputActuatorId;
  setNumber("jsyRoleGridPowerW", sensors.jsyRoleValue("grid", "POWER"));
  setNumber("jsyLoadPowerW", sensors.jsyRoleValue("load", "POWER"));
  JsonObject router = system["router"].as<JsonObject>();
  setNumber("pidMeasuredW", state.pidMeasuredW);
  setNumber("gridSetpointW", isfinite(state.pidGridSetpointW) ? state.pidGridSetpointW : (router["gridSetpointW"] | 0.0f));
  setNumber("deadbandW", router["deadbandW"] | 30.0f);
  setNumber("pidKp", state.pidKp);
  setNumber("pidKi", state.pidKi);
  setNumber("pidKd", state.pidKd);
  setNumber("maxOutputRampPercentPerSecond", state.pidRamp);
  setNumber("maxOutputDropPercentPerSecond", state.pidDrop);
  setNumber("outputMaxPowerW", router["outputMaxPowerW"] | router["ssr1MaxW"] | 1500.0f);
  JsonObject energy = out["energy"].to<JsonObject>();
  setObjectNumber(energy, "productionW", state.productionW);
  setObjectNumber(energy, "gridPowerW", state.gridPowerW);
  setObjectNumber(energy, "outputPowerW", displayedOutputW);
  setObjectNumber(energy, "outputPercent", state.commandPercent);
  setObjectNumber(energy, "primaryTemperatureC", state.temperature1C);
  setObjectNumber(energy, "surplusW", state.surplusW);
  setObjectNumber(energy, "consumptionW", state.consumptionW);

  JsonObject pid = out["pid"].to<JsonObject>();
  pid["contextId"] = state.pidContextId;
  pid["active"] = state.pidActive && state.commandSource == "RULE_ENGINE" &&
                  state.pidAllowed && state.pidEnabled && !state.safetyTripped;
  pid["mode"] = router["mode"] | (state.pidEnabled ? "AUTO" : "OFF");
  pid["source"] = state.pidSensorSource;
  setObjectNumber(pid, "targetGridW", isfinite(state.pidGridSetpointW) ? state.pidGridSetpointW : (router["gridSetpointW"] | 0.0f));
  setObjectNumber(pid, "deadbandW", state.pidDeadbandW);
  rules.pidToJson(out["pids"].to<JsonArray>());
  setObjectNumber(pid, "errorW", state.pidErrorW);
  setObjectNumber(pid, "outputPercent", state.pidOutputPercent);
  setObjectNumber(pid, "commandPercent", state.commandPercent);
  pid["status"] = state.pidStatus.length() ? state.pidStatus : "IDLE";
  setObjectNumber(pid, "outputPowerW", displayedOutputW);

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
        setObjectNumber(values, "CH1_APPARENT", state.jsyOnline && state.voltageV1 > 1.0f ? state.voltageV1 * state.currentA1 : NAN);
        setObjectNumber(values, "CH1_POWER", state.activePowerW1);
        setObjectNumber(values, "CH1_PF", state.powerFactor1);
        setObjectNumber(values, "CH1_ENERGY_POS", state.jsyImportEnergyWh1);
        setObjectNumber(values, "CH1_ENERGY_NEG", state.jsyExportEnergyWh1);
        values["CH1_DIR"] = state.energyDirection1;
        setObjectNumber(values, "CH2_CURR", state.currentA2);
        setObjectNumber(values, "CH2_APPARENT", state.jsyOnline && state.voltageV2 > 1.0f ? state.voltageV2 * state.currentA2 : NAN);
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
  sensors.mqttProductionSensorToJson(activeSensors);
  sensors.mqttProductionToJson(out["mqttProduction"].to<JsonObject>());

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
  JsonObject deviceOut = out["device"].to<JsonObject>();
  deviceOut["name"] = out["deviceName"];
  out["firmwareVersion"] = ROUTEUR_FIRMWARE_VERSION;
  out["littleFsVersion"] = readLittleFsTextFile("/www/littlefs_version.txt");
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
  out["safetyLevel"] = state.safetyLevel.length() ? state.safetyLevel : "N/A";
  out["safetyReason"] = state.safetyReason.length() ? state.safetyReason : "N/A";

  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *boot = esp_ota_get_boot_partition();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
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
  ota["runningInfo"] = BootManager::getRunningPartitionInfo();
  ota["bootInfo"] = BootManager::getBootPartitionInfo();
  ota["mainInfo"] = BootManager::getMainPartitionInfo();
  ota["sketchSize"] = ESP.getSketchSize();
  ota["freeSketchSpace"] = ESP.getFreeSketchSpace();
  ota["runningSlotSize"] = running ? running->size : 0;
  ota["runningRemainingBytes"] = running && running->size > ESP.getSketchSize() ? running->size - ESP.getSketchSize() : 0;
  appendOtaPartitionJson(ota["factory"].to<JsonObject>(), factory, running, boot);
  appendOtaPartitionJson(ota["app0"].to<JsonObject>(), app0, running, boot);
  appendOtaPartitionJson(ota["app1"].to<JsonObject>(), app1, running, boot);

  JsonObject storage = out["storage"].to<JsonObject>();
  storage["type"] = "LittleFS";
  storage["version"] = readLittleFsTextFile("/www/littlefs_version.txt");
  storage["total"] = LittleFS.totalBytes();
  storage["used"] = LittleFS.usedBytes();
  storage["status"] = state.littleFsOk || LittleFS.totalBytes() > 0 ? "OK" : "Erreur";

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
  html += F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LittleFS</title><style>body{background:#111827;color:#F9FAFB;font-family:system-ui;padding:18px}a{color:#93C5FD}pre{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:12px;white-space:pre-wrap}</style></head><body><h1>LittleFS</h1><p><a href=\"/app\">Retour app</a> <a href=\"/api/fs\">JSON</a></p><pre id=\"out\">Chargement...</pre><script>fetch('/api/fs').then(r=>r.text()).then(t=>{try{out.textContent=JSON.stringify(JSON.parse(t),null,2)}catch(e){out.textContent=t}}).catch(e=>out.textContent=e.message)</script></body></html>");
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
  bool allowEmpty = false;
  if (strcmp(name, "sensors") == 0) {
    allowEmpty = server.arg("allowEmptySensors") == "true" || server.arg("allowEmptySensors") == "1";
    String deleteId = server.arg("deleteId");
    if (deleteId.length()) {
      Serial.print(F("SENSOR DELETE requested id="));
      Serial.println(deleteId);
    }
  } else if (strcmp(name, "actuators") == 0) {
    allowEmpty = server.arg("allowEmptyActuators") == "true" || server.arg("allowEmptyActuators") == "1";
    String deleteId = server.arg("deleteId");
    if (deleteId.length()) {
      Serial.print(F("ACTUATOR DELETE requested id="));
      Serial.println(deleteId);
    }
  }
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
      size_t oldCount = config.sensorsDoc()["sensors"].as<JsonArray>().size() + config.sensorsDoc()["ds18b20"].as<JsonArray>().size();
      size_t newCount = check["sensors"].as<JsonArray>().size() + check["ds18b20"].as<JsonArray>().size();
      if (oldCount > 0 && newCount == 0 && !allowEmpty) {
        Serial.println(F("SENSOR SAVE refused reason=empty_without_confirmation"));
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Liste capteurs vide refusee sans allowEmptySensors=true\"}");
        return;
      }
    } else if (strcmp(name, "actuators") == 0) {
      size_t oldCount = config.actuatorsDoc()["actuators"].as<JsonArray>().size();
      size_t newCount = check["actuators"].as<JsonArray>().size();
      if (oldCount > 0 && newCount == 0 && !allowEmpty) {
        Serial.println(F("ACTUATOR SAVE refused reason=empty_without_confirmation"));
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Liste actionneurs vide refusee sans allowEmptyActuators=true\"}");
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
  size_t oldCount = 0;
  size_t newCount = 0;
  if (strcmp(name, "sensors") == 0) {
    DynamicJsonDocument check(24576);
    if (!deserializeJson(check, server.arg("plain"))) {
      oldCount = config.sensorsDoc()["sensors"].as<JsonArray>().size() + config.sensorsDoc()["ds18b20"].as<JsonArray>().size();
      newCount = check["sensors"].as<JsonArray>().size() + check["ds18b20"].as<JsonArray>().size();
    }
  } else if (strcmp(name, "actuators") == 0) {
    DynamicJsonDocument check(12288);
    if (!deserializeJson(check, server.arg("plain"))) {
      oldCount = config.actuatorsDoc()["actuators"].as<JsonArray>().size();
      newCount = check["actuators"].as<JsonArray>().size();
    }
  }
  bool allowWifiClear = strcmp(name, "system") == 0 && (server.arg("clearWifi") == "true" || server.arg("clearWifi") == "1");
  bool ok = config.replaceFile(path, server.arg("plain"), allowEmpty, allowWifiClear);
  if (ok && strcmp(name, "rules") == 0) rules.loadRules();
  if (ok && strcmp(name, "device") == 0) state.begin(config);
  if (ok && strcmp(name, "system") == 0) {
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
  if (strcmp(name, "sensors") == 0) {
    Serial.print(F("SENSOR SAVE oldCount="));
    Serial.print(oldCount);
    Serial.print(F(" newCount="));
    Serial.print(newCount);
    Serial.print(F(" result="));
    Serial.println(ok ? F("ok") : F("refused"));
  } else if (strcmp(name, "actuators") == 0) {
    Serial.print(F("ACTUATOR SAVE oldCount="));
    Serial.print(oldCount);
    Serial.print(F(" newCount="));
    Serial.print(newCount);
    Serial.print(F(" result="));
    Serial.println(ok ? F("ok") : F("refused"));
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
  html.reserve(1800);
  html += F("<!DOCTYPE html><html lang=\"fr\"><head><meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>RouteurSolaireESP32 Recovery</title>");
  html += F("<style>body{margin:0;background:#111827;color:#F9FAFB;font-family:system-ui,Arial,sans-serif;display:grid;min-height:100vh;place-items:center;padding:18px}.card{width:min(520px,100%);background:#1F2937;border:1px solid #374151;border-radius:8px;padding:18px}h1{margin:0 0 8px}.muted{color:#9CA3AF}.kv{display:grid;grid-template-columns:110px 1fr;gap:6px 10px;margin:14px 0}.kv span:nth-child(odd){color:#9CA3AF}.danger,a{display:inline-block;margin:6px 6px 0 0;background:#3b1111;color:#F9FAFB;border:1px solid #F44336;border-radius:8px;padding:10px 12px;text-decoration:none}a{background:#263244;border-color:#374151}</style>");
  html += F("</head><body><main class=\"card\"><h1>Passage en recovery</h1><p class=\"muted\">Page embarquee dans le firmware, independante de LittleFS.</p><div class=\"kv\"><span>IP box</span><span>");
  html += state.stationIp;
  html += F("</span><span>IP AP</span><span>");
  html += state.apIp;
  html += F("</span><span>WiFi</span><span>");
  html += state.networkMode;
  html += F("</span></div><form method=\"POST\" action=\"/api/system/recovery\" onsubmit=\"return confirm('Redemarrer en recovery ?')\"><input class=\"danger\" type=\"submit\" value=\"Passer en recovery\"></form><a href=\"/app\">Retour /app</a></main></body></html>");
  return html;
}

String WebUi::litePage() {
  return R"HTML(<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RouteurSolaireESP32 Lite</title>
<style>
body{margin:0;background:#111827;color:#F9FAFB;font-family:system-ui,Segoe UI,sans-serif}nav{background:#050505;padding:12px;display:flex;gap:8px;flex-wrap:wrap;position:sticky;top:0}button{background:#1F2937;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:10px 12px}main{padding:14px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:10px}.card{background:#1F2937;border:1px solid #374151;border-radius:8px;padding:12px}.label{color:#9CA3AF;font-size:12px}.value{font-size:22px;margin-top:6px}.ok{color:#22C55E}.bad{color:#F44336}.warn{color:#FF9800}input,select{width:100%;background:#0B1220;color:#F9FAFB;border:1px solid #374151;border-radius:8px;padding:9px}.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}.banner{background:#7f1d1d;border:1px solid #F44336;border-radius:8px;padding:10px;margin:8px 0}.sim{background:#2a1b08;border:1px solid #FF9800;border-radius:8px;padding:10px;margin:8px 0}
</style></head><body>
<nav><button onclick="show('dash')">Dashboard</button><button onclick="show('wifi')">WiFi</button><button onclick="location.href='/app'">Interface principale</button><button onclick="location.href='/recovery'">Recovery</button></nav>
<main id="v">Chargement...</main>
<script>
let status={}, current='dash';
function q(s){return document.querySelector(s)}
function esc(v){return String(v==null?'':v).replace(/[&<>"']/g,function(m){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]})}
function fmt(v){return typeof v==='number'?Math.round(v*10)/10:v}
async function api(p,o){let r=await fetch(p,o);let t=await r.text();if(!r.ok)throw new Error('HTTP '+r.status+' '+t);try{return JSON.parse(t)}catch(e){return t}}
function card(l,v,u,c){return '<div class="card"><div class="label">'+l+'</div><div class="value '+(c||'')+'">'+esc(fmt(v))+' '+(u||'')+'</div></div>'}
function emptyStatus(){return {networkMode:'inconnu',localIp:'-',stationIp:'-',apIp:'192.168.4.1',wifiConnected:false,wifiSsid:'-',rssi:0,injectionW:0,gridPowerW:0,ssr1PowerPct:0,ssr2PowerPct:0,robotDynPowerPct:0,temperature1C:0,temperature2C:0,temperature3C:0,heapFree:0}}
async function load(){try{status=await api('/api/status-lite')}catch(e){status=emptyStatus();status.loadError=e.message}}
function top(){return status.safetyTripped?'<div class="banner">SECURITE: '+esc(status.safetyReason)+'</div>':''}
async function show(p){current=p;await render()}
async function render(){q('#v').innerHTML='<h2>RouteurSolaireESP32</h2><p>Chargement API...</p>';await load();let h='<h2>RouteurSolaireESP32</h2>'+(status.loadError?'<div class="banner">Erreur API: '+esc(status.loadError)+'<br>Teste /api/ping puis /api/status</div>':'')+top();if(current==='dash')h+=dash();if(current==='wifi')h+=wifi();q('#v').innerHTML=h}
function dash(){return '<div class="grid">'+card('Mode reseau',status.networkMode,'')+card('IP box',status.stationIp||status.localIp,'')+card('IP AP',status.apIp||'192.168.4.1','')+card('Injection',status.injectionW,'W','ok')+card('Reseau',status.gridPowerW,'W')+card('SSR1',status.ssr1PowerPct,'%')+card('SSR2',status.ssr2PowerPct,'%')+card('RobotDyn',status.robotDynPowerPct,'%')+card('Sonde1',status.temperature1C,'C')+card('Sonde2',status.temperature2C,'C')+card('Sonde3',status.temperature3C,'C')+card('Heap',status.heapFree,'o')+'</div>'}
function wifi(){return '<div class="grid">'+card('Mode',status.networkMode,'')+card('WiFi connecte',status.wifiConnected,'')+card('SSID',status.wifiSsid,'')+card('RSSI',status.rssi,'')+card('IP box',status.stationIp,'')+card('IP AP',status.apIp,'')+'</div><p>Si tu es connecte au point d&#39;acces local RouteurSolaire_Config, ouvre http://192.168.4.1</p>'}
setInterval(function(){if(current==='dash')render()},5000);render();
</script></body></html>
)HTML";
}
