#pragma once
#include <ArduinoJson.h>
#include <math.h>

// Import boundary only. No historical command is executable by the PID engine.
inline bool migratePIDActions(JsonObject root, JsonObject router, JsonArray sensors) {
  bool changed = false;
  for (JsonObject rule : root["rules"].as<JsonArray>()) {
    if (!rule["actions"].is<JsonArray>() && rule["then"].is<JsonArray>()) {
      rule["actions"].set(rule["then"]); changed = true;
    }
    if (!rule["elseActions"].is<JsonArray>() && rule["else"].is<JsonArray>()) {
      rule["elseActions"].set(rule["else"]); changed = true;
    }
    for (const char *branch : {"actions", "elseActions", "then", "else"}) {
      for (JsonObject a : rule[branch].as<JsonArray>()) {
        const String command = a["command"] | "";
        const bool legacyMeter = command == "setPowerFromLinky";
        const bool regulation = legacyMeter || command == "setPowerFromSurplus" || command == "setActuatorPercentFromSurplus";
        if (!regulation || (!legacyMeter && (a["pidSchema"] | 0) >= 1)) continue;
        if (legacyMeter) { a["command"] = "setPowerFromSurplus"; a["regulation"] = "PID"; }
        const bool proportional = String(a["regulation"] | "PID") == "PROPORTIONAL";
        if (!proportional) {
          if (!a.containsKey("pidSensorId")) {
            String id = "grid";
            if (legacyMeter) {
              id = "unavailable";
              unsigned count = 0;
              for (JsonObject sensor : sensors) {
                if (String(sensor["type"] | "") == "TIC Linky" && (sensor["enabled"] | true)) { id = sensor["id"] | "unavailable"; ++count; }
              }
              if (count != 1) id = "unavailable";
            }
            a["pidSensorId"] = id;
          }
          if (!a.containsKey("pidMeasurement")) a["pidMeasurement"] = "gridPowerW";
          if (!a.containsKey("gridSetpointW")) a["gridSetpointW"] = legacyMeter ? 100.0f : (router["gridSetpointW"] | 0.0f);
          if (!a.containsKey("kp")) a["kp"] = router["kp"] | router["pidKp"] | 0.02f;
          if (!a.containsKey("ki")) a["ki"] = router["ki"] | router["pidKi"] | 0.002f;
          if (!a.containsKey("kd")) a["kd"] = router["kd"] | router["pidKd"] | 0.0f;
          if (!a.containsKey("deadbandW")) a["deadbandW"] = router["deadbandW"] | 30.0f;
          if (!a.containsKey("pidEnabled")) a["pidEnabled"] = router["pidEnabled"] | true;
          if (!a.containsKey("maxPercent")) a["maxPercent"] = 100;
          if (!a.containsKey("maxOutputRampPercentPerSecond")) a["maxOutputRampPercentPerSecond"] = router["maxOutputRampPercentPerSecond"] | 5.0f;
          if (!a.containsKey("maxOutputDropPercentPerSecond")) a["maxOutputDropPercentPerSecond"] = legacyMeter
              ? (router["maxOutputDropPercentPerSecond"] | 100.0f) : (router["maxOutputRampPercentPerSecond"] | 5.0f);
          if (!a.containsKey("measurementFilterMs")) {
            const float alpha = router["alphaFilter"] | 0.25f;
            a["measurementFilterMs"] = isfinite(alpha) && alpha > 0 && alpha < 1 ? -250.0f / logf(1.0f - alpha) : 0.0f;
          }
          if (!a.containsKey("derivativeFilterMs")) a["derivativeFilterMs"] = 500;
          if (!a.containsKey("pidIntervalMs")) a["pidIntervalMs"] = 200;
          if (!a.containsKey("measurementTimeoutMs")) a["measurementTimeoutMs"] = legacyMeter ? 10000 : 5000;
          if (!a.containsKey("requireProduction")) a["requireProduction"] = legacyMeter;
        }
        a["pidSchema"] = 1;
        changed = true;
      }
    }
  }
  return changed;
}
