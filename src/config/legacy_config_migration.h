#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Import boundary only: old keys never leave the configuration API.
inline bool migrateConfigKey(JsonObject object, const char *oldKey, const char *newKey) {
  if (!object.containsKey(oldKey)) return false;
  if (!object.containsKey(newKey)) object[newKey].set(object[oldKey]);
  object.remove(oldKey);
  return true;
}

inline bool migrateGenericConfig(const char *path, JsonObject root) {
  bool changed = false;
  if (strcmp(path, "/config/system.json") == 0) {
    JsonObject router = root["router"];
    changed |= migrateConfigKey(router, "heaterMaxPowerW", "outputMaxPowerW");
    changed |= migrateConfigKey(router, "tankMaxC", "temperatureMaxC");
    changed |= migrateConfigKey(router, "tankSafetyC", "tempSafetyMaxC");
    changed |= migrateConfigKey(root["safety"], "blockOnMissingTopSensor", "blockOnMissingPrimarySensor");
    for (JsonObject graph : root["dashboard"]["graphs"].as<JsonArray>()) {
      for (JsonObject series : graph["series"].as<JsonArray>()) {
        const char *oldKeys[] = {"heaterPowerW", "heaterPowerPositiveW", "heaterPercent", "tankTempC"};
        const char *newKeys[] = {"outputPowerW", "outputPowerPositiveW", "outputPercent", "primaryTemperatureC"};
        for (size_t i = 0; i < 4; ++i) {
          if (String(series["valueKey"] | "") == oldKeys[i]) { series["valueKey"] = newKeys[i]; changed = true; }
        }
        if (String(series["label"] | "") == "Ballon") { series["label"] = "Temperature"; changed = true; }
      }
    }
  }
  if (strcmp(path, "/config/rules.json") == 0) {
    for (JsonObject rule : root["rules"].as<JsonArray>()) {
      if (String(rule["name"] | "") == "Routage solaire chauffe-eau SSR1") { rule["name"] = "Routage solaire SSR1"; changed = true; }
      if (String(rule["name"] | "") == "Securite temperature ballon") { rule["name"] = "Securite temperature"; changed = true; }
      for (JsonObject action : rule["actions"].as<JsonArray>()) {
        changed |= migrateConfigKey(action, "maxHeaterPowerW", "maxOutputPowerW");
        if (String(action["message"] | "") == "Temperature ballon depassee") { action["message"] = "Limite de temperature depassee"; changed = true; }
      }
    }
  }
  if (strcmp(path, "/config/sensors.json") == 0) {
    for (const char *collection : {"sensors", "ds18b20"}) {
      for (JsonObject sensor : root[collection].as<JsonArray>()) {
        const char *oldRoles[] = {"ballon_haut", "ballon_milieu", "ballon_bas", "depart_eau_chaude", "retour_eau_froide"};
        const char *newRoles[] = {"temperature_1", "temperature_2", "temperature_3", "temperature_1", "temperature_2"};
        for (size_t i = 0; i < 5; ++i) {
          if (String(sensor["role"] | "") == oldRoles[i]) { sensor["role"] = newRoles[i]; changed = true; }
        }
      }
    }
  }
  if (strcmp(path, "/config/actuators.json") == 0) {
    for (JsonObject actuator : root["actuators"].as<JsonArray>()) {
      if (String(actuator["usage"] | "") == "water_heater") { actuator["usage"] = "routing"; changed = true; }
      for (const char *key : {"heater", "waterHeater", "isWaterHeater"}) {
        if (!actuator.containsKey(key)) continue;
        if (!actuator.containsKey("usage") && (actuator[key] | false)) actuator["usage"] = "routing";
        actuator.remove(key); changed = true;
      }
      if (String(actuator["name"] | "") == "SSR1 chauffe-eau principal") { actuator["name"] = "SSR1 sortie principale"; changed = true; }
    }
  }
  return changed;
}

// Keep installed equipment IDs stable, including remote/MQTT references.
inline const char *legacyRoutingActuatorId() { return "ssr1_water_heater"; }
inline String genericTemperatureId(const String &id) {
  if (id == "temp_tank_top") return "temperature_1";
  if (id == "temp_tank_middle") return "temperature_2";
  if (id == "temp_tank_bottom") return "temperature_3";
  return id;
}
