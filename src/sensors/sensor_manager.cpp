#include "sensor_manager.h"

void SensorManager::begin() {
  ds18b20.begin();
  jsy.begin();
  tic.begin();
  state.addLog("Sensors initialized");
}

void SensorManager::loop(uint32_t now) {
  // En simulation, les valeurs capteurs viennent du SimulationManager.
  // On ne laisse donc pas les lectures physiques ecraser ces valeurs.
  if (state.simulationMode) {
    return;
  }

  jsy.loop(now);
  tic.loop(now);
  ds18b20.loop(now);
  applyGridPowerSource();
}

void SensorManager::applyGridPowerSource() {
  String source = config.system()["router"]["gridPowerSource"] | "JSY";
  source.toUpperCase();
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
  return ds18b20.detectedAddressesJson();
}

String SensorManager::ds18b20StatusJson() {
  return ds18b20.getSensorStatusJson();
}

bool SensorManager::assignDs18b20(const String &sensorId, const String &address) {
  return ds18b20.assignAddress(sensorId, address);
}
