#include "simulation_manager.h"

void SimulationManager::begin() {
  JsonObject root = config.systemDoc().as<JsonObject>();
  if (!root["simulation"].is<JsonObject>()) {
    JsonObject sim = root["simulation"].to<JsonObject>();
    sim["enabled"] = false;
    sim["mode"] = "manual";
    sim["scenario"] = "normal";
    sim["randomUpdateIntervalMs"] = 2000;
  }
  // Securite: la simulation ne doit jamais rester active apres un reboot.
  root["simulation"]["enabled"] = false;
  root["simulationMode"] = false;
  config.saveSystem();
  runtimeEnabled = false;
  enabledAtMs = 0;
  expiresAtMs = 0;
  state.simulationMode = false;
  state.simulationType = simulationConfig()["mode"] | "manual";
  state.simulationScenario = simulationConfig()["scenario"] | "normal";
  state.simulationRemainingMs = 0;
}

void SimulationManager::loop(uint32_t now) {
  state.simulationMode = runtimeEnabled;
  state.simulationType = simulationConfig()["mode"] | "manual";
  state.simulationScenario = simulationConfig()["scenario"] | "normal";
  if (!state.simulationMode) return;

  if (enabledAtMs == 0 || expiresAtMs == 0) markEnabled();
  if ((int32_t)(now - expiresAtMs) >= 0) {
    disable();
    state.logEvent("WARNING", "SIMULATION_TIMEOUT", "Simulation arretee automatiquement apres 5 minutes", "SimulationManager");
    return;
  }
  state.simulationRemainingMs = expiresAtMs - now;

  String mode = state.simulationType;
  if ((mode == "random" || mode == "scenario") && now - lastUpdateMs >= updateIntervalMs()) {
    lastUpdateMs = now;
    if (mode == "random") randomize();
    else applyScenario(state.simulationScenario);
  }
  applyRoutedPower();
}

bool SimulationManager::enabled() {
  return runtimeEnabled;
}

void SimulationManager::enable() {
  JsonObject sim = simulationConfig();
  // La simulation est volontairement runtime: jamais persistante apres reboot.
  sim["enabled"] = false;
  config.systemDoc()["simulationMode"] = false;
  markEnabled();
  state.logEvent("WARNING", "CONFIG_CHANGED", "Mode simulation active", "SimulationManager");
}

void SimulationManager::disable() {
  JsonObject sim = simulationConfig();
  sim["enabled"] = false;
  config.systemDoc()["simulationMode"] = false;
  runtimeEnabled = false;
  enabledAtMs = 0;
  expiresAtMs = 0;
  simulatedBaseGridPowerW = 0.0f;
  simulatedMeterAvailable = false;
  state.simulationMode = false;
  state.simulationRemainingMs = 0;
  state.heaterPowerW = 0.0f;
  state.pidOutputPercent = 0.0f;
  state.commandPercent = 0.0f;
  state.ssr1PowerPct = 0.0f;
  state.ssr2PowerPct = 0.0f;
  state.robotDynPowerPct = 0.0f;
  state.pidStatus = "IDLE";
  state.logEvent("WARNING", "ACTUATOR_FORCED_OFF", "Mode simulation desactive", "SimulationManager");
}

void SimulationManager::setMode(const String &mode) {
  String m = mode;
  m.toLowerCase();
  if (m != "off" && m != "manual" && m != "random" && m != "scenario") m = "manual";
  JsonObject sim = simulationConfig();
  sim["mode"] = m;
  sim["enabled"] = false;
  config.systemDoc()["simulationMode"] = false;
  if (m != "off") markEnabled();
  else {
    runtimeEnabled = false;
    enabledAtMs = 0;
    expiresAtMs = 0;
    state.simulationRemainingMs = 0;
  }
  saveConfig();
  state.simulationType = m;
}

void SimulationManager::setScenario(const String &scenario) {
  JsonObject sim = simulationConfig();
  sim["scenario"] = scenario.length() ? scenario : "normal";
  sim["mode"] = "scenario";
  sim["enabled"] = false;
  config.systemDoc()["simulationMode"] = false;
  markEnabled();
  saveConfig();
  applyScenario(sim["scenario"].as<String>());
}

