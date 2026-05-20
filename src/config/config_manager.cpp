#include "config_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include "../build_info.h"

static const char *DEVICE_PATH = "/config/device.json";
static const char *SYSTEM_PATH = "/config/system.json";
static const char *SENSORS_PATH = "/config/sensors.json";
static const char *ACTUATORS_PATH = "/config/actuators.json";
static const char *RULES_PATH = "/config/rules.json";
static const uint16_t CONFIG_VERSION = 2;
static const char *FIRMWARE_VERSION = ROUTEUR_FIRMWARE_VERSION;
static const char *DEFAULT_AP_SSID = "RouteurSolaire_Config";
static const char *DEFAULT_AP_PASSWORD = "routeur1234";
static const char *CONFIG_BACKUP_NAMESPACE = "rs_cfg_bak";
static const size_t CONFIG_BACKUP_CHUNK_SIZE = 850;

static bool isConfigPlaceholder(const char *value) {
  if (!value || !value[0]) return true;
  String text(value);
  text.toUpperCase();
  return text.indexOf("A_CONFIGURER") >= 0 || text.indexOf("ACONFIGURER") >= 0;
}

static bool putChunkedString(Preferences &prefs, const char *prefix, const String &value) {
  const uint16_t chunks = (value.length() + CONFIG_BACKUP_CHUNK_SIZE - 1) / CONFIG_BACKUP_CHUNK_SIZE;
  String countKey = String(prefix) + "_cnt";
  if (!prefs.putUShort(countKey.c_str(), chunks)) return false;
  for (uint16_t i = 0; i < chunks; i++) {
    String key = String(prefix) + "_" + String(i);
    String part = value.substring(i * CONFIG_BACKUP_CHUNK_SIZE, min(value.length(), static_cast<size_t>((i + 1) * CONFIG_BACKUP_CHUNK_SIZE)));
    if (!prefs.putString(key.c_str(), part)) return false;
  }
  return true;
}

static String getChunkedString(Preferences &prefs, const char *prefix) {
  String countKey = String(prefix) + "_cnt";
  uint16_t chunks = prefs.getUShort(countKey.c_str(), 0);
  String value;
  for (uint16_t i = 0; i < chunks; i++) {
    String key = String(prefix) + "_" + String(i);
    value += prefs.getString(key.c_str(), "");
  }
  return value;
}

static bool writeTextFile(const char *path, const String &value) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  size_t written = file.print(value);
  file.flush();
  file.close();
  return written == value.length();
}

bool ConfigManager::begin() {
  bool ok = loadAll();
  if (normalizeSystemConfig()) {
    saveSystem();
    Serial.println(F("Configuration systeme normalisee."));
  }
  JsonObject display = systemConfig["display"].as<JsonObject>();
  if (!display.isNull() && (display["mosi"] | 19) == 23) {
    display["mosi"] = 19;
    saveSystem();
    Serial.println(F("Configuration ecran normalisee."));
  }
  if (!display.isNull() && (display["refreshMs"] | 4000) < 3000) {
    display["refreshMs"] = 4000;
    saveSystem();
    Serial.println(F("Rafraichissement ecran ralenti."));
  }
  if (normalizeSensorsConfig()) {
    saveSensors();
    Serial.println(F("Configuration capteurs normalisee."));
  }
  if (normalizeActuatorsConfig()) {
    saveActuators();
    Serial.println(F("Configuration actionneurs normalisee."));
  }
  printConfigSummary();
  return ok;
}

bool ConfigManager::loadAll() {
  ensureConfigDir();
  restoreFromNvsAfterLittleFsOta();
  bool ok = true;
  ok &= loadOrDefault(DEVICE_PATH, deviceConfig, &ConfigManager::defaultDevice);
  ok &= loadOrDefault(SYSTEM_PATH, systemConfig, &ConfigManager::defaultSystem);
  ok &= loadOrDefault(SENSORS_PATH, sensorsConfig, &ConfigManager::defaultSensors);
  ok &= loadOrDefault(ACTUATORS_PATH, actuatorsConfig, &ConfigManager::defaultActuators);
  ok &= loadOrDefault(RULES_PATH, rulesConfig, &ConfigManager::defaultRules);
  return ok;
}

bool ConfigManager::saveAll() {
  return saveDevice() && saveSystem() && saveSensors() && saveActuators() && saveRules();
}

bool ConfigManager::saveDevice() { return saveDoc(DEVICE_PATH, deviceConfig); }
bool ConfigManager::saveSystem() { return saveDoc(SYSTEM_PATH, systemConfig); }
bool ConfigManager::saveSensors() { return saveDoc(SENSORS_PATH, sensorsConfig); }
bool ConfigManager::saveActuators() { return saveDoc(ACTUATORS_PATH, actuatorsConfig); }
bool ConfigManager::saveRules() { return saveDoc(RULES_PATH, rulesConfig); }
bool ConfigManager::saveDeviceConfig() { return saveDevice(); }
bool ConfigManager::saveSystemConfig() { return saveSystem(); }
bool ConfigManager::saveSensorsConfig() { return saveSensors(); }
bool ConfigManager::saveActuatorsConfig() { return saveActuators(); }
bool ConfigManager::saveRulesConfig() { return saveRules(); }

bool ConfigManager::resetToDefaults() {
  defaultDevice();
  defaultSystem();
  defaultSensors();
  defaultActuators();
  defaultRules();
  return saveAll();
}

