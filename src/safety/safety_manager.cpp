#include "safety_manager.h"
#include "../diagnostics/serial_capture.h"

void SafetyManager::begin() {
  level = SAFETY_OK;
  reason = "";
  manualStop = false;
  state.safetyLevel = levelText(level);
  state.safetyReason = "";
  state.safetyTripped = false;
  state.addLog("SafetyManager pret");
}

void SafetyManager::loop() {
  loop(millis());
}

void SafetyManager::loop(uint32_t now) {
  evaluate(now);
}

void SafetyManager::evaluate(uint32_t now) {
  JsonObject router = config.system()["router"];
  JsonObject safety = config.system()["safety"];
  const float safetyC = router["tempSafetyMaxC"] | 70.0f;
  const uint32_t jsyTimeoutMs = router["jsyTimeoutMs"] | 3000;
  const uint32_t ticTimeoutMs = router["ticTimeoutMs"] | 10000;
  const bool safetyEnabled = safety["enabled"] | true;
  const bool blockMissingDs18b20 = safety["blockOnMissingDs18b20"] | true;
  const bool blockMissingTop = safety["blockOnMissingPrimarySensor"] | true;
  const bool blockMissingJsy = safety["blockOnMissingJsy"] | true;
  const bool blockMissingJsyAndTic = safety["blockOnMissingJsyAndTic"] | true;
  const bool warningOnlyMissingSensors = safety["warningOnlyOnMissingSensors"] | false;
  String warningCause = "";

  if (manualStop) return setLevel(SAFETY_CRITICAL, "MANUAL_EMERGENCY_STOP", "Arret manuel actif", now);
  if (!safetyEnabled) return setLevel(SAFETY_OK, "", "", now);
  if (configError()) return setLevel(SAFETY_CRITICAL, "CONFIG_ERROR", "Configuration de securite invalide", now);
  if (!isfinite(safetyC)) return setLevel(SAFETY_CRITICAL, "CONFIG_ERROR", "Limite temperature invalide", now);
  if (temperatureHigh(safetyC)) return setLevel(SAFETY_CRITICAL, "TEMP_HIGH_LIMIT", "Temperature equipement au-dessus de la limite", now);
  const float voltageMin = router["voltageMinV"] | 180.0f;
  const float voltageMax = router["voltageMaxV"] | 260.0f;
  const float frequencyMin = router["frequencyMinHz"] | 47.0f;
  const float frequencyMax = router["frequencyMaxHz"] | 53.0f;
  if (state.jsyOnline && !isnan(state.gridVoltageV) && state.gridVoltageV > 1.0f &&
      (state.gridVoltageV < voltageMin || state.gridVoltageV > voltageMax)) {
    return setLevel(SAFETY_CRITICAL, "VOLTAGE_OUT_OF_RANGE", "Tension reseau hors plage", now);
  }
  if (state.jsyOnline && !isnan(state.gridFrequencyHz) && state.gridFrequencyHz > 1.0f &&
      (state.gridFrequencyHz < frequencyMin || state.gridFrequencyHz > frequencyMax)) {
    return setLevel(SAFETY_CRITICAL, "FREQUENCY_OUT_OF_RANGE", "Frequence reseau hors plage", now);
  }

  const bool topMissing = primarySensorMissing();
  const bool criticalMissing = criticalDs18b20Missing();
  if (safetyEnabled && topMissing && blockMissingTop && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "DS18B20_PRIMARY_MISSING", "Sonde principale absente", now);
  if (safetyEnabled && criticalMissing && blockMissingDs18b20 && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "DS18B20_CRITICAL_MISSING", "Sonde DS18B20 critique absente", now);
  if (topMissing) warningCause = "DS18B20_PRIMARY_MISSING: Sonde principale absente";
  else if (criticalMissing) warningCause = "DS18B20_CRITICAL_MISSING: Sonde DS18B20 critique absente";

  const bool jsyMissing = jsyConfigured() && jsyTimedOut(now, jsyTimeoutMs);
  const bool ticMissing = ticConfigured() && ticTimedOut(now, ticTimeoutMs);
  const bool jsyOk = jsyConfigured() && !jsyMissing && state.jsyOnline;
  const bool ticOk = ticConfigured() && !ticMissing && state.ticAvailable;
  const bool meterConfigured = jsyConfigured() || ticConfigured();
  const bool meterAvailable = jsyOk || ticOk;

  // Une seule mesure reseau valide suffit : JSY OU Linky.
  // L'autre capteur peut etre absent sans bloquer le routeur solaire.
  if (meterConfigured && !meterAvailable) {
    String details;
    if (jsyConfigured() && ticConfigured()) details = "JSY et TIC indisponibles";
    else if (jsyConfigured()) details = "JSY indisponible";
    else details = "TIC indisponible";
    if (safetyEnabled && (blockMissingJsyAndTic || blockMissingJsy) && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "POWER_METER_TIMEOUT", details, now);
    return setLevel(SAFETY_DEGRADED, "POWER_METER_TIMEOUT", details, now);
  }
  if (warningCause.length()) return setLevel(SAFETY_WARNING, warningCause.substring(0, warningCause.indexOf(':')), warningCause.substring(warningCause.indexOf(':') + 2), now);

  setLevel(SAFETY_OK, "", "", now);
}

