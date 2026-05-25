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
  checkRemoteSensorTimeouts(now);
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
    String source = sensor["source"] | "local";
    if (source.equalsIgnoreCase("espnow")) continue;
    if (!(sensor["enabled"] | true)) continue;
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

  bool jsyConfigured = false;
  bool ticConfigured = false;
  bool ticIsEspNow = false;
  for (JsonObject sensor : config.sensors()) {
    if (!(sensor["enabled"] | true)) continue;
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    String sensorSource = sensor["source"] | "local";
    if (id == "jsy_grid" || type == "JSY-MK-194T") jsyConfigured = true;
    if (id == "tic_linky" || type == "TIC Linky") {
      ticConfigured = true;
      if (sensorSource.equalsIgnoreCase("espnow")) ticIsEspNow = true;
    }
  }

  // gridPowerSource choisit seulement la mesure officielle du routeur.
  // Les compteurs restent actifs pour diagnostic/comparaison, sauf conflit UART local.
  bool wantJsy = jsyConfigured;
  bool wantTic = ticConfigured && !ticIsEspNow;
  if (conflict) {
    wantJsy = source != "TIC";
    wantTic = source == "TIC";
  }

  if (wantJsy && !jsyStarted) {
    jsy.begin();
    jsyStarted = true;
  }
  if (!wantJsy && jsyStarted) {
    jsyStarted = false;
    jsy.stop();
    state.addLog("JSY suspendu: conflit UART local avec TIC");
  }

  if (wantTic && !ticStarted) {
    tic.begin();
    ticStarted = true;
  }
  if (!wantTic && ticStarted) {
    ticStarted = false;
    tic.stop();
    state.addLog(ticIsEspNow ? "TIC locale inactive: source ESP-NOW" : "TIC suspendue: conflit UART local avec JSY");
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
  state.gridPowerRawW = selected;
  const float alpha = constrain(config.system()["router"]["alphaFilter"] | 0.25f, 0.01f, 1.0f);
  if (isnan(state.gridPowerFilteredW)) state.gridPowerFilteredW = selected;
  else state.gridPowerFilteredW = (alpha * selected) + ((1.0f - alpha) * state.gridPowerFilteredW);
  state.gridPowerSource = source;
  state.gridPowerW = state.gridPowerFilteredW;
  state.gridEnergyDirection = state.gridPowerW < 0 ? "injection" : "consumption";
  state.injectionW = state.gridPowerW < 0 ? -state.gridPowerW : 0;
  state.consumptionW = state.gridPowerW > 0 ? state.gridPowerW : 0;
  state.surplusW = state.injectionW;
}

