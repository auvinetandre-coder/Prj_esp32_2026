#include "config_manager.h"
#include <LittleFS.h>

static const char *DEVICE_PATH = "/config/device.json";
static const char *SYSTEM_PATH = "/config/system.json";
static const char *SENSORS_PATH = "/config/sensors.json";
static const char *ACTUATORS_PATH = "/config/actuators.json";
static const char *RULES_PATH = "/config/rules.json";
static const uint16_t CONFIG_VERSION = 2;
static const char *FIRMWARE_VERSION = "0.2.0";

bool ConfigManager::begin() {
  bool ok = loadAll();
  printConfigSummary();
  return ok;
}

bool ConfigManager::loadAll() {
  ensureConfigDir();
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

JsonObject ConfigManager::device() { return deviceConfig.as<JsonObject>(); }
JsonObject ConfigManager::system() { return systemConfig.as<JsonObject>(); }
JsonArray ConfigManager::sensors() { return sensorsConfig["sensors"].as<JsonArray>(); }
JsonArray ConfigManager::actuators() { return actuatorsConfig["actuators"].as<JsonArray>(); }
JsonArray ConfigManager::rules() { return rulesConfig["rules"].as<JsonArray>(); }

bool ConfigManager::replaceFile(const char *path, const String &json) {
  DynamicJsonDocument temp(12288);
  if (deserializeJson(temp, json)) return false;
  if (!validateDoc(path, temp)) return false;
  if (strcmp(path, DEVICE_PATH) == 0) deviceConfig = temp;
  else if (strcmp(path, SYSTEM_PATH) == 0) systemConfig = temp;
  else if (strcmp(path, SENSORS_PATH) == 0) sensorsConfig = temp;
  else if (strcmp(path, ACTUATORS_PATH) == 0) actuatorsConfig = temp;
  else if (strcmp(path, RULES_PATH) == 0) rulesConfig = temp;
  else return false;
  return saveDoc(path, temp);
}

bool ConfigManager::loadOrDefault(const char *path, DynamicJsonDocument &doc, void (ConfigManager::*defaults)()) {
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
  ensureConfigDir();
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  serializeJsonPretty(doc, file);
  file.flush();
  file.close();
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
  root["wifiSsid"] = "WIFI_SSID_A_CONFIGURER";
  root["wifiPassword"] = "WIFI_PASSWORD_A_CONFIGURER";
  root["fallbackApSsid"] = "AP_SSID_A_CONFIGURER";
  root["fallbackApPassword"] = "AP_PASSWORD_A_CONFIGURER";
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
  wifi["ssid"] = "WIFI_SSID_A_CONFIGURER";
  wifi["password"] = "WIFI_PASSWORD_A_CONFIGURER";
  wifi["keepFallbackApAlwaysOn"] = true;
  JsonObject ap = root["fallbackAp"].to<JsonObject>();
  ap["ssid"] = "AP_SSID_A_CONFIGURER";
  ap["password"] = "AP_PASSWORD_A_CONFIGURER";
  ap["ip"] = "192.168.4.1";
  JsonObject router = root["router"].to<JsonObject>();
  router["mode"] = "AUTO";
  router["gridPowerSource"] = "JSY";
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
  router["kp"] = 0.08;
  router["ki"] = 0.01;
  router["kd"] = 0.0;
  root["debug"] = true;
  root["peers"].to<JsonArray>();
}

void ConfigManager::defaultSensors() {
  sensorsConfig.clear();
  sensorsConfig["version"] = CONFIG_VERSION;
  JsonObject bus = sensorsConfig["oneWireBus"].to<JsonObject>();
  bus["gpio"] = 4;
  bus["enabled"] = true;
  bus["scanOnBoot"] = true;
  bus["readIntervalMs"] = 2000;
  JsonArray arr = sensorsConfig["sensors"].to<JsonArray>();
  JsonObject s = arr.add<JsonObject>();
  s["id"] = "jsy_grid"; s["name"] = "JSY reseau"; s["type"] = "JSY-MK-194T"; s["source"] = "local";
  s["serial"] = "Serial2"; s["rx"] = 16; s["tx"] = 17; s["role"] = "mesure reseau principal"; s["enabled"] = true;
  JsonArray channels = s["channels"].to<JsonArray>();
  JsonObject ch1 = channels.add<JsonObject>();
  ch1["id"] = "clamp1"; ch1["name"] = "Pince 1"; ch1["role"] = "grid"; ch1["measures"].add("currentA1"); ch1["measures"].add("activePowerW1");
  JsonObject ch2 = channels.add<JsonObject>();
  ch2["id"] = "clamp2"; ch2["name"] = "Pince 2"; ch2["role"] = "production"; ch2["measures"].add("currentA2"); ch2["measures"].add("activePowerW2");
  JsonObject t = arr.add<JsonObject>();
  t["id"] = "tic_linky"; t["name"] = "TIC Linky"; t["type"] = "TIC Linky"; t["source"] = "local";
  t["serial"] = "Serial1"; t["rx"] = 32; t["mode"] = "historique"; t["baudrate"] = 1200; t["timeoutMs"] = 5000;
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
  JsonObject sim = actuatorsConfig["simulationOutput"].to<JsonObject>();
  sim["enabled"] = true;
  sim["ledSsr1Gpio"] = 18;
  sim["ledSsr2Gpio"] = 19;
  sim["ledTriacGpio"] = 21;
  sim["visualCycleMs"] = 1000;
  JsonArray arr = actuatorsConfig["actuators"].to<JsonArray>();
  JsonObject a = arr.add<JsonObject>();
  a["id"] = "ssr1_water_heater"; a["name"] = "SSR1 chauffe-eau principal"; a["type"] = "SSR"; a["source"] = "local";
  a["gpio"] = 26; a["mode"] = "BURST_FIRE"; a["maxPowerW"] = 1500; a["cycleMs"] = 1000; a["critical"] = true; a["enabled"] = true;
  JsonObject b = arr.add<JsonObject>();
  b["id"] = "ssr2_aux"; b["name"] = "SSR2 auxiliaire"; b["type"] = "SSR"; b["source"] = "local";
  b["gpio"] = 25; b["mode"] = "BURST_FIRE"; b["maxPowerW"] = 1000; b["cycleMs"] = 1000; b["critical"] = true; b["enabled"] = true;
  JsonObject c = arr.add<JsonObject>();
  c["id"] = "robotdyn_triac"; c["name"] = "RobotDyn Triac"; c["type"] = "RobotDyn Triac"; c["source"] = "local";
  c["zeroCross"] = 27; c["control"] = 33; c["mode"] = "PHASE_ANGLE"; c["maxPowerW"] = 1000; c["critical"] = true; c["enabled"] = true;
}

void ConfigManager::defaultRules() {
  rulesConfig.clear();
  rulesConfig["version"] = CONFIG_VERSION;
  JsonArray arr = rulesConfig["rules"].to<JsonArray>();
  JsonObject r = arr.add<JsonObject>();
  r["id"] = "solar_routing_ssr1"; r["name"] = "Routage solaire chauffe-eau SSR1"; r["enabled"] = true; r["priority"] = 10; r["logic"] = "AND";
  JsonArray c = r["conditions"].to<JsonArray>();
  JsonObject c1 = c.add<JsonObject>(); c1["id"] = "cond_surplus"; c1["source"] = "JSY-MK-194T"; c1["measure"] = "injectionW"; c1["type"] = "number"; c1["operator"] = ">"; c1["value"] = 200; c1["unit"] = "W";
  JsonObject c2 = c.add<JsonObject>(); c2["id"] = "cond_top_temp"; c2["source"] = "DS18B20_TOP"; c2["measure"] = "temperatureC"; c2["type"] = "number"; c2["operator"] = "<"; c2["value"] = 65; c2["unit"] = "C";
  JsonObject c3 = c.add<JsonObject>(); c3["id"] = "cond_middle_temp"; c3["source"] = "DS18B20_MIDDLE"; c3["measure"] = "temperatureC"; c3["type"] = "number"; c3["operator"] = "<"; c3["value"] = 65; c3["unit"] = "C";
  JsonObject c4 = c.add<JsonObject>(); c4["id"] = "cond_bottom_temp"; c4["source"] = "DS18B20_BOTTOM"; c4["measure"] = "temperatureC"; c4["type"] = "number"; c4["operator"] = "<"; c4["value"] = 65; c4["unit"] = "C";
  JsonObject c5 = c.add<JsonObject>(); c5["id"] = "cond_safety_ok"; c5["source"] = "Securite"; c5["measure"] = "safetyLevel"; c5["type"] = "enum"; c5["operator"] = "=="; c5["value"] = "OK";
  JsonArray a = r["actions"].to<JsonArray>();
  JsonObject a1 = a.add<JsonObject>(); a1["actuatorId"] = "ssr1_water_heater"; a1["command"] = "setPowerFromSurplus"; a1["maxHeaterPowerW"] = 1500;

  JsonObject s = arr.add<JsonObject>();
  s["id"] = "tank_temperature_safety"; s["name"] = "Securite temperature ballon"; s["enabled"] = true; s["priority"] = 100; s["logic"] = "OR";
  JsonArray sc = s["conditions"].to<JsonArray>();
  JsonObject sc1 = sc.add<JsonObject>(); sc1["id"] = "cond_safety_top"; sc1["source"] = "DS18B20_TOP"; sc1["measure"] = "temperatureC"; sc1["type"] = "number"; sc1["operator"] = ">="; sc1["value"] = 70; sc1["unit"] = "C";
  JsonObject sc2 = sc.add<JsonObject>(); sc2["id"] = "cond_safety_middle"; sc2["source"] = "DS18B20_MIDDLE"; sc2["measure"] = "temperatureC"; sc2["type"] = "number"; sc2["operator"] = ">="; sc2["value"] = 70; sc2["unit"] = "C";
  JsonObject sc3 = sc.add<JsonObject>(); sc3["id"] = "cond_safety_bottom"; sc3["source"] = "DS18B20_BOTTOM"; sc3["measure"] = "temperatureC"; sc3["type"] = "number"; sc3["operator"] = ">="; sc3["value"] = 70; sc3["unit"] = "C";
  JsonArray sa = s["actions"].to<JsonArray>();
  JsonObject sa1 = sa.add<JsonObject>(); sa1["actuatorId"] = "ssr1_water_heater"; sa1["command"] = "stop"; sa1["value"] = 0;
  JsonObject sa2 = sa.add<JsonObject>(); sa2["actuatorId"] = "ssr2_aux"; sa2["command"] = "stop"; sa2["value"] = 0;
  JsonObject sa3 = sa.add<JsonObject>(); sa3["actuatorId"] = "robotdyn_triac"; sa3["command"] = "stop"; sa3["value"] = 0;
  JsonObject sa4 = sa.add<JsonObject>(); sa4["command"] = "setSafetyWarning"; sa4["message"] = "Temperature ballon depassee";
}