void SafetyManager::softwareWatchdog(uint32_t now) {
  if (now - state.watchdogSeenMs > 5000) setLevel(SAFETY_CRITICAL, "CONFIG_ERROR", "Watchdog logiciel", now);
}

void SafetyManager::clearManualStop() {
  manualStop = false;
  state.addLog("Arret manuel acquitte");
}

void SafetyManager::triggerManualStop() {
  manualStop = true;
  setLevel(SAFETY_CRITICAL, "MANUAL_EMERGENCY_STOP", "Arret manuel demande", millis());
}

void SafetyManager::printStatus() {
  Serial.println(F("=== SafetyManager status ==="));
  Serial.print(F("level=")); Serial.println(levelText(level));
  Serial.print(F("reason=")); Serial.println(reason);
  Serial.print(F("manualStop=")); Serial.println(manualStop ? F("true") : F("false"));
}

void SafetyManager::setLevel(SafetyLevel next, const String &cause, const String &details, uint32_t now) {
  String text = cause;
  if (details.length()) text += String(": ") + details;

  const bool changed = next != level || text != reason;
  level = next;
  reason = text;
  state.safetyLevel = levelText(level);
  state.safetyReason = reason;
  state.safetyTripped = level == SAFETY_CRITICAL;

  if (changed) {
    String decision = String("Safety ") + levelText(level);
    if (reason.length()) decision += String(" - ") + reason;
    state.addLog(decision);
    Serial.println(decision);
  }

  if (level == SAFETY_CRITICAL) applyCriticalCut(now);
}

void SafetyManager::applyCriticalCut(uint32_t now) {
  if (now - lastCriticalCutMs < 250) return;
  lastCriticalCutMs = now;
  actuators.lockAllForSafety(reason);
  actuators.stopCritical();
}

bool SafetyManager::temperatureHigh(float safetyC) {
  size_t index = 0;
  for (JsonObject sensor : config.sensorsDoc()["ds18b20"].as<JsonArray>()) {
    if (index >= 3) break;
    if ((sensor["enabled"] | false) && state.ds18b20Temps[index] >= safetyC) return true;
    ++index;
  }
  if (!isnan(state.temperature1C) && state.temperature1C >= safetyC) return true;
  // All configured temperature channels retain the same high-limit protection.
  if (!isnan(state.temperature2C) && state.temperature2C >= safetyC) return true;
  if (!isnan(state.temperature3C) && state.temperature3C >= safetyC) return true;
  return false;
}

bool SafetyManager::primarySensorMissing() {
  size_t index = 0;
  JsonArray ds = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (JsonObject sensor : ds) {
    const size_t slot = index++;
    if (!(sensor["enabled"] | false) || String(sensor["role"] | "") != "temperature_1") continue;
    if (slot >= 3 || !state.ds18b20Available[slot]) return true;
  }
  return false;
}

bool SafetyManager::criticalDs18b20Missing() {
  return state.ds18b20CriticalMissing;
}

bool SafetyManager::jsyTimedOut(uint32_t now, uint32_t timeoutMs) {
  if (state.jsyOnline) return false;
  if (state.lastJsyReadMs == 0) return now > timeoutMs;
  return now - state.lastJsyReadMs > timeoutMs;
}

bool SafetyManager::ticTimedOut(uint32_t now, uint32_t timeoutMs) {
  if (state.ticAvailable) return false;
  if (state.lastTicReadMs == 0) return now > timeoutMs;
  return now - state.lastTicReadMs > timeoutMs;
}

bool SafetyManager::jsyConfigured() {
  for (JsonObject sensor : config.sensors()) {
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    if (id == "jsy_grid" || type == "JSY-MK-194T") return sensor["enabled"] | false;
  }
  return false;
}

bool SafetyManager::ticConfigured() {
  for (JsonObject sensor : config.sensors()) {
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    if (id == "tic_linky" || type == "TIC Linky") return sensor["enabled"] | false;
  }
  return false;
}

bool SafetyManager::configError() {
  const String target = config.routingActuatorId();
  bool found = !target.length();
  for (JsonObject actuator : config.actuators()) {
    if (!(actuator["enabled"] | false)) continue;
    String id = actuator["id"] | "";
    if (!id.length()) return true;
    if (id == target) found = true;
  }
  return !found;
}

const char *SafetyManager::levelText(SafetyLevel value) {
  switch (value) {
    case SAFETY_WARNING: return "WARNING";
    case SAFETY_DEGRADED: return "DEGRADED";
    case SAFETY_CRITICAL: return "CRITICAL";
    default: return "OK";
  }
}