bool ConfigManager::backupToNvsBeforeLittleFsOta() {
  configLastError = "";
  Preferences prefs;
  if (!prefs.begin(CONFIG_BACKUP_NAMESPACE, false)) {
    configLastError = "Impossible d'ouvrir NVS pour sauvegarde config";
    return false;
  }

  prefs.clear();
  bool ok = true;
  String payload;

  payload = "";
  serializeJson(deviceConfig, payload);
  ok = ok && putChunkedString(prefs, "dev", payload);

  payload = "";
  serializeJson(systemConfig, payload);
  ok = ok && putChunkedString(prefs, "sys", payload);

  payload = "";
  serializeJson(sensorsConfig, payload);
  ok = ok && putChunkedString(prefs, "sen", payload);

  payload = "";
  serializeJson(actuatorsConfig, payload);
  ok = ok && putChunkedString(prefs, "act", payload);

  payload = "";
  serializeJson(rulesConfig, payload);
  ok = ok && putChunkedString(prefs, "rul", payload);

  if (ok) {
    prefs.putBool("pending", true);
    prefs.putString("fw", ROUTEUR_FIRMWARE_VERSION);
  } else {
    prefs.putBool("pending", false);
    configLastError = "Sauvegarde NVS incomplete avant OTA LittleFS";
  }
  prefs.end();
  return ok;
}

bool ConfigManager::restoreFromNvsAfterLittleFsOta() {
  Preferences prefs;
  if (!prefs.begin(CONFIG_BACKUP_NAMESPACE, false)) return false;
  bool pending = prefs.getBool("pending", false);
  if (!pending) {
    prefs.end();
    return false;
  }

  ensureConfigDir();
  struct RestoreItem {
    const char *prefix;
    const char *path;
  };
  const RestoreItem items[] = {
      {"dev", DEVICE_PATH},
      {"sys", SYSTEM_PATH},
      {"sen", SENSORS_PATH},
      {"act", ACTUATORS_PATH},
      {"rul", RULES_PATH},
  };

  bool ok = true;
  for (const RestoreItem &item : items) {
    String json = getChunkedString(prefs, item.prefix);
    if (!json.length()) {
      ok = false;
      continue;
    }
    DynamicJsonDocument check(32768);
    DeserializationError err = deserializeJson(check, json);
    if (err || check.overflowed() || !validateDoc(item.path, check)) {
      ok = false;
      continue;
    }
    ok = writeTextFile(item.path, json) && ok;
  }

  if (ok) {
    prefs.clear();
    Serial.println(F("Configuration restauree depuis NVS apres OTA LittleFS."));
  } else {
    prefs.putBool("pending", false);
    Serial.println(F("Restauration NVS incomplete apres OTA LittleFS."));
  }
  prefs.end();
  return ok;
}

JsonObject ConfigManager::device() { return deviceConfig.as<JsonObject>(); }
JsonObject ConfigManager::system() { return systemConfig.as<JsonObject>(); }
JsonArray ConfigManager::sensors() { return sensorsConfig["sensors"].as<JsonArray>(); }
JsonArray ConfigManager::actuators() { return actuatorsConfig["actuators"].as<JsonArray>(); }
JsonArray ConfigManager::rules() { return rulesConfig["rules"].as<JsonArray>(); }

bool ConfigManager::replaceFile(const char *path, const String &json) {
  configLastError = "";
  DynamicJsonDocument temp(32768);
  DeserializationError err = deserializeJson(temp, json);
  if (err) {
    configLastError = String("JSON invalide: ") + err.c_str();
    return false;
  }
  if (temp.overflowed()) {
    configLastError = "Memoire JSON insuffisante pour charger la configuration";
    return false;
  }
  if (!validateDoc(path, temp)) {
    configLastError = String("Structure invalide pour ") + path;
    return false;
  }
  if (strcmp(path, DEVICE_PATH) == 0) deviceConfig = temp;
  else if (strcmp(path, SYSTEM_PATH) == 0) systemConfig = temp;
  else if (strcmp(path, SENSORS_PATH) == 0) sensorsConfig = temp;
  else if (strcmp(path, ACTUATORS_PATH) == 0) actuatorsConfig = temp;
  else if (strcmp(path, RULES_PATH) == 0) rulesConfig = temp;
  else {
    configLastError = String("Chemin config inconnu: ") + path;
    return false;
  }
  return saveDoc(path, temp);
}

bool ConfigManager::loadOrDefault(const char *path, DynamicJsonDocument &doc, void (ConfigManager::*defaults)()) {
  configLastError = "";
  if (!LittleFS.exists(path)) {
    (this->*defaults)();
    return saveDoc(path, doc);
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    backupCorruptFile(path);
    (this->*defaults)();
    return saveDoc(path, doc);
  }
  DeserializationError err = deserializeJson(doc, file);
  if (err) {
    backupCorruptFile(path);
    (this->*defaults)();
    return saveDoc(path, doc);
  }
  if (!doc["version"].is<int>()) {
    doc["version"] = CONFIG_VERSION;
    saveDoc(path, doc);
  }
  if (!validateDoc(path, doc)) {
    backupCorruptFile(path);
    (this->*defaults)();
    return saveDoc(path, doc);
  }
  return true;
}