void SimulationManager::randomize() {
  applyNormalAvailability();
  float grid = randomFloat(-2500.0f, 2500.0f);
  applyJsy(true, grid, randomFloat(215.0f, 245.0f), randomFloat(0.0f, 16.0f), randomFloat(0.85f, 1.0f), randomFloat(49.8f, 50.2f));
  applyTic(true, randomFloat(0.0f, 5000.0f), randomFloat(0.0f, 22.0f), "BASE");
  applyTemperatures(randomFloat(35.0f, 65.0f), randomFloat(30.0f, 60.0f), randomFloat(25.0f, 55.0f));
  state.logEvent("INFO", "CONFIG_CHANGED", "Simulation random mise a jour", "SimulationManager");
}

void SimulationManager::applyScenario(const String &scenario) {
  String s = scenario;
  s.toLowerCase();
  applyNormalAvailability();
  if (s == "production_low" || s == "production solaire faible") {
    applyJsy(true, randomFloat(100.0f, 1200.0f), 230, 4, 0.95f, 50.0f);
    applyTemperatures(45, 42, 38);
  } else if (s == "injection_medium" || s == "injection solaire moyenne") {
    applyJsy(true, randomFloat(-1200.0f, -200.0f), 231, 5, 0.96f, 50.0f);
    applyTemperatures(46, 42, 39);
  } else if (s == "injection_high" || s == "forte injection solaire") {
    applyJsy(true, randomFloat(-2500.0f, -1200.0f), 232, 10, 0.98f, 50.0f);
    applyTemperatures(48, 44, 40);
  } else if (s == "tank_almost_hot" || s == "ballon presque chaud") {
    applyJsy(true, -900, 231, 4, 0.96f, 50.0f);
    float maxC = config.system()["router"]["tankMaxC"] | 65.0f;
    applyTemperatures(maxC - 1, maxC - 2, maxC - 4);
  } else if (s == "tank_overheat" || s == "surchauffe ballon") {
    applyJsy(true, -800, 231, 4, 0.96f, 50.0f);
    float safeC = config.system()["router"]["tempSafetyMaxC"] | config.system()["router"]["tankSafetyC"] | 70.0f;
    applyTemperatures(safeC + 3, 55, 50);
  } else if (s == "critical_sensor_lost" || s == "perte capteur critique") {
    applyJsy(true, -800, 231, 4, 0.96f, 50.0f);
    bool available[3] = {true, true, true};
    for (JsonObject probe : config.sensorsDoc()["ds18b20"].as<JsonArray>()) {
      if (probe["critical"] | false) {
        String id = probe["id"] | "sonde1";
        uint8_t idx = id == "sonde2" ? 1 : (id == "sonde3" ? 2 : 0);
        available[idx] = false;
        break;
      }
    }
    applyTemperatures(48, 43, 39, available[0], available[1], available[2]);
  } else if (s == "jsy_lost" || s == "perte jsy") {
    applyJsy(false, 0, NAN, NAN, NAN, NAN);
    applyTemperatures(45, 42, 38);
  } else {
    applyJsy(true, randomFloat(-900.0f, -300.0f), 231, 4, 0.96f, 50.0f);
    applyTemperatures(45, 42, 38);
  }
  applyTic(true, state.consumptionW > 0 ? state.consumptionW : 900, 4.0f, "BASE");
  state.logEvent("INFO", "CONFIG_CHANGED", String("Scenario simulation: ") + scenario, "SimulationManager");
}