bool SensorManager::updateRemoteSensor(const String &sourceMac, const EspNowSensorPacket &packet) {
  RemoteSensorRuntime *slot = findOrCreateRemoteSensor(packet.nodeId, packet.sensorId, sourceMac);
  if (!slot) {
    state.addLog("ESP-NOW capteur distant ignore: table pleine");
    return false;
  }

  const bool wasTimedOut = slot->timedOut;
  slot->sourceMac = sourceMac;
  slot->nodeId = packet.nodeId;
  espNowCopyFixedText(slot->nodeName, sizeof(slot->nodeName), packet.nodeName);
  slot->sensorId = packet.sensorId;
  espNowCopyFixedText(slot->sensorName, sizeof(slot->sensorName), packet.sensorName);
  slot->sensorType = packet.sensorType;
  slot->origin = SENSOR_ORIGIN_ESPNOW;
  slot->ok = packet.sensorOk;
  if (packet.sensorOk) slot->timedOut = false;
  slot->lastUpdateMs = millis();
  updateRemotePacketLoss(*slot, packet.sequence);
  slot->valueCount = min<uint8_t>(packet.valueCount, ESPNOW_MAX_SENSOR_VALUES);
  for (uint8_t i = 0; i < slot->valueCount; i++) slot->values[i] = packet.values[i];

  if (wasTimedOut && packet.sensorOk) {
    state.addLog(String("ESP-NOW capteur distant revenu: nodeId=") + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
  }
  applyRemoteSensorToState(*slot);
  return true;
}

bool SensorManager::updateRemoteSensorFast(const String &sourceMac, const EspNowFastSensorPacket &packet) {
  RemoteSensorRuntime *slot = findOrCreateRemoteSensor(packet.nodeId, packet.sensorId, sourceMac);
  if (!slot) {
    state.addLog("ESP-NOW fast ignore: table capteurs distants pleine");
    return false;
  }

  const bool wasTimedOut = slot->timedOut;
  slot->sourceMac = sourceMac;
  slot->nodeId = packet.nodeId;
  slot->sensorId = packet.sensorId;
  slot->sensorType = packet.sensorType;
  slot->origin = SENSOR_ORIGIN_ESPNOW;
  slot->ok = packet.sensorOk;
  if (packet.sensorOk) slot->timedOut = false;
  slot->lastUpdateMs = millis();
  updateRemotePacketLoss(*slot, packet.sequence);
  slot->valueCount = min<uint8_t>(packet.valueCount, ESPNOW_MAX_FAST_VALUES);
  for (uint8_t i = 0; i < slot->valueCount; i++) {
    slot->values[i].valueType = packet.values[i].valueType;
    slot->values[i].value = packet.values[i].value;
    setDefaultValueMetadata(slot->values[i]);
  }

  if (wasTimedOut && packet.sensorOk) {
    state.addLog(String("ESP-NOW capteur distant revenu: nodeId=") + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
  }
  applyRemoteSensorToState(*slot);
  return true;
}

bool SensorManager::updateRemoteSensorDiscovery(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet) {
  RemoteSensorRuntime *slot = findOrCreateRemoteSensor(packet.nodeId, packet.sensorId, sourceMac);
  if (!slot) {
    state.addLog("ESP-NOW discovery capteur ignore: table pleine");
    return false;
  }
  slot->sourceMac = sourceMac;
  slot->nodeId = packet.nodeId;
  espNowCopyFixedText(slot->nodeName, sizeof(slot->nodeName), packet.nodeName);
  slot->sensorId = packet.sensorId;
  espNowCopyFixedText(slot->sensorName, sizeof(slot->sensorName), packet.sensorName);
  slot->sensorType = packet.sensorType;
  slot->origin = SENSOR_ORIGIN_ESPNOW;
  slot->lastDiscoveryMs = millis();
  const uint8_t count = min<uint8_t>(packet.valueCount, min<uint8_t>(ESPNOW_MAX_FAST_VALUES, ESPNOW_MAX_SENSOR_VALUES));
  for (uint8_t i = 0; i < count; i++) {
    slot->values[i].valueType = packet.values[i].valueType;
    espNowCopyFixedText(slot->values[i].key, sizeof(slot->values[i].key), packet.values[i].key);
    espNowCopyFixedText(slot->values[i].unit, sizeof(slot->values[i].unit), packet.values[i].unit);
  }
  if (slot->valueCount < count) slot->valueCount = count;
  return true;
}

void SensorManager::updateRemoteDiagnostic(const String &sourceMac, const EspNowDiagnosticPacket &packet) {
  RemoteSensorRuntime *slot = findOrCreateRemoteSensor(packet.nodeId, 0, sourceMac);
  if (!slot) return;
  slot->sourceMac = sourceMac;
  slot->nodeId = packet.nodeId;
  slot->sensorId = 0;
  slot->sensorType = SENSOR_ROUTER;
  slot->origin = SENSOR_ORIGIN_ESPNOW;
  slot->lastDiagnosticMs = millis();
  slot->lostPackets = packet.lostPackets;
  slot->receivedPackets = packet.receivedCount;
  slot->lastError = packet.lastError;
}

void SensorManager::checkRemoteSensorTimeouts(uint32_t now) {
  for (RemoteSensorRuntime &sensor : remoteSensors) {
    if (!sensor.used || sensor.timedOut || !sensor.ok || !sensor.lastUpdateMs) continue;
    if (now - sensor.lastUpdateMs <= remoteTimeoutForType(sensor.sensorType)) continue;
    sensor.ok = false;
    sensor.timedOut = true;
    state.addLog(String("ESP-NOW capteur distant perdu: nodeId=") + sensor.nodeId + " sensorId=" + sensor.sensorId + " name=" + sensor.sensorName + " ageMs=" + String(now - sensor.lastUpdateMs) + " timeoutMs=" + String(remoteTimeoutForType(sensor.sensorType)));
  }
}

void SensorManager::remoteSensorsToJson(JsonArray out) {
  const uint32_t now = millis();
  for (const RemoteSensorRuntime &sensor : remoteSensors) {
    if (!sensor.used) continue;
    JsonObject item = out.add<JsonObject>();
    item["origin"] = "espnow";
    item["mac"] = sensor.sourceMac;
    item["nodeId"] = sensor.nodeId;
    item["nodeName"] = sensor.nodeName;
    item["sensorId"] = sensor.sensorId;
    item["sensorName"] = sensor.sensorName;
    item["sensorRole"] = sensor.sensorRole;
    item["sensorType"] = sensor.sensorType;
    item["sensorTypeText"] = espNowSensorTypeText(sensor.sensorType);
    item["ok"] = sensor.ok;
    item["timedOut"] = sensor.timedOut;
    item["lastSequence"] = sensor.lastSequence;
    item["lostPackets"] = sensor.lostPackets;
    item["receivedPackets"] = sensor.receivedPackets;
    item["packetLossPercent"] = sensor.receivedPackets ? (100.0f * sensor.lostPackets) / (sensor.receivedPackets + sensor.lostPackets) : 0.0f;
    item["lastError"] = sensor.lastError;
    item["ageMs"] = sensor.lastUpdateMs ? now - sensor.lastUpdateMs : 4294967295UL;
    item["lastDiscoveryAgeMs"] = sensor.lastDiscoveryMs ? now - sensor.lastDiscoveryMs : 4294967295UL;
    item["lastDiagnosticAgeMs"] = sensor.lastDiagnosticMs ? now - sensor.lastDiagnosticMs : 4294967295UL;
    JsonArray values = item["values"].to<JsonArray>();
    for (uint8_t i = 0; i < sensor.valueCount && i < ESPNOW_MAX_SENSOR_VALUES; i++) {
      JsonObject v = values.add<JsonObject>();
      v["valueType"] = sensor.values[i].valueType;
      v["key"] = sensor.values[i].key;
      v["value"] = sensor.values[i].value;
      v["unit"] = sensor.values[i].unit;
    }
  }
}

uint32_t SensorManager::remoteTimeoutForType(uint8_t sensorType) {
  switch (sensorType) {
    case SENSOR_JSY: return 1000UL;
    case SENSOR_LINKY: return 8000UL;
    case SENSOR_DS18B20:
    case SENSOR_TEMP_HUM: return 30000UL;
    case SENSOR_BATTERY: return 15000UL;
    default: return REMOTE_SENSOR_TIMEOUT_MS;
  }
}

void SensorManager::updateRemotePacketLoss(RemoteSensorRuntime &sensor, uint32_t sequence) {
  if (sensor.receivedPackets > 0 && sequence > sensor.lastSequence + 1) {
    sensor.lostPackets += sequence - sensor.lastSequence - 1;
  }
  sensor.lastSequence = sequence;
  sensor.receivedPackets++;
}

void SensorManager::setDefaultValueMetadata(EspNowSensorValue &value) {
  switch (value.valueType) {
    case VALUE_POWER_W:
      espNowCopyFixedText(value.key, sizeof(value.key), "POWER");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "W");
      break;
    case VALUE_GRID_POWER_W:
      espNowCopyFixedText(value.key, sizeof(value.key), "GRID");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "W");
      break;
    case VALUE_VOLTAGE_V:
      espNowCopyFixedText(value.key, sizeof(value.key), "VOLT");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "V");
      break;
    case VALUE_CURRENT_A:
      espNowCopyFixedText(value.key, sizeof(value.key), "CURR");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "A");
      break;
    case VALUE_APPARENT_POWER_VA:
      espNowCopyFixedText(value.key, sizeof(value.key), "PAPP");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "VA");
      break;
    case VALUE_TEMPERATURE_C:
      espNowCopyFixedText(value.key, sizeof(value.key), "TEMP");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "C");
      break;
    case VALUE_HUMIDITY_PERCENT:
      espNowCopyFixedText(value.key, sizeof(value.key), "HUM");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "%");
      break;
    case VALUE_BATTERY_VOLTAGE_V:
      espNowCopyFixedText(value.key, sizeof(value.key), "BATV");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "V");
      break;
    case VALUE_BATTERY_CURRENT_A:
      espNowCopyFixedText(value.key, sizeof(value.key), "BATA");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "A");
      break;
    case VALUE_BATTERY_SOC_PERCENT:
      espNowCopyFixedText(value.key, sizeof(value.key), "SOC");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "%");
      break;
    default:
      espNowCopyFixedText(value.key, sizeof(value.key), "VALUE");
      espNowCopyFixedText(value.unit, sizeof(value.unit), "");
      break;
  }
}

