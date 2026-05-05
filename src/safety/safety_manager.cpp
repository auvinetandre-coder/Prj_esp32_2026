#include "safety_manager.h"

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
  const float safetyC = router["tempSafetyMaxC"] | router["tankSafetyC"] | 70.0f;
  const uint32_t jsyTimeoutMs = router["jsyTimeoutMs"] | 3000;
  const uint32_t ticTimeoutMs = router["ticTimeoutMs"] | 10000;
  const uint32_t takeoverTimeoutMs = config.system()["takeoverTimeoutMs"] | 1000;
  const bool safetyEnabled = safety["enabled"] | true;
  const bool blockMissingDs18b20 = safety["blockOnMissingDs18b20"] | true;
  const bool blockMissingTop = safety["blockOnMissingTopSensor"] | true;
  const bool blockMissingJsy = safety["blockOnMissingJsy"] | true;
  const bool blockMissingJsyAndTic = safety["blockOnMissingJsyAndTic"] | true;
  const bool warningOnlyMissingSensors = safety["warningOnlyOnMissingSensors"] | false;
  String warningCause = "";

  if (manualStop) return setLevel(SAFETY_CRITICAL, "MANUAL_EMERGENCY_STOP", "Arret manuel actif", now);
  if (!safetyEnabled) return setLevel(SAFETY_OK, "", "", now);
  if (configError()) return setLevel(SAFETY_CRITICAL, "CONFIG_ERROR", "Configuration de securite invalide", now);
  if (doubleMasterRisk()) return setLevel(SAFETY_CRITICAL, "DOUBLE_MASTER_RISK", "Risque double pilotage MASTER/BACKUP", now);
  if (topTemperatureHigh(safetyC)) return setLevel(SAFETY_CRITICAL, "TEMP_HIGH_LIMIT", "Temperature ballon au-dessus de la limite", now);

  const bool topMissing = topSensorMissing();
  const bool criticalMissing = criticalDs18b20Missing();
  if (safetyEnabled && topMissing && blockMissingTop && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "DS18B20_TOP_MISSING", "Sonde ballon haut absente", now);
  if (safetyEnabled && criticalMissing && blockMissingDs18b20 && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "DS18B20_CRITICAL_MISSING", "Sonde DS18B20 critique absente", now);
  if (topMissing) warningCause = "DS18B20_TOP_MISSING: Sonde ballon haut absente";
  else if (criticalMissing) warningCause = "DS18B20_CRITICAL_MISSING: Sonde DS18B20 critique absente";

  const bool jsyMissing = jsyConfigured() && jsyTimedOut(now, jsyTimeoutMs);
  const bool ticMissing = ticConfigured() && ticTimedOut(now, ticTimeoutMs);

  if (jsyMissing && (!ticConfigured() || ticMissing)) {
    if (safetyEnabled && blockMissingJsyAndTic && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "JSY_TIMEOUT", "JSY et TIC indisponibles", now);
    return setLevel(SAFETY_DEGRADED, "JSY_TIMEOUT", "JSY et TIC indisponibles", now);
  }
  if (jsyMissing) {
    if (safetyEnabled && blockMissingJsy && !warningOnlyMissingSensors) return setLevel(SAFETY_CRITICAL, "JSY_TIMEOUT", "JSY absent trop longtemps", now);
    return setLevel(SAFETY_DEGRADED, "JSY_TIMEOUT", "JSY absent trop longtemps", now);
  }
  if (masterLost(now, takeoverTimeoutMs)) return setLevel(SAFETY_DEGRADED, "MASTER_LOST", "MASTER perdu, fonctionnement en reprise BACKUP", now);
  if (ticMissing && state.jsyOnline) return setLevel(SAFETY_WARNING, "TIC_TIMEOUT", "TIC absente, JSY disponible", now);
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

bool SafetyManager::topTemperatureHigh(float safetyC) {
  if (!isnan(state.tankTopC) && state.tankTopC >= safetyC) return true;
  // Conserve la securite historique : toute sonde rolee ballon_* au-dessus coupe aussi.
  if (!isnan(state.tankMiddleC) && state.tankMiddleC >= safetyC) return true;
  if (!isnan(state.tankBottomC) && state.tankBottomC >= safetyC) return true;
  return false;
}

bool SafetyManager::topSensorMissing() {
  JsonArray ds = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (JsonObject sensor : ds) {
    if (!(sensor["enabled"] | false)) continue;
    if (String(sensor["role"] | "") != "ballon_haut") continue;
    String id = sensor["id"] | "";
    if (id == "sonde1") return !state.ds18b20Available[0];
    if (id == "sonde2") return !state.ds18b20Available[1];
    if (id == "sonde3") return !state.ds18b20Available[2];
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
    if (String(sensor["id"] | "") == "jsy_grid") return sensor["enabled"] | false;
  }
  return false;
}

bool SafetyManager::ticConfigured() {
  for (JsonObject sensor : config.sensors()) {
    if (String(sensor["id"] | "") == "tic_linky") return sensor["enabled"] | false;
  }
  return false;
}

bool SafetyManager::doubleMasterRisk() {
  return state.splitBrainDetected || state.redundancyState == "SPLIT_BRAIN_DETECTED" ||
         (state.role == ROLE_BACKUP && state.isActiveMaster && state.masterAlive);
}

bool SafetyManager::masterLost(uint32_t now, uint32_t takeoverTimeoutMs) {
  if (state.role != ROLE_BACKUP) return false;
  if (!state.isActiveMaster) return false;
  if (state.lastMasterHeartbeatMs == 0) return true;
  return now - state.lastMasterHeartbeatMs > takeoverTimeoutMs;
}

bool SafetyManager::configError() {
  bool hasSsr1 = false;
  bool hasSsr2 = false;
  bool hasRobotDyn = false;
  for (JsonObject actuator : config.actuators()) {
    String id = actuator["id"] | "";
    hasSsr1 |= id == "ssr1_water_heater";
    hasSsr2 |= id == "ssr2_aux";
    hasRobotDyn |= id == "robotdyn_triac";
  }
  return !hasSsr1 || !hasSsr2 || !hasRobotDyn;
}

const char *SafetyManager::levelText(SafetyLevel value) {
  switch (value) {
    case SAFETY_WARNING: return "WARNING";
    case SAFETY_DEGRADED: return "DEGRADED";
    case SAFETY_CRITICAL: return "CRITICAL";
    default: return "OK";
  }
}
