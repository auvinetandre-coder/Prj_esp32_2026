#include "sensor_manager.h"

void SensorManager::begin() {
  ds18b20.begin();
  startMetersForCurrentSource();
  state.addLog("Sensors initialized");
}

void SensorManager::loop(uint32_t now) {
  // En simulation, les valeurs capteurs viennent du SimulationManager.
  // On ne laisse donc pas les lectures physiques ecraser ces valeurs.
  if (state.simulationMode) {
    return;
  }

  startMetersForCurrentSource();
  if (jsyStarted) jsy.loop(now);
  if (ticStarted) tic.loop(now);
  ds18b20.loop(now);
  applyGridPowerSource();
}

String SensorManager::configuredGridPowerSource() {
  String source = config.system()["router"]["gridPowerSource"] | "JSY";
  source.toUpperCase();
  if (source != "TIC" && source != "AUTO") source = "JSY";
  return source;
}

bool SensorManager::meterPinsConflict() {
  int jsyRx = -1;
  int jsyTx = -1;
  int ticRx = -1;

  for (JsonObject sensor : config.sensors()) {
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    if (id == "jsy_grid" || type == "JSY-MK-194T") {
      jsyRx = sensor["rx"] | 26;
      jsyTx = sensor["tx"] | 27;
    } else if (id == "tic_linky" || type == "TIC Linky") {
      ticRx = sensor["rx"] | 26;
    }
  }

  if (ticRx < 0 || jsyRx < 0) return false;
  return ticRx == jsyRx || ticRx == jsyTx;
}

void SensorManager::startMetersForCurrentSource() {
  String source = configuredGridPowerSource();
  bool conflict = meterPinsConflict();

  // Si JSY et Linky partagent une broche, un seul UART doit etre actif.
  // AUTO garde le JSY comme reference rapide pour le routeur solaire.
  bool wantJsy = !conflict || source == "JSY" || source == "AUTO";
  bool wantTic = !conflict || source == "TIC";

  if (wantJsy && !jsyStarted) {
    jsy.begin();
    jsyStarted = true;
  }
  if (!wantJsy && jsyStarted) {
    jsyStarted = false;
    jsy.stop();
    state.addLog("JSY suspendu: source reseau TIC active");
  }

  if (wantTic && !ticStarted) {
    tic.begin();
    ticStarted = true;
  }
  if (!wantTic && ticStarted) {
    ticStarted = false;
    tic.stop();
    state.addLog("TIC suspendue: source reseau JSY active");
  }
}

void SensorManager::applyGridPowerSource() {
  String source = configuredGridPowerSource();
  float selected = NAN;

  if (source == "TIC") {
    if (state.ticAvailable && !isnan(state.ticGridPowerW)) selected = state.ticGridPowerW;
  } else if (source == "AUTO") {
    if (state.ticAvailable && !isnan(state.ticGridPowerW)) selected = state.ticGridPowerW;
    else if (state.jsyOnline && !isnan(state.jsyGridPowerW)) selected = state.jsyGridPowerW;
  } else {
    source = "JSY";
    if (state.jsyOnline && !isnan(state.jsyGridPowerW)) selected = state.jsyGridPowerW;
  }

  if (isnan(selected)) return;
  state.gridPowerSource = source;
  state.gridPowerW = selected;
  state.gridEnergyDirection = selected < 0 ? "injection" : "consumption";
  state.injectionW = selected < 0 ? -selected : 0;
  state.consumptionW = selected > 0 ? selected : 0;
  state.surplusW = state.injectionW;
}

float SensorManager::valueFor(const String &sensorId, const String &variable) {
  if (sensorId == "jsy_grid" && variable == "activePower") return state.gridPowerW;
  if (sensorId == "jsy_grid" && variable == "activePowerW1") return state.activePowerW1;
  if (sensorId == "jsy_grid" && variable == "activePowerW2") return state.activePowerW2;
  if (sensorId == "jsy_grid" && variable == "voltageV1") return state.voltageV1;
  if (sensorId == "jsy_grid" && variable == "voltageV2") return state.voltageV2;
  if (sensorId == "jsy_grid" && variable == "currentA1") return state.currentA1;
  if (sensorId == "jsy_grid" && variable == "currentA2") return state.currentA2;
  if (sensorId == "jsy_grid" && variable == "powerFactor1") return state.powerFactor1;
  if (sensorId == "jsy_grid" && variable == "powerFactor2") return state.powerFactor2;
  if (sensorId == "tic_linky" && (variable == "papp" || variable == "apparentPowerVA")) return state.ticApparentPowerVA;
  if (sensorId == "tic_linky" && variable == "gridPowerW") return state.ticGridPowerW;
  if (sensorId == "tic_linky" && variable == "currentA") return state.ticCurrentA;
  if (sensorId == "tic_linky" && variable == "energyWh") return static_cast<float>(state.ticEnergyWh);
  if (variable == "temperatureC") return ds18b20.getTemperatureById(sensorId);
  if (sensorId == "temp_tank_top" && variable == "temperature") return state.tankTopC;
  if (sensorId == "temp_tank_middle" && variable == "temperature") return state.tankMiddleC;
  if (sensorId == "temp_tank_bottom" && variable == "temperature") return state.tankBottomC;
  if (sensorId == "virtual_surplus" && variable == "surplus") return state.surplusW;
  return NAN;
}

String SensorManager::detectedDs18b20Json() {
  ds18b20.scanBus();
  return ds18b20.detectedAddressesJson();
}

String SensorManager::ds18b20StatusJson() {
  return ds18b20.getSensorStatusJson();
}

bool SensorManager::assignDs18b20(const String &sensorId, const String &address) {
  return ds18b20.assignAddress(sensorId, address);
}

void SensorManager::reloadConfiguration() {
  if (jsyStarted) jsy.stop();
  if (ticStarted) tic.stop();
  jsyStarted = false;
  ticStarted = false;
  startMetersForCurrentSource();
  ds18b20.reloadConfig();
  applyGridPowerSource();
  state.addLog("Configuration capteurs rechargee");
}