SensorManager::RemoteSensorRuntime *SensorManager::findOrCreateRemoteSensor(uint8_t nodeId, uint8_t sensorId, const String &sourceMac) {
  RemoteSensorRuntime *freeSlot = nullptr;
  for (RemoteSensorRuntime &sensor : remoteSensors) {
    if (sensor.used && sensor.origin == SENSOR_ORIGIN_ESPNOW && sensor.nodeId == nodeId && sensor.sensorId == sensorId) return &sensor;
    if (sensor.used && sensor.sourceMac.equalsIgnoreCase(sourceMac) && sensor.sensorId == sensorId) return &sensor;
    if (!sensor.used && !freeSlot) freeSlot = &sensor;
  }
  if (!freeSlot) return nullptr;
  freeSlot->used = true;
  return freeSlot;
}

JsonObject SensorManager::configuredEspNowSensorFor(const RemoteSensorRuntime &remote) {
  for (JsonObject sensor : config.sensors()) {
    if (!(sensor["enabled"] | true)) continue;
    String source = sensor["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) continue;
    String mac = sensor["mac"] | "";
    if (!mac.equalsIgnoreCase(remote.sourceMac)) continue;
    if (sensor["remoteSensorId"].is<uint8_t>() && (sensor["remoteSensorId"] | 0) != remote.sensorId) continue;

    String type = sensor["type"] | "";
    type.toUpperCase();
    bool typeMatches = false;
    if ((type.indexOf("TIC") >= 0 || type.indexOf("LINKY") >= 0) && remote.sensorType == SENSOR_LINKY) typeMatches = true;
    if (type.indexOf("JSY") >= 0 && remote.sensorType == SENSOR_JSY) typeMatches = true;
    if ((type.indexOf("DS18") >= 0 || type.indexOf("TEMP") >= 0) && (remote.sensorType == SENSOR_DS18B20 || remote.sensorType == SENSOR_TEMP_HUM)) typeMatches = true;
    if (type.indexOf("BAT") >= 0 && remote.sensorType == SENSOR_BATTERY) typeMatches = true;
    if ((type.indexOf("SOLAR") >= 0 || type.indexOf("SOLAIRE") >= 0) && remote.sensorType == SENSOR_SOLAR) typeMatches = true;
    if (!typeMatches && type.length()) continue;
    return sensor;
  }
  return JsonObject();
}

void SensorManager::applyRemoteSensorToState(const RemoteSensorRuntime &sensor) {
  for (uint8_t i = 0; i < sensor.valueCount && i < ESPNOW_MAX_SENSOR_VALUES; i++) {
    applyRemoteValueToState(sensor, sensor.values[i]);
  }
}

void SensorManager::applyRemoteValueToState(const RemoteSensorRuntime &sensor, const EspNowSensorValue &value) {
  JsonObject configSensor = configuredEspNowSensorFor(sensor);
  if (configSensor.isNull()) return;

  String key = value.key;
  key.trim();
  key.toUpperCase();
  String remoteKey = configSensor["remoteKey"] | configSensor["key"] | "";
  remoteKey.trim();
  remoteKey.toUpperCase();
  const bool importAll = !remoteKey.length() || remoteKey == "ALL" || remoteKey == "*";
  if (!importAll && remoteKey != key) return;

  String role = configSensor["role"] | "";
  role.toLowerCase();
  const uint32_t now = millis();
  const uint8_t type = sensor.sensorType;
  const uint8_t valueType = value.valueType ? value.valueType : espNowValueTypeFromKey(key.c_str());

  if (type == SENSOR_LINKY) {
    state.ticAvailable = sensor.ok;
    state.ticStatus = sensor.ok ? "ESP_NOW_OK" : "ESP_NOW_SENSOR_ERROR";
    state.lastTicReadMs = now;
    if (valueType == VALUE_GRID_POWER_W || key == "GRID") state.ticGridPowerW = value.value;
    else if (valueType == VALUE_APPARENT_POWER_VA || key == "PAPP" || key == "SINSTS") state.ticApparentPowerVA = value.value;
    else if (valueType == VALUE_CURRENT_A || key == "IINST") state.ticCurrentA = value.value;
    else if (valueType == VALUE_ENERGY_KWH || key == "BASE" || key == "ENERGY") state.ticEnergyWh = static_cast<uint64_t>(max(0.0f, value.value));
    return;
  }

  if (type == SENSOR_JSY) {
    state.jsyOnline = sensor.ok;
    state.lastJsyReadMs = now;
    if (valueType == VALUE_GRID_POWER_W || key == "GRID" || key == "POWER") {
      state.jsyGridPowerW = value.value;
      state.gridPowerRawW = value.value;
    } else if (valueType == VALUE_VOLTAGE_V || key == "VOLT") state.gridVoltageV = value.value;
    else if (valueType == VALUE_CURRENT_A || key == "CURR") state.gridCurrentA = value.value;
    else if (valueType == VALUE_POWER_FACTOR || key == "PF") state.gridPowerFactor = value.value;
    else if (valueType == VALUE_FREQUENCY_HZ || key == "FREQ") state.gridFrequencyHz = value.value;
    return;
  }

  if (type == SENSOR_DS18B20 || type == SENSOR_TEMP_HUM) {
    if (valueType != VALUE_TEMPERATURE_C && key != "TEMP") return;
    if (role.indexOf("haut") >= 0 || role.indexOf("top") >= 0 || role == "ballon_haut") {
      state.tankTopC = value.value;
      state.ds18b20Temps[0] = value.value;
      state.ds18b20Available[0] = sensor.ok;
      state.ds18b20LastReadMs[0] = now;
    } else if (role.indexOf("milieu") >= 0 || role.indexOf("middle") >= 0 || role == "ballon_milieu") {
      state.tankMiddleC = value.value;
      state.ds18b20Temps[1] = value.value;
      state.ds18b20Available[1] = sensor.ok;
      state.ds18b20LastReadMs[1] = now;
    } else if (role.indexOf("bas") >= 0 || role.indexOf("bottom") >= 0 || role == "ballon_bas") {
      state.tankBottomC = value.value;
      state.ds18b20Temps[2] = value.value;
      state.ds18b20Available[2] = sensor.ok;
      state.ds18b20LastReadMs[2] = now;
    }
    return;
  }

  if (type == SENSOR_BATTERY) {
    state.batteryOnline = sensor.ok;
    state.lastBatteryReadMs = now;
    if (valueType == VALUE_BATTERY_VOLTAGE_V || key == "BATV" || key == "VOLT") state.batteryVoltageV = value.value;
    else if (valueType == VALUE_BATTERY_CURRENT_A || key == "BATA" || key == "CURR") state.batteryCurrentA = value.value;
    else if (valueType == VALUE_POWER_W || key == "BATP" || key == "POWER") state.batteryPowerW = value.value;
    else if (valueType == VALUE_BATTERY_SOC_PERCENT || key == "SOC") state.batterySocPct = value.value;
    return;
  }

  if (type == SENSOR_SOLAR && (valueType == VALUE_POWER_W || key == "POWER" || key == "PROD")) {
    state.productionW = max(0.0f, value.value);
  }
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
  if (sensorId == "battery" && variable == "voltageV") return state.batteryVoltageV;
  if (sensorId == "battery" && variable == "currentA") return state.batteryCurrentA;
  if (sensorId == "battery" && variable == "powerW") return state.batteryPowerW;
  if (sensorId == "battery" && variable == "socPct") return state.batterySocPct;
  if (sensorId == "solar" && variable == "powerW") return state.productionW;
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
