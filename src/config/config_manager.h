#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class Preferences;

class ConfigManager {
public:
  bool begin();
  bool loadAll();
  bool saveAll();
  bool saveDevice();
  bool saveSystem();
  bool saveSensors();
  bool saveActuators();
  bool saveRules();
  bool saveDeviceConfig();
  bool saveSystemConfig();
  bool saveSensorsConfig();
  bool saveActuatorsConfig();
  bool saveRulesConfig();
  bool resetToDefaults();
  bool backupConfigToNvs();
  bool restoreConfigFromNvs();
  void printConfigSummary();
  bool replaceFile(const char *path, const String &json, bool allowEmpty = false);
  String lastError() const { return configLastError; }
  void setLastError(const String &error) { configLastError = error; }

  JsonObject device();
  JsonObject system();
  JsonArray sensors();
  JsonArray actuators();
  JsonArray rules();

  DynamicJsonDocument &deviceDoc() { return deviceConfig; }
  DynamicJsonDocument &systemDoc() { return systemConfig; }
  DynamicJsonDocument &sensorsDoc() { return sensorsConfig; }
  DynamicJsonDocument &actuatorsDoc() { return actuatorsConfig; }
  DynamicJsonDocument &rulesDoc() { return rulesConfig; }

private:
  DynamicJsonDocument deviceConfig{2048};
  DynamicJsonDocument systemConfig{12288};
  DynamicJsonDocument sensorsConfig{16384};
  DynamicJsonDocument actuatorsConfig{8192};
  DynamicJsonDocument rulesConfig{12288};
  String configLastError = "";
  bool allowEmptyConfigWrite = false;

  bool loadOrDefault(const char *path, DynamicJsonDocument &doc, void (ConfigManager::*defaults)());
  bool saveDoc(const char *path, DynamicJsonDocument &doc);
  bool validateDoc(const char *path, DynamicJsonDocument &doc);
  bool restoreBackupFile(const char *path, DynamicJsonDocument &doc);
  bool writeDiagnosticFile(const char *path, const String &reason);
  bool backupUserConfigFile(const char *path);
  bool configLooksEmpty(const char *path, DynamicJsonDocument &doc);
  bool wouldEraseUserConfig(const char *path, DynamicJsonDocument &doc);
  bool migrateSystemWifiConfig(DynamicJsonDocument &doc, const char *context);
  bool preserveExistingWifiConfig(DynamicJsonDocument &incoming);
  bool systemWifiPresent(DynamicJsonDocument &doc);
  bool shouldRestoreConfigFromNvs(Preferences &prefs);
  void logConfigFileResult(const char *path, const char *status, size_t size, const char *parseResult);
  bool normalizeSystemConfig();
  bool normalizeSensorsConfig();
  bool normalizeActuatorsConfig();
  void backupCorruptFile(const char *path);
  void ensureConfigDir();
  void defaultDevice();
  void defaultSystem();
  void defaultSensors();
  void defaultActuators();
  void defaultRules();
};