bool ConfigManager::saveDoc(const char *path, DynamicJsonDocument &doc) {
  configLastError = "";
  if (doc.overflowed()) {
    configLastError = String("Document JSON trop grand pour ") + path;
    Serial.println(configLastError);
    return false;
  }
  ensureConfigDir();
  File file = LittleFS.open(path, "w");
  if (!file) {
    configLastError = String("Impossible d'ouvrir en ecriture: ") + path;
    Serial.println(configLastError);
    return false;
  }
  size_t written = serializeJsonPretty(doc, file);
  file.flush();
  file.close();
  if (written == 0) {
    configLastError = String("Ecriture JSON vide: ") + path;
    Serial.println(configLastError);
    return false;
  }
  return true;
}

void ConfigManager::ensureConfigDir() {
  if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
}

bool ConfigManager::validateDoc(const char *path, DynamicJsonDocument &doc) {
  if (!doc.is<JsonObject>()) return false;
  if (!doc["version"].is<int>()) return false;
  if (strcmp(path, DEVICE_PATH) == 0) return doc["role"].is<const char *>() && doc["deviceName"].is<const char *>();
  if (strcmp(path, SYSTEM_PATH) == 0) return doc["wifiSsid"].is<const char *>() && doc["fallbackApSsid"].is<const char *>();
  if (strcmp(path, SENSORS_PATH) == 0) return doc["sensors"].is<JsonArray>() && doc["sensors"].size() > 0 && doc["oneWireBus"].is<JsonObject>() && doc["ds18b20"].is<JsonArray>();
  if (strcmp(path, ACTUATORS_PATH) == 0) return doc["actuators"].is<JsonArray>() && doc["actuators"].size() > 0;
  if (strcmp(path, RULES_PATH) == 0) return doc["rules"].is<JsonArray>() && doc["rules"].size() > 0;
  return false;
}

