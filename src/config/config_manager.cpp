#include "config_manager.h"
#include <LittleFS.h>
#include <Preferences.h>
#include "../build_info.h"
#include "../communication/espnow_protocol.h"

static const char *DEVICE_PATH = "/config/device.json";
static const char *SYSTEM_PATH = "/config/system.json";
static const char *SENSORS_PATH = "/config/sensors.json";
static const char *ACTUATORS_PATH = "/config/actuators.json";
static const char *RULES_PATH = "/config/rules.json";
static const char *BACKUP_DIR = "/config/backup";
static const char *DIAG_DIR = "/config/diagnostic";
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

static bool textPresent(const char *value) {
  return value && value[0] && !isConfigPlaceholder(value);
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

static String configBaseName(const char *path) {
  String text(path);
  int slash = text.lastIndexOf('/');
  return slash >= 0 ? text.substring(slash + 1) : text;
}

static String backupPathFor(const char *path) {
  return String(BACKUP_DIR) + "/" + configBaseName(path);
}

static String diagnosticPathFor(const char *path) {
  String base = configBaseName(path);
  base.replace(".json", "");
  return String(DIAG_DIR) + "/" + base + "_load_error.txt";
}

static bool copyLittleFsFile(const char *from, const char *to) {
  File src = LittleFS.open(from, "r");
  if (!src) return false;
  File dst = LittleFS.open(to, "w");
  if (!dst) {
    src.close();
    return false;
  }
  uint8_t buffer[256];
  while (src.available()) {
    size_t len = src.read(buffer, sizeof(buffer));
    if (len && dst.write(buffer, len) != len) {
      src.close();
      dst.close();
      return false;
    }
  }
  dst.flush();
  src.close();
  dst.close();
  return true;
}

bool ConfigManager::begin() {
  bool ok = loadAll();
  if (ok && systemWifiPresent(systemConfig)) {
    backupConfigToNvs();
  }
  if (normalizeSystemConfig()) {
    if (saveSystem()) Serial.println(F("Configuration systeme normalisee."));
    else Serial.println(F("Normalisation systeme non sauvegardee."));
  }
  JsonObject display = systemConfig["display"].as<JsonObject>();
  if (!display.isNull() && (display["mosi"] | 19) == 23) {
    display["mosi"] = 19;
    if (saveSystem()) Serial.println(F("Configuration ecran normalisee."));
    else Serial.println(F("Normalisation ecran non sauvegardee."));
  }
  if (!display.isNull() && (display["refreshMs"] | 4000) < 3000) {
    display["refreshMs"] = 4000;
    if (saveSystem()) Serial.println(F("Rafraichissement ecran ralenti."));
    else Serial.println(F("Normalisation refresh ecran non sauvegardee."));
  }
  if (normalizeSensorsConfig()) {
    if (saveSensors()) Serial.println(F("Configuration capteurs normalisee."));
    else Serial.println(F("Normalisation capteurs non sauvegardee."));
  }
  if (normalizeActuatorsConfig()) {
    if (saveActuators()) Serial.println(F("Configuration actionneurs normalisee."));
    else Serial.println(F("Normalisation actionneurs non sauvegardee."));
  }
  printConfigSummary();
  return ok;
}

bool ConfigManager::loadAll() {
  ensureConfigDir();
  restoreConfigFromNvs();
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

bool ConfigManager::backupConfigToNvs() {
  configLastError = "";
  if (!validateDoc(SYSTEM_PATH, systemConfig) || !systemWifiPresent(systemConfig)) {
    configLastError = "Sauvegarde NVS refusee: system.json invalide ou WiFi absent";
    Serial.println(configLastError);
    return false;
  }
  if (!validateDoc(DEVICE_PATH, deviceConfig) || !validateDoc(SENSORS_PATH, sensorsConfig) ||
      !validateDoc(ACTUATORS_PATH, actuatorsConfig) || !validateDoc(RULES_PATH, rulesConfig)) {
    configLastError = "Sauvegarde NVS refusee: une configuration est invalide";
    Serial.println(configLastError);
    return false;
  }
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
    prefs.putBool("valid", true);
    prefs.putBool("pending", true);
    prefs.putString("fw", ROUTEUR_FIRMWARE_VERSION);
    Serial.println(F("Sauvegarde NVS config valide creee."));
  } else {
    prefs.putBool("pending", false);
    prefs.putBool("valid", false);
    configLastError = "Sauvegarde NVS config incomplete";
  }
  prefs.end();
  return ok;
}

bool ConfigManager::restoreConfigFromNvs() {
  Preferences prefs;
  if (!prefs.begin(CONFIG_BACKUP_NAMESPACE, false)) return false;
  bool available = prefs.getBool("valid", false) || prefs.getBool("pending", false);
  if (!available) {
    Serial.println(F("Backup NVS config: absent."));
    prefs.end();
    return false;
  }
  Serial.println(F("Backup NVS config: present."));
  if (!shouldRestoreConfigFromNvs(prefs)) {
    Serial.println(F("Backup NVS config: restauration non necessaire."));
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
    prefs.putBool("valid", true);
    prefs.putBool("pending", false);
    Serial.println(F("Configuration restauree depuis NVS."));
  } else {
    prefs.putBool("pending", false);
    Serial.println(F("Restauration NVS config incomplete."));
  }
  prefs.end();
  return ok;
}

JsonObject ConfigManager::device() { return deviceConfig.as<JsonObject>(); }
JsonObject ConfigManager::system() { return systemConfig.as<JsonObject>(); }
JsonArray ConfigManager::sensors() { return sensorsConfig["sensors"].as<JsonArray>(); }
JsonArray ConfigManager::actuators() { return actuatorsConfig["actuators"].as<JsonArray>(); }
JsonArray ConfigManager::rules() { return rulesConfig["rules"].as<JsonArray>(); }

bool ConfigManager::replaceFile(const char *path, const String &json, bool allowEmpty, bool allowWifiClear) {
  configLastError = "";
  bool previousAllowEmpty = allowEmptyConfigWrite;
  bool previousAllowWifiClear = allowWifiClearConfigWrite;
  allowEmptyConfigWrite = allowEmpty;
  allowWifiClearConfigWrite = allowWifiClear;
  DynamicJsonDocument temp(32768);
  DeserializationError err = deserializeJson(temp, json);
  if (err) {
    configLastError = String("JSON invalide: ") + err.c_str();
    allowEmptyConfigWrite = previousAllowEmpty;
    allowWifiClearConfigWrite = previousAllowWifiClear;
    return false;
  }
  if (temp.overflowed()) {
    configLastError = "Memoire JSON insuffisante pour charger la configuration";
    allowEmptyConfigWrite = previousAllowEmpty;
    allowWifiClearConfigWrite = previousAllowWifiClear;
    return false;
  }
  if (strcmp(path, SYSTEM_PATH) == 0) {
    if (allowWifiClear) {
      Serial.println(F("WiFi config clear explicite autorise pour /config/system.json."));
    } else {
      preserveExistingWifiConfig(temp);
    }
    migrateSystemWifiConfig(temp, "save");
  }
  if (!validateDoc(path, temp)) {
    configLastError = String("Structure invalide pour ") + path;
    allowEmptyConfigWrite = previousAllowEmpty;
    allowWifiClearConfigWrite = previousAllowWifiClear;
    return false;
  }
  if (wouldEraseUserConfig(path, temp)) {
    configLastError = String("Sauvegarde refusee: risque d'ecraser une configuration utilisateur existante avec une configuration vide: ") + path;
    Serial.println(configLastError);
    allowEmptyConfigWrite = previousAllowEmpty;
    allowWifiClearConfigWrite = previousAllowWifiClear;
    return false;
  }
  backupUserConfigFile(path);
  if (strcmp(path, DEVICE_PATH) == 0) deviceConfig = temp;
  else if (strcmp(path, SYSTEM_PATH) == 0) systemConfig = temp;
  else if (strcmp(path, SENSORS_PATH) == 0) sensorsConfig = temp;
  else if (strcmp(path, ACTUATORS_PATH) == 0) actuatorsConfig = temp;
  else if (strcmp(path, RULES_PATH) == 0) rulesConfig = temp;
  else {
    configLastError = String("Chemin config inconnu: ") + path;
    allowEmptyConfigWrite = previousAllowEmpty;
    allowWifiClearConfigWrite = previousAllowWifiClear;
    return false;
  }
  bool ok = saveDoc(path, temp);
  if (ok && allowWifiClear && strcmp(path, SYSTEM_PATH) == 0 && !systemWifiPresent(temp)) {
    Preferences prefs;
    if (prefs.begin(CONFIG_BACKUP_NAMESPACE, false)) {
      prefs.putBool("valid", false);
      prefs.putBool("pending", false);
      prefs.end();
      Serial.println(F("Backup NVS WiFi invalide apres effacement explicite."));
    }
  }
  allowEmptyConfigWrite = previousAllowEmpty;
  allowWifiClearConfigWrite = previousAllowWifiClear;
  return ok;
}

bool ConfigManager::loadOrDefault(const char *path, DynamicJsonDocument &doc, void (ConfigManager::*defaults)()) {
  configLastError = "";
  if (!LittleFS.exists(path)) {
    logConfigFileResult(path, "absent", 0, "defaults usine");
    (this->*defaults)();
    Serial.print(F("Config absente, creation depuis defaults: "));
    Serial.println(path);
    return saveDoc(path, doc);
  }
  File sizeFile = LittleFS.open(path, "r");
  size_t fileSize = sizeFile ? sizeFile.size() : 0;
  if (sizeFile) sizeFile.close();
  File file = LittleFS.open(path, "r");
  if (!file) {
    logConfigFileResult(path, "trouve mais ouverture impossible", fileSize, "non lu");
    writeDiagnosticFile(path, "ouverture impossible");
    if (restoreBackupFile(path, doc)) return true;
    (this->*defaults)();
    return false;
  }
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    logConfigFileResult(path, "trouve", fileSize, err.c_str());
    writeDiagnosticFile(path, String("JSON invalide: ") + err.c_str());
    backupCorruptFile(path);
    if (restoreBackupFile(path, doc)) return true;
    (this->*defaults)();
    return false;
  }
  logConfigFileResult(path, "trouve", fileSize, "JSON OK");
  if (!doc["version"].is<int>()) {
    doc["version"] = CONFIG_VERSION;
    if (!configLooksEmpty(path, doc)) saveDoc(path, doc);
  }
  if (strcmp(path, SYSTEM_PATH) == 0 && migrateSystemWifiConfig(doc, "boot")) {
    saveDoc(path, doc);
  }
  if (!validateDoc(path, doc)) {
    writeDiagnosticFile(path, "structure JSON invalide");
    (this->*defaults)();
    if (restoreBackupFile(path, doc)) return true;
    return false;
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
  if (wouldEraseUserConfig(path, doc)) {
    configLastError = String("Ecriture refusee, config vide dangereuse: ") + path;
    Serial.println(configLastError);
    return false;
  }
  ensureConfigDir();
  if (strcmp(path, SYSTEM_PATH) == 0 && LittleFS.exists(path)) backupUserConfigFile(path);
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
  if (strcmp(path, SYSTEM_PATH) == 0 && systemWifiPresent(doc)) {
    backupConfigToNvs();
  }
  return true;
}

void ConfigManager::ensureConfigDir() {
  if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
  if (!LittleFS.exists(BACKUP_DIR)) LittleFS.mkdir(BACKUP_DIR);
  if (!LittleFS.exists(DIAG_DIR)) LittleFS.mkdir(DIAG_DIR);
}

bool ConfigManager::validateDoc(const char *path, DynamicJsonDocument &doc) {
  if (!doc.is<JsonObject>()) return false;
  if (!doc["version"].is<int>()) return false;
  if (strcmp(path, DEVICE_PATH) == 0) return doc["role"].is<const char *>() && doc["deviceName"].is<const char *>();
  if (strcmp(path, SYSTEM_PATH) == 0) {
    bool hasWifi = doc["wifi"].is<JsonObject>() || doc["wifiSsid"].is<const char *>();
    bool hasFallback = doc["fallbackAp"].is<JsonObject>() || doc["fallbackApSsid"].is<const char *>();
    return hasWifi && hasFallback;
  }
  if (strcmp(path, SENSORS_PATH) == 0) return doc["sensors"].is<JsonArray>() && doc["oneWireBus"].is<JsonObject>() && doc["ds18b20"].is<JsonArray>();
  if (strcmp(path, ACTUATORS_PATH) == 0) return doc["actuators"].is<JsonArray>();
  if (strcmp(path, RULES_PATH) == 0) return doc["rules"].is<JsonArray>() && doc["rules"].size() > 0;
  return false;
}

bool ConfigManager::restoreBackupFile(const char *path, DynamicJsonDocument &doc) {
  String backup = backupPathFor(path);
  if (!LittleFS.exists(backup.c_str())) {
    Serial.print(F("Aucun backup config disponible pour "));
    Serial.println(path);
    return false;
  }
  File file = LittleFS.open(backup.c_str(), "r");
  if (!file) return false;
  DynamicJsonDocument temp(doc.capacity());
  DeserializationError err = deserializeJson(temp, file);
  file.close();
  if (err || temp.overflowed() || !validateDoc(path, temp)) {
    Serial.print(F("Backup config invalide: "));
    Serial.println(backup);
    return false;
  }
  doc = temp;
  if (saveDoc(path, doc)) {
    Serial.print(F("Configuration restauree depuis backup: "));
    Serial.println(backup);
    return true;
  }
  return false;
}

bool ConfigManager::writeDiagnosticFile(const char *path, const String &reason) {
  ensureConfigDir();
  String diag = diagnosticPathFor(path);
  String body;
  body.reserve(180);
  body += "path=";
  body += path;
  body += "\nreason=";
  body += reason;
  body += "\n";
  Serial.print(F("Diagnostic config: "));
  Serial.print(path);
  Serial.print(F(" -> "));
  Serial.println(reason);
  return writeTextFile(diag.c_str(), body);
}

bool ConfigManager::backupUserConfigFile(const char *path) {
  ensureConfigDir();
  if (!LittleFS.exists(path)) return false;
  File currentFile = LittleFS.open(path, "r");
  if (!currentFile) return false;
  DynamicJsonDocument current(32768);
  DeserializationError err = deserializeJson(current, currentFile);
  currentFile.close();
  if (err || current.overflowed() || !validateDoc(path, current) || configLooksEmpty(path, current)) {
    Serial.print(F("Backup config ignore, fichier courant invalide ou vide: "));
    Serial.println(path);
    return false;
  }
  String backup = backupPathFor(path);
  bool ok = copyLittleFsFile(path, backup.c_str());
  Serial.print(ok ? F("Backup config cree: ") : F("Backup config impossible: "));
  Serial.println(backup);
  return ok;
}

bool ConfigManager::configLooksEmpty(const char *path, DynamicJsonDocument &doc) {
  if (strcmp(path, SYSTEM_PATH) == 0) {
    const char *rootSsid = doc["wifiSsid"] | "";
    const char *wifiSsid = doc["wifi"]["ssid"] | "";
    return !textPresent(rootSsid) && !textPresent(wifiSsid);
  }
  if (strcmp(path, SENSORS_PATH) == 0) {
    JsonArray sensorsArr = doc["sensors"].as<JsonArray>();
    JsonArray dsArr = doc["ds18b20"].as<JsonArray>();
    return sensorsArr.size() == 0 && dsArr.size() == 0;
  }
  if (strcmp(path, ACTUATORS_PATH) == 0) return doc["actuators"].as<JsonArray>().size() == 0;
  if (strcmp(path, RULES_PATH) == 0) return doc["rules"].as<JsonArray>().size() == 0;
  return false;
}

bool ConfigManager::systemWifiPresent(DynamicJsonDocument &doc) {
  JsonObject root = doc.as<JsonObject>();
  JsonObject wifi = root["wifi"].is<JsonObject>() ? root["wifi"].as<JsonObject>() : root["wifi"].to<JsonObject>();
  const char *modernSsid = wifi["ssid"] | "";
  const char *legacySsid = root["wifiSsid"] | "";
  return textPresent(modernSsid) || textPresent(legacySsid);
}

bool ConfigManager::shouldRestoreConfigFromNvs(Preferences &prefs) {
  String systemJson = getChunkedString(prefs, "sys");
  DynamicJsonDocument backupSystem(systemConfig.capacity());
  DeserializationError backupErr = deserializeJson(backupSystem, systemJson);
  bool backupWifi = !backupErr && !backupSystem.overflowed() && validateDoc(SYSTEM_PATH, backupSystem) && systemWifiPresent(backupSystem);
  Serial.print(F("Backup NVS system WiFi present: "));
  Serial.println(backupWifi ? F("oui") : F("non"));
  if (!backupWifi) return false;

  if (!LittleFS.exists(SYSTEM_PATH)) {
    Serial.println(F("Restauration NVS requise: system.json absent."));
    return true;
  }
  File file = LittleFS.open(SYSTEM_PATH, "r");
  if (!file) {
    Serial.println(F("Restauration NVS requise: system.json illisible."));
    return true;
  }
  DynamicJsonDocument current(systemConfig.capacity());
  DeserializationError currentErr = deserializeJson(current, file);
  file.close();
  bool currentWifi = !currentErr && !current.overflowed() && validateDoc(SYSTEM_PATH, current) && systemWifiPresent(current);
  Serial.print(F("system.json actuel WiFi present: "));
  Serial.println(currentWifi ? F("oui") : F("non"));
  return !currentWifi;
}

bool ConfigManager::migrateSystemWifiConfig(DynamicJsonDocument &doc, const char *context) {
  JsonObject root = doc.as<JsonObject>();
  JsonObject wifi = root["wifi"].is<JsonObject>() ? root["wifi"].as<JsonObject>() : root["wifi"].to<JsonObject>();
  const char *modernSsid = wifi["ssid"] | "";
  const char *legacySsid = root["wifiSsid"] | "";
  const char *modernPassword = wifi["password"] | "";
  const char *legacyPassword = root["wifiPassword"] | "";
  bool changed = false;

  Serial.print(F("WiFi config "));
  Serial.print(context);
  Serial.print(F(": wifi.ssid="));
  Serial.print(textPresent(modernSsid) ? F("oui") : F("non"));
  Serial.print(F(", wifiSsid legacy="));
  Serial.print(textPresent(legacySsid) ? F("oui") : F("non"));

  if (!textPresent(modernSsid) && textPresent(legacySsid)) {
    wifi["ssid"] = legacySsid;
    modernSsid = legacySsid;
    changed = true;
    Serial.print(F(", source=wifiSsid legacy"));
  } else {
    Serial.print(F(", source=wifi.ssid"));
  }

  if (!textPresent(modernPassword) && textPresent(legacyPassword)) {
    wifi["password"] = legacyPassword;
    changed = true;
  }

  if (textPresent(modernSsid)) {
    root["wifiSsid"] = modernSsid;
    changed = true;
  }
  if (textPresent(wifi["password"] | "")) {
    root["wifiPassword"] = wifi["password"] | "";
    changed = true;
  }
  Serial.println();
  return changed;
}

bool ConfigManager::preserveExistingWifiConfig(DynamicJsonDocument &incoming) {
  if (!LittleFS.exists(SYSTEM_PATH)) return false;
  DynamicJsonDocument current(systemConfig.capacity());
  File file = LittleFS.open(SYSTEM_PATH, "r");
  if (!file) return false;
  DeserializationError err = deserializeJson(current, file);
  file.close();
  if (err || current.overflowed()) return false;
  migrateSystemWifiConfig(current, "preserve-current");
  JsonObject inRoot = incoming.as<JsonObject>();
  JsonObject inWifi = inRoot["wifi"].is<JsonObject>() ? inRoot["wifi"].as<JsonObject>() : inRoot["wifi"].to<JsonObject>();
  JsonObject curRoot = current.as<JsonObject>();
  JsonObject curWifi = curRoot["wifi"].as<JsonObject>();
  bool changed = false;

  if (!textPresent(inWifi["ssid"] | "") && textPresent(curWifi["ssid"] | "")) {
    inWifi["ssid"] = curWifi["ssid"] | "";
    inRoot["wifiSsid"] = curWifi["ssid"] | "";
    changed = true;
  }
  if (!textPresent(inWifi["password"] | "") && textPresent(curWifi["password"] | "")) {
    inWifi["password"] = curWifi["password"] | "";
    inRoot["wifiPassword"] = curWifi["password"] | "";
    changed = true;
  }
  if (changed) Serial.println(F("WiFi config preservee: champs vides remplaces par la configuration existante."));
  return changed;
}

bool ConfigManager::wouldEraseUserConfig(const char *path, DynamicJsonDocument &doc) {
  if (allowEmptyConfigWrite) return false;
  if (allowWifiClearConfigWrite && strcmp(path, SYSTEM_PATH) == 0) return false;
  if (!LittleFS.exists(path) || !configLooksEmpty(path, doc)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  DynamicJsonDocument current(32768);
  DeserializationError err = deserializeJson(current, file);
  file.close();
  if (err || current.overflowed() || !validateDoc(path, current)) return false;
  return !configLooksEmpty(path, current);
}

void ConfigManager::logConfigFileResult(const char *path, const char *status, size_t size, const char *parseResult) {
  Serial.print(F("Config load "));
  Serial.print(path);
  Serial.print(F(" : "));
  Serial.print(status);
  Serial.print(F(", taille="));
  Serial.print(size);
  Serial.print(F(", parse="));
  Serial.println(parseResult);
}

bool ConfigManager::normalizeSystemConfig() {
  bool changed = false;
  JsonObject root = systemConfig.as<JsonObject>();
  JsonObject wifi = root["wifi"].is<JsonObject>() ? root["wifi"].as<JsonObject>() : root["wifi"].to<JsonObject>();
  JsonObject ap = root["fallbackAp"].is<JsonObject>() ? root["fallbackAp"].as<JsonObject>() : root["fallbackAp"].to<JsonObject>();

  if (migrateSystemWifiConfig(systemConfig, "normalize")) changed = true;

  // Les identifiants WiFi maison restent volontairement vides tant que
  // l'utilisateur ne les a pas saisis depuis l'interface Web.
  if (isConfigPlaceholder(root["wifiSsid"] | "") && !textPresent(wifi["ssid"] | "")) {
    root["wifiSsid"] = "";
    changed = true;
  }
  if (isConfigPlaceholder(root["wifiPassword"] | "") && !textPresent(wifi["password"] | "")) {
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
  JsonObject statusLed = root["statusLed"].is<JsonObject>() ? root["statusLed"].as<JsonObject>() : root["statusLed"].to<JsonObject>();
  if (!statusLed["bluePin"].is<int>()) {
    statusLed["bluePin"] = statusLed["pin"] | 2;
    changed = true;
  }
  if (!statusLed["redPin"].is<int>()) {
    statusLed["redPin"] = -1;
    changed = true;
  }
  if (!statusLed["activeHigh"].is<bool>()) {
    statusLed["activeHigh"] = true;
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
  if (!router["maxOutputRampPercentPerSecond"].is<float>() && !router["maxOutputRampPercentPerSecond"].is<int>()) { router["maxOutputRampPercentPerSecond"] = 5; changed = true; }
  else if (fabs((router["maxOutputRampPercentPerSecond"] | 5.0f) - 15.0f) < 0.001f) { router["maxOutputRampPercentPerSecond"] = 5; changed = true; }
  if (!router["heaterMaxPowerW"].is<float>() && !router["heaterMaxPowerW"].is<int>()) { router["heaterMaxPowerW"] = router["ssr1MaxW"] | 1500; changed = true; }
  if (!router["kp"].is<float>() && !router["kp"].is<int>()) { router["kp"] = 0.02; changed = true; }
  else if (fabs((router["kp"] | 0.02f) - 0.08f) < 0.0001f) { router["kp"] = 0.02; changed = true; }
  if (!router["ki"].is<float>() && !router["ki"].is<int>()) { router["ki"] = 0.002; changed = true; }
  else if (fabs((router["ki"] | 0.002f) - 0.01f) < 0.0001f) { router["ki"] = 0.002; changed = true; }
  if (!router["kd"].is<float>() && !router["kd"].is<int>()) { router["kd"] = 0.0; changed = true; }
  if (!router["jsyReadIntervalMs"].is<uint32_t>()) { router["jsyReadIntervalMs"] = 100; changed = true; }
  if (!router["voltageMinV"].is<float>() && !router["voltageMinV"].is<int>()) { router["voltageMinV"] = 180; changed = true; }
  if (!router["voltageMaxV"].is<float>() && !router["voltageMaxV"].is<int>()) { router["voltageMaxV"] = 260; changed = true; }
  if (!router["frequencyMinHz"].is<float>() && !router["frequencyMinHz"].is<int>()) { router["frequencyMinHz"] = 47; changed = true; }
  if (!router["frequencyMaxHz"].is<float>() && !router["frequencyMaxHz"].is<int>()) { router["frequencyMaxHz"] = 53; changed = true; }
  JsonObject dashboard = root["dashboard"].is<JsonObject>() ? root["dashboard"].as<JsonObject>() : root["dashboard"].to<JsonObject>();
  JsonArray graphs = dashboard["graphs"].is<JsonArray>() ? dashboard["graphs"].as<JsonArray>() : dashboard["graphs"].to<JsonArray>();
  if (graphs.size() == 0) {
    JsonObject tempGraph = graphs.add<JsonObject>();
    tempGraph["id"] = "graph_temperature";
    tempGraph["title"] = "Temperatures";
    tempGraph["type"] = "line";
    tempGraph["enabled"] = true;
    JsonArray tempSeries = tempGraph["series"].to<JsonArray>();
    JsonObject temp = tempSeries.add<JsonObject>();
    temp["sourceType"] = "energy";
    temp["sourceId"] = "energy";
    temp["valueKey"] = "tankTempC";
    temp["label"] = "Ballon";
    temp["unit"] = "C";
    temp["color"] = "#38bdf8";
    JsonObject energyGraph = graphs.add<JsonObject>();
    energyGraph["id"] = "graph_energy";
    energyGraph["title"] = "Energie";
    energyGraph["type"] = "area";
    energyGraph["enabled"] = true;
    JsonArray energySeries = energyGraph["series"].to<JsonArray>();
    JsonObject grid = energySeries.add<JsonObject>();
    grid["sourceType"] = "energy";
    grid["sourceId"] = "energy";
    grid["valueKey"] = "gridPowerW";
    grid["label"] = "Reseau";
    grid["unit"] = "W";
    grid["color"] = "#ef4444";
    JsonObject surplus = energySeries.add<JsonObject>();
    surplus["sourceType"] = "energy";
    surplus["sourceId"] = "energy";
    surplus["valueKey"] = "surplusW";
    surplus["label"] = "Surplus";
    surplus["unit"] = "W";
    surplus["color"] = "#22c55e";
    JsonObject pidGraph = graphs.add<JsonObject>();
    pidGraph["id"] = "graph_pid";
    pidGraph["title"] = "PID";
    pidGraph["type"] = "pid";
    pidGraph["enabled"] = true;
    JsonArray pidSeries = pidGraph["series"].to<JsonArray>();
    JsonObject pidError = pidSeries.add<JsonObject>();
    pidError["sourceType"] = "pid";
    pidError["sourceId"] = "pid";
    pidError["valueKey"] = "errorW";
    pidError["label"] = "Erreur PID";
    pidError["unit"] = "W";
    pidError["color"] = "#f59e0b";
    changed = true;
  }
  JsonObject espnow = root["espnow"].is<JsonObject>() ? root["espnow"].as<JsonObject>() : root["espnow"].to<JsonObject>();
  if (!espnow["sensorDiscoveryIntervalMs"].is<uint32_t>()) { espnow["sensorDiscoveryIntervalMs"] = 30000; changed = true; }
  if (!espnow["diagnosticIntervalMs"].is<uint32_t>()) { espnow["diagnosticIntervalMs"] = 10000; changed = true; }
  if (!espnow["debugTransmission"].is<bool>()) { espnow["debugTransmission"] = false; changed = true; }
  if (!espnow["debugReception"].is<bool>()) { espnow["debugReception"] = false; changed = true; }
  if (!espnow["exports"].is<JsonArray>()) { espnow["exports"].to<JsonArray>(); changed = true; }
  return changed;
}

bool ConfigManager::normalizeSensorsConfig() {
  bool changed = false;
  Serial.println(F("CONFIG NORMALIZE sensors: file exists, no defaults injected"));
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
    const bool isEspNowSensor = sensor["source"].as<String>().equalsIgnoreCase("espnow");
    if (!isEspNowSensor && (strcmp(id, "jsy_grid") == 0 || strcmp(type, "JSY-MK-194T") == 0)) {
      int rx = sensor["rx"] | 26;
      int tx = sensor["tx"] | 27;
      if (rx == 16 && tx == 17) {
        sensor["rx"] = 26;
        sensor["tx"] = 27;
        changed = true;
      }
      if (!sensor["baudrate"].is<uint32_t>()) {
        sensor["baudrate"] = 19200;
        changed = true;
      }
      if (!sensor["modbusAddress"].is<int>()) {
        sensor["modbusAddress"] = 1;
        changed = true;
      }
      if (!sensor["readIntervalMs"].is<uint32_t>()) {
        sensor["readIntervalMs"] = 350;
        changed = true;
      }
      if (!sensor["timeoutMs"].is<uint32_t>()) {
        sensor["timeoutMs"] = 300;
        changed = true;
      }
      JsonArray channels = sensor["channels"].as<JsonArray>();
      if (channels.isNull() || channels.size() < 2) {
        channels = sensor["channels"].to<JsonArray>();
        JsonObject ch1 = channels.add<JsonObject>();
        ch1["id"] = "clamp1"; ch1["name"] = "Pince 1"; ch1["role"] = "production"; ch1["measures"].add("current"); ch1["measures"].add("power"); ch1["measures"].add("pf"); ch1["measures"].add("energyPos"); ch1["measures"].add("energyNeg"); ch1["measures"].add("direction");
        JsonObject ch2 = channels.add<JsonObject>();
        ch2["id"] = "clamp2"; ch2["name"] = "Pince 2"; ch2["role"] = "grid"; ch2["measures"].add("current"); ch2["measures"].add("power"); ch2["measures"].add("pf"); ch2["measures"].add("energyPos"); ch2["measures"].add("energyNeg"); ch2["measures"].add("direction");
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
          m.add("current"); m.add("power"); m.add("pf"); m.add("energyPos"); m.add("energyNeg"); m.add("direction");
          changed = true;
        }
        if (!ch2["measures"].is<JsonArray>()) {
          JsonArray m = ch2["measures"].to<JsonArray>();
          m.add("current"); m.add("power"); m.add("pf"); m.add("energyPos"); m.add("energyNeg"); m.add("direction");
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
    } else if (!isEspNowSensor && (strcmp(id, "tic_linky") == 0 || strcmp(type, "TIC Linky") == 0)) {
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
      if (!sensor["debug"].is<bool>()) {
        sensor["debug"] = false;
        changed = true;
      }
    }
    if (!isEspNowSensor && !sensor["espNowExportEnabled"].is<bool>()) {
      sensor["espNowExportEnabled"] = false;
      sensor["espNowSendOnChange"] = true;
      if (strcmp(id, "jsy_grid") == 0 || strcmp(type, "JSY-MK-194T") == 0) {
        sensor["espNowExportIntervalMs"] = 200;
        sensor["espNowExportPriority"] = PRIORITY_CRITICAL;
        sensor["espNowMinDelta"] = 5.0;
      } else if (strcmp(id, "tic_linky") == 0 || strcmp(type, "TIC Linky") == 0) {
        sensor["espNowExportIntervalMs"] = 1000;
        sensor["espNowExportPriority"] = PRIORITY_NORMAL;
        sensor["espNowMinDelta"] = 10.0;
      } else {
        sensor["espNowExportIntervalMs"] = 1000;
        sensor["espNowExportPriority"] = PRIORITY_NORMAL;
        sensor["espNowMinDelta"] = 10.0;
      }
      changed = true;
    }
  }
  JsonArray ds = root["ds18b20"].as<JsonArray>();
  for (JsonObject sensor : ds) {
    if (!sensor["espNowExportEnabled"].is<bool>()) {
      sensor["espNowExportEnabled"] = false;
      sensor["espNowExportIntervalMs"] = 10000;
      sensor["espNowExportPriority"] = PRIORITY_LOW;
      sensor["espNowSendOnChange"] = true;
      sensor["espNowMinDelta"] = 0.2;
      changed = true;
    }
  }
  return changed;
}

bool ConfigManager::normalizeActuatorsConfig() {
  bool changed = false;
  Serial.println(F("CONFIG NORMALIZE actuators: file exists, no defaults injected"));
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
    }
    if (!actuator["activeHigh"].is<bool>()) {
      actuator["activeHigh"] = true;
      changed = true;
    }
    String source = actuator["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) {
      if (!actuator["espNowExportEnabled"].is<bool>()) {
        actuator["espNowExportEnabled"] = false;
        changed = true;
      }
      if (!actuator["espNowExportIntervalMs"].is<uint32_t>()) {
        actuator["espNowExportIntervalMs"] = 30000;
        changed = true;
      }
      if (!actuator["espNowCommandTimeoutMs"].is<uint32_t>()) {
        actuator["espNowCommandTimeoutMs"] = actuator["ttlMs"] | 1000;
        changed = true;
      }
      if (!actuator["espNowDebug"].is<bool>()) {
        actuator["espNowDebug"] = actuator["debug"] | false;
        changed = true;
      }
    }
  }
  return changed;
}

void ConfigManager::backupCorruptFile(const char *path) {
  if (!LittleFS.exists(path)) return;
  String diagnostic = String(DIAG_DIR) + "/" + configBaseName(path) + ".bad";
  if (LittleFS.exists(diagnostic)) LittleFS.remove(diagnostic);
  LittleFS.rename(path, diagnostic);
  Serial.print(F("Config invalide conservee: "));
  Serial.println(diagnostic);
}

void ConfigManager::printConfigSummary() {
  Serial.println(F("=== Config summary ==="));
  Serial.print(F("Device: "));
  Serial.print(device()["deviceName"] | device()["name"] | "?");
  Serial.print(F(" / role "));
  Serial.println(device()["role"] | "?");
  Serial.print(F("WiFi SSID: "));
  {
    const char *modernSsid = system()["wifi"]["ssid"] | "";
    const char *legacySsid = system()["wifiSsid"] | "";
    const char *ssid = modernSsid[0] ? modernSsid : legacySsid;
    Serial.println(ssid[0] ? F("present") : F("absent"));
  }
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
  root["nodeId"] = 0;
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
  JsonObject statusLed = root["statusLed"].to<JsonObject>();
  statusLed["bluePin"] = 2;
  statusLed["redPin"] = -1;
  statusLed["activeHigh"] = true;
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
  router["maxOutputRampPercentPerSecond"] = 5;
  router["heaterMaxPowerW"] = 1500;
  router["jsyReadIntervalMs"] = 100;
  router["voltageMinV"] = 180;
  router["voltageMaxV"] = 260;
  router["frequencyMinHz"] = 47;
  router["frequencyMaxHz"] = 53;
  router["kp"] = 0.02;
  router["ki"] = 0.002;
  router["kd"] = 0.0;
  JsonObject dashboard = root["dashboard"].to<JsonObject>();
  dashboard["temperatureMainSource"] = nullptr;
  JsonArray graphs = dashboard["graphs"].to<JsonArray>();
  JsonObject tempGraph = graphs.add<JsonObject>();
  tempGraph["id"] = "graph_temperature";
  tempGraph["title"] = "Temperatures";
  tempGraph["type"] = "line";
  tempGraph["enabled"] = true;
  JsonArray tempSeries = tempGraph["series"].to<JsonArray>();
  JsonObject temp = tempSeries.add<JsonObject>();
  temp["sourceType"] = "energy";
  temp["sourceId"] = "energy";
  temp["valueKey"] = "tankTempC";
  temp["label"] = "Ballon";
  temp["unit"] = "C";
  temp["color"] = "#38bdf8";
  JsonObject energyGraph = graphs.add<JsonObject>();
  energyGraph["id"] = "graph_energy";
  energyGraph["title"] = "Energie";
  energyGraph["type"] = "area";
  energyGraph["enabled"] = true;
  JsonArray energySeries = energyGraph["series"].to<JsonArray>();
  JsonObject grid = energySeries.add<JsonObject>();
  grid["sourceType"] = "energy";
  grid["sourceId"] = "energy";
  grid["valueKey"] = "gridPowerW";
  grid["label"] = "Reseau";
  grid["unit"] = "W";
  grid["color"] = "#ef4444";
  JsonObject surplus = energySeries.add<JsonObject>();
  surplus["sourceType"] = "energy";
  surplus["sourceId"] = "energy";
  surplus["valueKey"] = "surplusW";
  surplus["label"] = "Surplus";
  surplus["unit"] = "W";
  surplus["color"] = "#22c55e";
  JsonObject pidGraph = graphs.add<JsonObject>();
  pidGraph["id"] = "graph_pid";
  pidGraph["title"] = "PID";
  pidGraph["type"] = "pid";
  pidGraph["enabled"] = true;
  JsonArray pidSeries = pidGraph["series"].to<JsonArray>();
  JsonObject pidError = pidSeries.add<JsonObject>();
  pidError["sourceType"] = "pid";
  pidError["sourceId"] = "pid";
  pidError["valueKey"] = "errorW";
  pidError["label"] = "Erreur PID";
  pidError["unit"] = "W";
  pidError["color"] = "#f59e0b";
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
  JsonObject espnow = root["espnow"].to<JsonObject>();
  espnow["sensorDiscoveryIntervalMs"] = 30000;
  espnow["diagnosticIntervalMs"] = 10000;
  espnow["debugTransmission"] = false;
  espnow["debugReception"] = false;
  espnow["exports"].to<JsonArray>();
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
  s["serial"] = "Serial2"; s["rx"] = 26; s["tx"] = 27; s["baudrate"] = 19200; s["modbusAddress"] = 1;
  s["readIntervalMs"] = 350; s["timeoutMs"] = 300;
  s["espNowExportEnabled"] = false; s["espNowExportIntervalMs"] = 200; s["espNowExportPriority"] = PRIORITY_CRITICAL; s["espNowSendOnChange"] = true; s["espNowMinDelta"] = 5.0;
  s["role"] = "mesure reseau principal"; s["enabled"] = true;
  JsonArray channels = s["channels"].to<JsonArray>();
  JsonObject ch1 = channels.add<JsonObject>();
  ch1["id"] = "clamp1"; ch1["name"] = "Pince 1"; ch1["role"] = "production"; ch1["measures"].add("current"); ch1["measures"].add("power"); ch1["measures"].add("pf"); ch1["measures"].add("energyPos"); ch1["measures"].add("energyNeg"); ch1["measures"].add("direction");
  JsonObject ch2 = channels.add<JsonObject>();
  ch2["id"] = "clamp2"; ch2["name"] = "Pince 2"; ch2["role"] = "grid"; ch2["measures"].add("current"); ch2["measures"].add("power"); ch2["measures"].add("pf"); ch2["measures"].add("energyPos"); ch2["measures"].add("energyNeg"); ch2["measures"].add("direction");
  JsonObject t = arr.add<JsonObject>();
  t["id"] = "tic_linky"; t["name"] = "TIC Linky"; t["type"] = "TIC Linky"; t["source"] = "local";
  t["serial"] = "Serial1"; t["rx"] = 26; t["tx"] = 27; t["mode"] = "historique"; t["baudrate"] = 1200; t["timeoutMs"] = 5000;
  t["debug"] = false;
  t["espNowExportEnabled"] = false; t["espNowExportIntervalMs"] = 1000; t["espNowExportPriority"] = PRIORITY_NORMAL; t["espNowSendOnChange"] = true; t["espNowMinDelta"] = 10.0;
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
    d["espNowExportEnabled"] = false;
    d["espNowExportIntervalMs"] = 10000;
    d["espNowExportPriority"] = PRIORITY_LOW;
    d["espNowSendOnChange"] = true;
    d["espNowMinDelta"] = 0.2;
  }
}

void ConfigManager::defaultActuators() {
  actuatorsConfig.clear();
  actuatorsConfig["version"] = CONFIG_VERSION;
  JsonArray arr = actuatorsConfig["actuators"].to<JsonArray>();
  JsonObject a = arr.add<JsonObject>();
  a["id"] = "ssr1_water_heater"; a["name"] = "SSR1 chauffe-eau principal"; a["type"] = "SSR"; a["source"] = "local";
  a["gpio"] = 5; a["mode"] = "BURST_FIRE"; a["activeHigh"] = true; a["maxPowerW"] = 1500; a["cycleMs"] = 1000; a["critical"] = true; a["enabled"] = true;
  JsonObject b = arr.add<JsonObject>();
  b["id"] = "ssr2_aux"; b["name"] = "SSR2 auxiliaire"; b["type"] = "SSR"; b["source"] = "local";
  b["gpio"] = 17; b["mode"] = "BURST_FIRE"; b["activeHigh"] = true; b["maxPowerW"] = 1000; b["cycleMs"] = 1000; b["critical"] = true; b["enabled"] = true;
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
  JsonObject a1 = a.add<JsonObject>(); a1["actuatorId"] = "ssr1_water_heater"; a1["command"] = "setPowerFromSurplus"; a1["regulation"] = "PID"; a1["maxHeaterPowerW"] = 1500;

  JsonObject s = arr.add<JsonObject>();
  s["id"] = "tank_temperature_safety"; s["name"] = "Securite temperature ballon"; s["enabled"] = true; s["priority"] = 100; s["logic"] = "OR";
  JsonArray sc = s["conditions"].to<JsonArray>();
  JsonObject sc1 = sc.add<JsonObject>(); sc1["id"] = "cond_safety_sonde1"; sc1["source"] = "sonde1"; sc1["measure"] = "temperatureC"; sc1["type"] = "number"; sc1["operator"] = ">="; sc1["value"] = 70; sc1["unit"] = "C";
  JsonObject sc2 = sc.add<JsonObject>(); sc2["id"] = "cond_safety_sonde2"; sc2["source"] = "sonde2"; sc2["measure"] = "temperatureC"; sc2["type"] = "number"; sc2["operator"] = ">="; sc2["value"] = 70; sc2["unit"] = "C";
  JsonObject sc3 = sc.add<JsonObject>(); sc3["id"] = "cond_safety_sonde3"; sc3["source"] = "sonde3"; sc3["measure"] = "temperatureC"; sc3["type"] = "number"; sc3["operator"] = ">="; sc3["value"] = 70; sc3["unit"] = "C";
  JsonArray sa = s["actions"].to<JsonArray>();
  JsonObject sa1 = sa.add<JsonObject>(); sa1["actuatorId"] = "ssr1_water_heater"; sa1["command"] = "stop"; sa1["value"] = 0;
  JsonObject sa2 = sa.add<JsonObject>(); sa2["actuatorId"] = "ssr2_aux"; sa2["command"] = "stop"; sa2["value"] = 0;
  JsonObject sa4 = sa.add<JsonObject>(); sa4["command"] = "setSafetyWarning"; sa4["message"] = "Temperature ballon depassee";
}