bool SimulationManager::setValuesFromJson(const String &json, String &error) {
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = "JSON simulation invalide";
    return false;
  }
  JsonObject jsy = doc["jsy"].as<JsonObject>();
  if (!jsy.isNull()) {
    bool available = jsy["available"] | true;
    float grid = jsy["gridPowerW"] | state.gridPowerW;
    float voltage = jsy["voltageV"] | state.gridVoltageV;
    float current = jsy["currentA"] | state.gridCurrentA;
    float pf = jsy["powerFactor"] | state.gridPowerFactor;
    float freq = jsy["frequencyHz"] | state.gridFrequencyHz;
    float p2 = jsy["activePowerW2"] | state.activePowerW2;
    float c2 = jsy["currentA2"] | state.currentA2;
    applyJsy(available, grid, voltage, current, pf, freq, p2, c2);
  }
  JsonObject tic = doc["tic"].as<JsonObject>();
  if (!tic.isNull()) applyTic(tic["available"] | true, tic["apparentPowerVA"] | state.ticApparentPowerVA, tic["currentA"] | state.ticCurrentA, tic["tariff"] | "BASE");
  JsonArray ds = doc["ds18b20"].as<JsonArray>();
  for (JsonObject item : ds) {
    String id = item["id"] | "";
    uint8_t idx = id == "sonde2" ? 1 : (id == "sonde3" ? 2 : 0);
    state.ds18b20Temps[idx] = item["temperatureC"] | state.ds18b20Temps[idx];
    state.ds18b20Available[idx] = item["available"] | true;
    state.ds18b20LastReadMs[idx] = millis();
  }
  state.tankTopC = state.ds18b20Temps[0];
  state.tankMiddleC = state.ds18b20Temps[1];
  state.tankBottomC = state.ds18b20Temps[2];
  state.logEvent("INFO", "CONFIG_CHANGED", "Valeurs simulation manuelles appliquees", "SimulationManager");
  return true;
}

void SimulationManager::toJson(JsonObject out) {
  out["enabled"] = enabled();
  out["mode"] = simulationConfig()["mode"] | "manual";
  out["scenario"] = simulationConfig()["scenario"] | "normal";
  out["randomUpdateIntervalMs"] = updateIntervalMs();
  out["timeoutMs"] = SIMULATION_TIMEOUT_MS;
  out["remainingMs"] = state.simulationRemainingMs;
  JsonObject jsy = out["jsy"].to<JsonObject>();
  jsy["available"] = state.jsyOnline;
  jsy["voltageV"] = state.gridVoltageV;
  jsy["currentA"] = state.gridCurrentA;
  jsy["activePowerW"] = state.activePowerW1;
  jsy["activePowerW1"] = state.activePowerW1;
  jsy["activePowerW2"] = state.activePowerW2;
  jsy["currentA1"] = state.currentA1;
  jsy["currentA2"] = state.currentA2;
  jsy["gridPowerW"] = state.gridPowerW;
  jsy["injectionW"] = state.injectionW;
  jsy["consumptionW"] = state.consumptionW;
  jsy["surplusW"] = state.surplusW;
  jsy["powerFactor"] = state.gridPowerFactor;
  jsy["frequencyHz"] = state.gridFrequencyHz;
  JsonObject tic = out["tic"].to<JsonObject>();
  tic["available"] = state.ticAvailable;
  tic["apparentPowerVA"] = state.ticApparentPowerVA;
  tic["currentA"] = state.ticCurrentA;
  tic["tariff"] = state.ticTariff;
  JsonArray ds = out["ds18b20"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; i++) {
    JsonObject item = ds.add<JsonObject>();
    item["id"] = String("sonde") + String(i + 1);
    item["available"] = state.ds18b20Available[i];
    item["temperatureC"] = state.ds18b20Temps[i];
  }
  JsonObject act = out["actuators"].to<JsonObject>();
  act["ssr1PowerPct"] = state.ssr1PowerPct;
  act["ssr2PowerPct"] = state.ssr2PowerPct;
  act["robotDynPowerPct"] = state.robotDynPowerPct;
  act["lastCommand"] = state.lastActuatorCommandLog;
}

JsonObject SimulationManager::simulationConfig() {
  JsonObject root = config.systemDoc().as<JsonObject>();
  if (!root["simulation"].is<JsonObject>()) root["simulation"].to<JsonObject>();
  return root["simulation"].as<JsonObject>();
}

uint32_t SimulationManager::updateIntervalMs() {
  return simulationConfig()["randomUpdateIntervalMs"] | 2000UL;
}

void SimulationManager::markEnabled() {
  // A chaque activation explicite, on redemarre le chronometre des 5 minutes.
  // Sinon une ancienne valeur enabledAtMs peut provoquer un timeout immediat.
  uint32_t now = millis();
  runtimeEnabled = true;
  enabledAtMs = now;
  expiresAtMs = now + SIMULATION_TIMEOUT_MS;
  lastUpdateMs = 0;
  state.simulationMode = true;
  state.simulationRemainingMs = SIMULATION_TIMEOUT_MS;
}