bool ConfigManager::normalizeSystemConfig() {
  bool changed = false;
  JsonObject root = systemConfig.as<JsonObject>();
  JsonObject wifi = root["wifi"].is<JsonObject>() ? root["wifi"].as<JsonObject>() : root["wifi"].to<JsonObject>();
  JsonObject ap = root["fallbackAp"].is<JsonObject>() ? root["fallbackAp"].as<JsonObject>() : root["fallbackAp"].to<JsonObject>();

  // Les identifiants WiFi maison restent volontairement vides tant que
  // l'utilisateur ne les a pas saisis depuis l'interface Web.
  if (isConfigPlaceholder(root["wifiSsid"] | "")) {
    root["wifiSsid"] = "";
    changed = true;
  }
  if (isConfigPlaceholder(root["wifiPassword"] | "")) {
    root["wifiPassword"] = "";
    changed = true;
  }
  if (isConfigPlaceholder(wifi["ssid"] | "")) {
    wifi["ssid"] = "";
    changed = true;
  }
  if (isConfigPlaceholder(wifi["password"] | "")) {
    wifi["password"] = "";
    changed = true;
  }

  // L'AP local doit toujours rester joignable, meme apres nettoyage des
  // fichiers de configuration ou apres une ancienne valeur placeholder.
  if (isConfigPlaceholder(root["fallbackApSsid"] | "")) {
    root["fallbackApSsid"] = DEFAULT_AP_SSID;
    changed = true;
  }
  if (isConfigPlaceholder(root["fallbackApPassword"] | "")) {
    root["fallbackApPassword"] = DEFAULT_AP_PASSWORD;
    changed = true;
  }
  if (isConfigPlaceholder(ap["ssid"] | "")) {
    ap["ssid"] = DEFAULT_AP_SSID;
    changed = true;
  }
  const char *apPassword = ap["password"] | "";
  if (isConfigPlaceholder(apPassword) || strlen(apPassword) < 8) {
    ap["password"] = DEFAULT_AP_PASSWORD;
    changed = true;
  }
  if (!ap["ip"].is<const char *>()) {
    ap["ip"] = root["fallbackIp"] | "192.168.4.1";
    changed = true;
  }
  if (!wifi["keepFallbackApAlwaysOn"].is<bool>()) {
    wifi["keepFallbackApAlwaysOn"] = true;
    changed = true;
  }
  if (!wifi["connectTimeoutMs"].is<uint32_t>()) {
    wifi["connectTimeoutMs"] = 8000;
    changed = true;
  }
  JsonObject ntp = root["ntp"].is<JsonObject>() ? root["ntp"].as<JsonObject>() : root["ntp"].to<JsonObject>();
  if (!ntp["enabled"].is<bool>()) {
    ntp["enabled"] = true;
    changed = true;
  }
  if (!ntp["server1"].is<const char *>() || isConfigPlaceholder(ntp["server1"] | "")) {
    ntp["server1"] = "pool.ntp.org";
    changed = true;
  }
  if (!ntp["server2"].is<const char *>() || isConfigPlaceholder(ntp["server2"] | "")) {
    ntp["server2"] = "time.nist.gov";
    changed = true;
  }
  if (!ntp["timezone"].is<const char *>() || isConfigPlaceholder(ntp["timezone"] | "")) {
    ntp["timezone"] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    changed = true;
  }
  JsonObject webAuth = root["webAuth"].is<JsonObject>() ? root["webAuth"].as<JsonObject>() : root["webAuth"].to<JsonObject>();
  if (!webAuth["enabled"].is<bool>()) {
    webAuth["enabled"] = true;
    changed = true;
  }
  if (!webAuth["username"].is<const char *>() || isConfigPlaceholder(webAuth["username"] | "")) {
    webAuth["username"] = "admin";
    changed = true;
  }
  if (!webAuth["password"].is<const char *>() || isConfigPlaceholder(webAuth["password"] | "") || strlen(webAuth["password"] | "") < 4) {
    webAuth["password"] = "routeur1234";
    changed = true;
  }
  JsonObject mqtt = root["mqtt"].is<JsonObject>() ? root["mqtt"].as<JsonObject>() : root["mqtt"].to<JsonObject>();
  if (!mqtt["enabled"].is<bool>()) {
    mqtt["enabled"] = false;
    changed = true;
  }
  if (!mqtt["host"].is<const char *>() || isConfigPlaceholder(mqtt["host"] | "")) {
    mqtt["host"] = "192.168.0.48";
    changed = true;
  }
  if (!mqtt["port"].is<uint16_t>()) {
    mqtt["port"] = 1883;
    changed = true;
  }
  if (!mqtt["clientId"].is<const char *>()) {
    mqtt["clientId"] = "RouteurSolaireESP32";
    changed = true;
  }
  if (!mqtt["baseTopic"].is<const char *>()) {
    mqtt["baseTopic"] = "routeurSolaire";
    changed = true;
  }
  if (!mqtt["username"].is<const char *>()) {
    mqtt["username"] = "";
    changed = true;
  }
  if (!mqtt["password"].is<const char *>()) {
    mqtt["password"] = "";
    changed = true;
  }
  if (!mqtt["publishIntervalMs"].is<uint32_t>()) {
    mqtt["publishIntervalMs"] = 5000;
    changed = true;
  }
  if (!mqtt["retain"].is<bool>()) {
    mqtt["retain"] = false;
    changed = true;
  }
  if (!mqtt["publishIndividualTopics"].is<bool>()) {
    mqtt["publishIndividualTopics"] = true;
    changed = true;
  }
  JsonObject mqttTopics = mqtt["topics"].is<JsonObject>() ? mqtt["topics"].as<JsonObject>() : mqtt["topics"].to<JsonObject>();
  const char *baseTopic = mqtt["baseTopic"] | "routeurSolaire";
  if (!mqttTopics["state"].is<const char *>()) {
    mqttTopics["state"] = String(baseTopic) + "/state";
    changed = true;
  }
  if (!mqttTopics["command"].is<const char *>()) {
    mqttTopics["command"] = String(baseTopic) + "/command";
    changed = true;
  }
  if (!mqttTopics["actuatorSet"].is<const char *>()) {
    mqttTopics["actuatorSet"] = String(baseTopic) + "/actuator/+/set";
    changed = true;
  }
  if (!mqttTopics["availability"].is<const char *>()) {
    mqttTopics["availability"] = String(baseTopic) + "/availability";
    changed = true;
  }
  JsonObject router = root["router"].is<JsonObject>() ? root["router"].as<JsonObject>() : root["router"].to<JsonObject>();
  if (!router["mode"].is<const char *>()) { router["mode"] = "AUTO"; changed = true; }
  if (!router["gridPowerSource"].is<const char *>()) { router["gridPowerSource"] = "JSY"; changed = true; }
  if (!router["linkyPowerFactorEstimate"].is<float>() && !router["linkyPowerFactorEstimate"].is<int>()) { router["linkyPowerFactorEstimate"] = 0.95; changed = true; }
  if (!router["pidEnabled"].is<bool>()) { router["pidEnabled"] = true; changed = true; }
  if (!router["gridSetpointW"].is<float>() && !router["gridSetpointW"].is<int>()) { router["gridSetpointW"] = 0; changed = true; }
  if (!router["deadbandW"].is<float>() && !router["deadbandW"].is<int>()) { router["deadbandW"] = 30; changed = true; }
  if (!router["alphaFilter"].is<float>() && !router["alphaFilter"].is<int>()) { router["alphaFilter"] = 0.25; changed = true; }
  if (!router["maxOutputRampPercentPerSecond"].is<float>() && !router["maxOutputRampPercentPerSecond"].is<int>()) { router["maxOutputRampPercentPerSecond"] = 15; changed = true; }
  if (!router["heaterMaxPowerW"].is<float>() && !router["heaterMaxPowerW"].is<int>()) { router["heaterMaxPowerW"] = router["ssr1MaxW"] | 1500; changed = true; }
  if (!router["jsyReadIntervalMs"].is<uint32_t>()) { router["jsyReadIntervalMs"] = 100; changed = true; }
  if (!router["voltageMinV"].is<float>() && !router["voltageMinV"].is<int>()) { router["voltageMinV"] = 180; changed = true; }
  if (!router["voltageMaxV"].is<float>() && !router["voltageMaxV"].is<int>()) { router["voltageMaxV"] = 260; changed = true; }
  if (!router["frequencyMinHz"].is<float>() && !router["frequencyMinHz"].is<int>()) { router["frequencyMinHz"] = 47; changed = true; }
  if (!router["frequencyMaxHz"].is<float>() && !router["frequencyMaxHz"].is<int>()) { router["frequencyMaxHz"] = 53; changed = true; }
  return changed;
}

bool ConfigManager::normalizeSensorsConfig() {
  bool changed = false;
  JsonObject root = sensorsConfig.as<JsonObject>();
  JsonObject bus = root["oneWireBus"].is<JsonObject>() ? root["oneWireBus"].as<JsonObject>() : root["oneWireBus"].to<JsonObject>();

  // Migration douce des anciens defauts du projet vers le pinout actuel.
  // Une valeur personnalisee differente n'est pas modifiee.
  int busGpio = bus["gpio"] | 13;
  if (busGpio == 4) {
    bus["gpio"] = 13;
    changed = true;
  }
  if (!bus["enabled"].is<bool>()) {
    bus["enabled"] = true;
    changed = true;
  }
  if (!bus["scanOnBoot"].is<bool>()) {
    bus["scanOnBoot"] = true;
    changed = true;
  }
  if (!bus["readIntervalMs"].is<uint32_t>()) {
    bus["readIntervalMs"] = 2000;
    changed = true;
  }

  JsonArray arr = root["sensors"].as<JsonArray>();
  for (JsonObject sensor : arr) {
    const char *id = sensor["id"] | "";
    const char *type = sensor["type"] | "";
    if (strcmp(id, "jsy_grid") == 0 || strcmp(type, "JSY-MK-194T") == 0) {
      int rx = sensor["rx"] | 26;
      int tx = sensor["tx"] | 27;
      if (rx == 16 && tx == 17) {
        sensor["rx"] = 26;
        sensor["tx"] = 27;
        changed = true;
      }
      if (!sensor["baudrate"].is<uint32_t>()) {
        sensor["baudrate"] = 4800;
        changed = true;
      }
      if (!sensor["modbusAddress"].is<int>()) {
        sensor["modbusAddress"] = 1;
        changed = true;
      }
      if (!sensor["readIntervalMs"].is<uint32_t>()) {
        sensor["readIntervalMs"] = 500;
        changed = true;
      }
      if (!sensor["timeoutMs"].is<uint32_t>()) {
        sensor["timeoutMs"] = 400;
        changed = true;
      }
      if (!sensor["rs485DirPin"].is<int>()) {
        sensor["rs485DirPin"] = -1;
        changed = true;
      }
      JsonArray channels = sensor["channels"].as<JsonArray>();
      if (channels.isNull() || channels.size() < 2) {
        channels = sensor["channels"].to<JsonArray>();
        JsonObject ch1 = channels.add<JsonObject>();
        ch1["id"] = "clamp1"; ch1["name"] = "Pince 1"; ch1["role"] = "production"; ch1["measures"].add("voltageV1"); ch1["measures"].add("currentA1"); ch1["measures"].add("activePowerW1"); ch1["measures"].add("powerFactor1");
        JsonObject ch2 = channels.add<JsonObject>();
        ch2["id"] = "clamp2"; ch2["name"] = "Pince 2"; ch2["role"] = "grid"; ch2["measures"].add("voltageV2"); ch2["measures"].add("currentA2"); ch2["measures"].add("activePowerW2"); ch2["measures"].add("powerFactor2");
        changed = true;
      } else {
        JsonObject ch1 = channels[0].as<JsonObject>();
        JsonObject ch2 = channels[1].as<JsonObject>();
        if (!ch1["id"].is<const char *>()) { ch1["id"] = "clamp1"; changed = true; }
        if (!ch2["id"].is<const char *>()) { ch2["id"] = "clamp2"; changed = true; }
        if (!ch1["name"].is<const char *>() || strlen(ch1["name"] | "") == 0) { ch1["name"] = "Pince 1"; changed = true; }
        if (!ch2["name"].is<const char *>() || strlen(ch2["name"] | "") == 0) { ch2["name"] = "Pince 2"; changed = true; }
        if (!ch1["measures"].is<JsonArray>()) {
          JsonArray m = ch1["measures"].to<JsonArray>();
          m.add("voltageV1"); m.add("currentA1"); m.add("activePowerW1"); m.add("powerFactor1");
          changed = true;
        }
        if (!ch2["measures"].is<JsonArray>()) {
          JsonArray m = ch2["measures"].to<JsonArray>();
          m.add("voltageV2"); m.add("currentA2"); m.add("activePowerW2"); m.add("powerFactor2");
          changed = true;
        }
        const char *role1 = ch1["role"] | "";
        const char *role2 = ch2["role"] | "";
        if (strcmp(role1, "grid") == 0 && strcmp(role2, "production") == 0) {
          ch1["role"] = "production";
          ch2["role"] = "grid";
          changed = true;
        }
      }
    } else if (strcmp(id, "tic_linky") == 0 || strcmp(type, "TIC Linky") == 0) {
      int rx = sensor["rx"] | 26;
      if (rx == 32) {
        sensor["rx"] = 26;
        changed = true;
      }
      if (!sensor["tx"].is<int>()) {
        sensor["tx"] = 27;
        changed = true;
      }
      if (!sensor["baudrate"].is<uint32_t>()) {
        const char *ticMode = sensor["mode"] | "historique";
        sensor["baudrate"] = strcmp(ticMode, "standard") == 0 ? 9600 : 1200;
        changed = true;
      }
      if (!sensor["timeoutMs"].is<uint32_t>()) {
        sensor["timeoutMs"] = 5000;
        changed = true;
      }
    }
  }
  return changed;
}