float SimulationManager::randomFloat(float minValue, float maxValue) {
  return minValue + (static_cast<float>(random(0, 10001)) / 10000.0f) * (maxValue - minValue);
}

void SimulationManager::applyPower(float gridPowerW) {
  simulatedBaseGridPowerW = gridPowerW;
  simulatedMeterAvailable = true;
  applyRoutedPower();
}

void SimulationManager::applyRoutedPower() {
  if (!simulatedMeterAvailable) return;
  const float routedW = max(0.0f, state.heaterPowerW);
  const float gridPowerW = simulatedBaseGridPowerW + routedW;
  state.gridPowerW = gridPowerW;
  state.gridPowerRawW = gridPowerW;
  state.gridPowerFilteredW = gridPowerW;
  state.gridPowerSource = "SIM";
  state.activePowerW1 = gridPowerW;
  state.energyDirection1 = gridPowerW < 0 ? "injection" : "consumption";
  state.injectionW = gridPowerW < 0 ? -gridPowerW : 0;
  state.consumptionW = gridPowerW > 0 ? gridPowerW : 0;
  state.surplusW = state.injectionW;
  state.productionW = state.injectionW + state.consumptionW;
}

void SimulationManager::applyNormalAvailability() {
  state.jsyOnline = true;
  state.ticAvailable = true;
  state.ds18b20CriticalMissing = false;
  state.ds18b20CriticalMissingList = "";
}

void SimulationManager::applyTemperatures(float t1, float t2, float t3, bool a1, bool a2, bool a3) {
  float values[3] = {t1, t2, t3};
  bool available[3] = {a1, a2, a3};
  for (uint8_t i = 0; i < 3; i++) {
    state.ds18b20Temps[i] = values[i];
    state.ds18b20Available[i] = available[i];
    state.ds18b20LastReadMs[i] = millis();
  }
  state.tankTopC = t1;
  state.tankMiddleC = t2;
  state.tankBottomC = t3;
  state.ds18b20CriticalMissing = false;
  state.ds18b20CriticalMissingList = "";
  for (JsonObject probe : config.sensorsDoc()["ds18b20"].as<JsonArray>()) {
    if (!(probe["critical"] | false)) continue;
    String id = probe["id"] | "sonde1";
    uint8_t idx = id == "sonde2" ? 1 : (id == "sonde3" ? 2 : 0);
    if (!available[idx]) {
      state.ds18b20CriticalMissing = true;
      if (state.ds18b20CriticalMissingList.length()) state.ds18b20CriticalMissingList += ",";
      state.ds18b20CriticalMissingList += id;
    }
  }
}

void SimulationManager::applyJsy(bool available, float gridPowerW, float voltageV, float currentA, float powerFactor, float frequencyHz, float activePowerW2, float currentA2) {
  state.jsyOnline = available;
  state.lastJsyReadMs = millis();
  state.gridVoltageV = voltageV;
  state.gridCurrentA = currentA;
  state.gridPowerFactor = powerFactor;
  state.gridFrequencyHz = frequencyHz;
  state.gridEnergyDirection = gridPowerW < 0 ? "injection" : "consumption";
  state.voltageV1 = voltageV;
  state.currentA1 = currentA;
  state.activePowerW1 = gridPowerW;
  state.powerFactor1 = powerFactor;
  state.energyDirection1 = state.gridEnergyDirection;
  state.voltageV2 = voltageV;
  state.currentA2 = isnan(currentA2) ? currentA : currentA2;
  state.activePowerW2 = isnan(activePowerW2) ? 0.0f : activePowerW2;
  state.powerFactor2 = powerFactor;
  state.energyDirection2 = state.activePowerW2 < 0 ? "injection" : "consumption";
  applyPower(available ? gridPowerW : 0);
}

void SimulationManager::applyTic(bool available, float apparentPowerVA, float currentA, const String &tariff) {
  state.ticAvailable = available;
  state.ticStatus = available ? "TIC_OK" : "TIC_TIMEOUT";
  state.ticApparentPowerVA = apparentPowerVA;
  state.ticCurrentA = currentA;
  state.ticTariff = tariff;
  state.ticPeriod = tariff;
  state.lastTicReadMs = millis();
}

void SimulationManager::saveConfig() {
  config.saveSystem();
}