bool ConfigManager::normalizeActuatorsConfig() {
  bool changed = false;
  if (actuatorsConfig["simulationOutput"].is<JsonObject>()) {
    actuatorsConfig.remove("simulationOutput");
    changed = true;
  }
  JsonArray arr = actuatorsConfig["actuators"].as<JsonArray>();

  for (JsonObject actuator : arr) {
    String id = actuator["id"] | "";

    if (id == "ssr1_water_heater") {
      int gpio = actuator["gpio"] | 5;
      if (gpio == 26) {
        actuator["gpio"] = 5;
        changed = true;
      }
    } else if (id == "ssr2_aux") {
      int gpio = actuator["gpio"] | 17;
      if (gpio == 25) {
        actuator["gpio"] = 17;
        changed = true;
      }
    } else if (id == "robotdyn_triac") {
      int zc = actuator["zeroCross"] | -1;
      // Ancien defaut en conflit avec le TX JSY sur GPIO27. On desactive
      // le triac tant que ses vraies broches PCB ne sont pas renseignees.
      if (zc == 27) {
        actuator["zeroCross"] = -1;
        actuator["enabled"] = false;
        changed = true;
      }
    }
  }
  return changed;
}

void ConfigManager::backupCorruptFile(const char *path) {
  if (!LittleFS.exists(path)) return;
  String backup = String(path) + ".bak";
  if (LittleFS.exists(backup)) LittleFS.remove(backup);
  LittleFS.rename(path, backup);
  Serial.print(F("Config corrompue sauvegardee: "));
  Serial.println(backup);
}

void ConfigManager::printConfigSummary() {
  Serial.println(F("=== Config summary ==="));
  Serial.print(F("Device: "));
  Serial.print(device()["deviceName"] | device()["name"] | "?");
  Serial.print(F(" / role "));
  Serial.println(device()["role"] | "?");
  Serial.print(F("WiFi SSID: "));
  Serial.println(system()["wifiSsid"] | system()["wifi"]["ssid"] | "?");
  Serial.print(F("Sensors: "));
  Serial.println(sensors().size());
  Serial.print(F("Actuators: "));
  Serial.println(actuators().size());
  Serial.print(F("Rules: "));
  Serial.println(rules().size());
}

void ConfigManager::defaultDevice() {
  deviceConfig.clear();
  JsonObject root = deviceConfig.to<JsonObject>();
  root["version"] = CONFIG_VERSION;
  root["deviceId"] = "";
  root["deviceName"] = "Routeur solaire ESP32";
  root["name"] = "Routeur solaire ESP32";
  root["role"] = "MASTER";
  root["isConfigured"] = false;
  root["firmwareVersion"] = FIRMWARE_VERSION;
  root["criticalActuatorOwner"] = "";
}

void ConfigManager::defaultSystem() {
  systemConfig.clear();
  JsonObject root = systemConfig.to<JsonObject>();
  root["version"] = CONFIG_VERSION;
  root["wifiSsid"] = "";
  root["wifiPassword"] = "";
  root["fallbackApSsid"] = DEFAULT_AP_SSID;
  root["fallbackApPassword"] = DEFAULT_AP_PASSWORD;
  root["fallbackIp"] = "192.168.4.1";
  root["heartbeatIntervalMs"] = 300;
  root["takeoverTimeoutMs"] = 1000;
  root["simulationMode"] = false;
  JsonObject simulation = root["simulation"].to<JsonObject>();
  simulation["enabled"] = false;
  simulation["mode"] = "manual";
  simulation["scenario"] = "normal";
  simulation["randomUpdateIntervalMs"] = 2000;
  JsonObject safety = root["safety"].to<JsonObject>();
  safety["enabled"] = true;
  safety["blockOnMissingDs18b20"] = true;
  safety["blockOnMissingTopSensor"] = true;
  safety["blockOnMissingJsy"] = true;
  safety["blockOnMissingJsyAndTic"] = true;
  safety["warningOnlyOnMissingSensors"] = false;
  JsonObject wifi = root["wifi"].to<JsonObject>();
  wifi["ssid"] = "";
  wifi["password"] = "";
  wifi["keepFallbackApAlwaysOn"] = true;
  wifi["connectTimeoutMs"] = 8000;
  JsonObject ntp = root["ntp"].to<JsonObject>();
  ntp["enabled"] = true;
  ntp["server1"] = "pool.ntp.org";
  ntp["server2"] = "time.nist.gov";
  ntp["timezone"] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
  JsonObject ap = root["fallbackAp"].to<JsonObject>();
  ap["ssid"] = DEFAULT_AP_SSID;
  ap["password"] = DEFAULT_AP_PASSWORD;
  ap["ip"] = "192.168.4.1";
  JsonObject webAuth = root["webAuth"].to<JsonObject>();
  webAuth["enabled"] = true;
  webAuth["username"] = "admin";
  webAuth["password"] = "routeur1234";
  JsonObject mqtt = root["mqtt"].to<JsonObject>();
  mqtt["enabled"] = false;
  mqtt["host"] = "192.168.0.48";
  mqtt["port"] = 1883;
  mqtt["clientId"] = "RouteurSolaireESP32";
  mqtt["baseTopic"] = "routeurSolaire";
  mqtt["username"] = "";
  mqtt["password"] = "";
  mqtt["publishIntervalMs"] = 5000;
  mqtt["retain"] = false;
  mqtt["publishIndividualTopics"] = true;
  JsonObject mqttTopics = mqtt["topics"].to<JsonObject>();
  mqttTopics["state"] = "routeurSolaire/state";
  mqttTopics["command"] = "routeurSolaire/command";
  mqttTopics["actuatorSet"] = "routeurSolaire/actuator/+/set";
  mqttTopics["availability"] = "routeurSolaire/availability";
  JsonObject router = root["router"].to<JsonObject>();
  router["mode"] = "AUTO";
  router["gridPowerSource"] = "JSY";
  router["linkyPowerFactorEstimate"] = 0.95;
  router["injectionThresholdW"] = -200;
  router["minInjectionStartW"] = 200;
  router["stopBelowInjectionW"] = 80;
  router["hysteresisW"] = 40;
  router["tankMaxC"] = 65;
  router["tankSafetyC"] = 70;
  router["tempSafetyMaxC"] = 70;
  router["jsyTimeoutMs"] = 3000;
  router["ticTimeoutMs"] = 10000;
  router["ssr1MaxW"] = 1500;
  router["ssr2MaxW"] = 1000;
  router["robotDynMaxW"] = 1000;
  router["ssrCycleMs"] = 1000;
  router["commandTimeoutMs"] = 5000;
  router["pidEnabled"] = true;
  router["gridSetpointW"] = 0;
  router["deadbandW"] = 30;
  router["alphaFilter"] = 0.25;
  router["maxOutputRampPercentPerSecond"] = 15;
  router["heaterMaxPowerW"] = 1500;
  router["jsyReadIntervalMs"] = 100;
  router["voltageMinV"] = 180;
  router["voltageMaxV"] = 260;
  router["frequencyMinHz"] = 47;
  router["frequencyMaxHz"] = 53;
  router["kp"] = 0.08;
  router["ki"] = 0.01;
  router["kd"] = 0.0;
  JsonObject display = root["display"].to<JsonObject>();
  display["enabled"] = true;
  display["type"] = "SSD1309_SPI";
  display["sclk"] = 18;
  display["mosi"] = 19;
  display["reset"] = 16;
  display["dc"] = 4;
  display["cs"] = 15;
  display["refreshMs"] = 4000;
  root["debug"] = true;
  root["peers"].to<JsonArray>();
}

void ConfigManager::defaultSensors() {
  sensorsConfig.clear();
  sensorsConfig["version"] = CONFIG_VERSION;
  JsonObject bus = sensorsConfig["oneWireBus"].to<JsonObject>();
  bus["gpio"] = 13;
  bus["enabled"] = true;
  bus["scanOnBoot"] = true;
  bus["readIntervalMs"] = 2000;
  JsonArray arr = sensorsConfig["sensors"].to<JsonArray>();
  JsonObject s = arr.add<JsonObject>();
  s["id"] = "jsy_grid"; s["name"] = "JSY reseau"; s["type"] = "JSY-MK-194T"; s["source"] = "local";
  s["serial"] = "Serial2"; s["rx"] = 26; s["tx"] = 27; s["baudrate"] = 4800; s["modbusAddress"] = 1;
  s["readIntervalMs"] = 100; s["timeoutMs"] = 300; s["rs485DirPin"] = -1;
  s["role"] = "mesure reseau principal"; s["enabled"] = true;
  JsonArray channels = s["channels"].to<JsonArray>();
  JsonObject ch1 = channels.add<JsonObject>();
  ch1["id"] = "clamp1"; ch1["name"] = "Pince 1"; ch1["role"] = "production"; ch1["measures"].add("voltageV1"); ch1["measures"].add("currentA1"); ch1["measures"].add("activePowerW1"); ch1["measures"].add("powerFactor1");
  JsonObject ch2 = channels.add<JsonObject>();
  ch2["id"] = "clamp2"; ch2["name"] = "Pince 2"; ch2["role"] = "grid"; ch2["measures"].add("voltageV2"); ch2["measures"].add("currentA2"); ch2["measures"].add("activePowerW2"); ch2["measures"].add("powerFactor2");
  JsonObject t = arr.add<JsonObject>();
  t["id"] = "tic_linky"; t["name"] = "TIC Linky"; t["type"] = "TIC Linky"; t["source"] = "local";
  t["serial"] = "Serial1"; t["rx"] = 26; t["tx"] = 27; t["mode"] = "historique"; t["baudrate"] = 1200; t["timeoutMs"] = 5000;
  t["role"] = "compteur officiel / puissance reseau"; t["enabled"] = true;
  JsonArray ds = sensorsConfig["ds18b20"].to<JsonArray>();
  const char *ids[] = {"sonde1", "sonde2", "sonde3"};
  const char *names[] = {"Sonde 1", "Sonde 2", "Sonde 3"};
  const char *roles[] = {"ballon_haut", "ballon_milieu", "ballon_bas"};
  for (uint8_t i = 0; i < 3; i++) {
    JsonObject d = ds.add<JsonObject>();
    d["id"] = ids[i];
    d["name"] = names[i];
    d["role"] = roles[i];
    d["address"] = "";
    d["enabled"] = true;
    d["critical"] = i == 0;
    d["unit"] = "C";
    d["temperatureC"] = nullptr;
    d["available"] = false;
    d["lastReadMs"] = 0;
    d["errorCount"] = 0;
  }
}

void ConfigManager::defaultActuators() {
  actuatorsConfig.clear();
  actuatorsConfig["version"] = CONFIG_VERSION;
  JsonArray arr = actuatorsConfig["actuators"].to<JsonArray>();
  JsonObject a = arr.add<JsonObject>();
  a["id"] = "ssr1_water_heater"; a["name"] = "SSR1 chauffe-eau principal"; a["type"] = "SSR"; a["source"] = "local";
  a["gpio"] = 5; a["mode"] = "BURST_FIRE"; a["maxPowerW"] = 1500; a["cycleMs"] = 1000; a["critical"] = true; a["enabled"] = true;
  JsonObject b = arr.add<JsonObject>();
  b["id"] = "ssr2_aux"; b["name"] = "SSR2 auxiliaire"; b["type"] = "SSR"; b["source"] = "local";
  b["gpio"] = 17; b["mode"] = "BURST_FIRE"; b["maxPowerW"] = 1000; b["cycleMs"] = 1000; b["critical"] = true; b["enabled"] = true;
  JsonObject c = arr.add<JsonObject>();
  c["id"] = "robotdyn_triac"; c["name"] = "RobotDyn Triac"; c["type"] = "RobotDyn Triac"; c["source"] = "local";
  c["zeroCross"] = -1; c["control"] = 33; c["mode"] = "PHASE_ANGLE"; c["maxPowerW"] = 1000; c["critical"] = true; c["enabled"] = false;
}

void ConfigManager::defaultRules() {
  rulesConfig.clear();
  rulesConfig["version"] = CONFIG_VERSION;
  JsonArray arr = rulesConfig["rules"].to<JsonArray>();
  JsonObject r = arr.add<JsonObject>();
  r["id"] = "solar_routing_ssr1"; r["name"] = "Routage solaire chauffe-eau SSR1"; r["enabled"] = true; r["priority"] = 10; r["logic"] = "AND";
  JsonArray c = r["conditions"].to<JsonArray>();
  JsonObject c1 = c.add<JsonObject>(); c1["id"] = "cond_surplus"; c1["source"] = "JSY-MK-194T"; c1["measure"] = "injectionW"; c1["type"] = "number"; c1["operator"] = ">"; c1["value"] = 200; c1["unit"] = "W";
  JsonObject c2 = c.add<JsonObject>(); c2["id"] = "cond_sonde1_temp"; c2["source"] = "sonde1"; c2["measure"] = "temperatureC"; c2["type"] = "number"; c2["operator"] = "<"; c2["value"] = 65; c2["unit"] = "C";
  JsonObject c3 = c.add<JsonObject>(); c3["id"] = "cond_sonde2_temp"; c3["source"] = "sonde2"; c3["measure"] = "temperatureC"; c3["type"] = "number"; c3["operator"] = "<"; c3["value"] = 65; c3["unit"] = "C";
  JsonObject c4 = c.add<JsonObject>(); c4["id"] = "cond_sonde3_temp"; c4["source"] = "sonde3"; c4["measure"] = "temperatureC"; c4["type"] = "number"; c4["operator"] = "<"; c4["value"] = 65; c4["unit"] = "C";
  JsonObject c5 = c.add<JsonObject>(); c5["id"] = "cond_safety_ok"; c5["source"] = "Securite"; c5["measure"] = "safetyLevel"; c5["type"] = "enum"; c5["operator"] = "=="; c5["value"] = "OK";
  JsonArray a = r["actions"].to<JsonArray>();
  JsonObject a1 = a.add<JsonObject>(); a1["actuatorId"] = "ssr1_water_heater"; a1["command"] = "setPowerFromSurplus"; a1["maxHeaterPowerW"] = 1500;

  JsonObject s = arr.add<JsonObject>();
  s["id"] = "tank_temperature_safety"; s["name"] = "Securite temperature ballon"; s["enabled"] = true; s["priority"] = 100; s["logic"] = "OR";
  JsonArray sc = s["conditions"].to<JsonArray>();
  JsonObject sc1 = sc.add<JsonObject>(); sc1["id"] = "cond_safety_sonde1"; sc1["source"] = "sonde1"; sc1["measure"] = "temperatureC"; sc1["type"] = "number"; sc1["operator"] = ">="; sc1["value"] = 70; sc1["unit"] = "C";
  JsonObject sc2 = sc.add<JsonObject>(); sc2["id"] = "cond_safety_sonde2"; sc2["source"] = "sonde2"; sc2["measure"] = "temperatureC"; sc2["type"] = "number"; sc2["operator"] = ">="; sc2["value"] = 70; sc2["unit"] = "C";
  JsonObject sc3 = sc.add<JsonObject>(); sc3["id"] = "cond_safety_sonde3"; sc3["source"] = "sonde3"; sc3["measure"] = "temperatureC"; sc3["type"] = "number"; sc3["operator"] = ">="; sc3["value"] = 70; sc3["unit"] = "C";
  JsonArray sa = s["actions"].to<JsonArray>();
  JsonObject sa1 = sa.add<JsonObject>(); sa1["actuatorId"] = "ssr1_water_heater"; sa1["command"] = "stop"; sa1["value"] = 0;
  JsonObject sa2 = sa.add<JsonObject>(); sa2["actuatorId"] = "ssr2_aux"; sa2["command"] = "stop"; sa2["value"] = 0;
  JsonObject sa3 = sa.add<JsonObject>(); sa3["actuatorId"] = "robotdyn_triac"; sa3["command"] = "stop"; sa3["value"] = 0;
  JsonObject sa4 = sa.add<JsonObject>(); sa4["command"] = "setSafetyWarning"; sa4["message"] = "Temperature ballon depassee";
}
