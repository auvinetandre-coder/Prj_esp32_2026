#include "sensor_manager.h"

namespace {
bool espNowRemoteValuePlausible(const EspNowSensorValue &value) {
  if (isnan(value.value) || isinf(value.value)) return false;
  const float v = value.value;
  switch (value.valueType) {
    case VALUE_POWER_W:
    case VALUE_GRID_POWER_W:
    case VALUE_APPARENT_POWER_VA:
      return fabsf(v) <= 100000.0f;
    case VALUE_VOLTAGE_V:
    case VALUE_BATTERY_VOLTAGE_V:
      return v >= 0.0f && v <= 300.0f;
    case VALUE_CURRENT_A:
    case VALUE_BATTERY_CURRENT_A:
      return fabsf(v) <= 500.0f;
    case VALUE_POWER_FACTOR:
      return fabsf(v) <= 1.2f;
    case VALUE_FREQUENCY_HZ:
      return v >= 45.0f && v <= 55.0f;
    case VALUE_TEMPERATURE_C:
      return v >= -30.0f && v <= 125.0f;
    case VALUE_HUMIDITY_PERCENT:
    case VALUE_BATTERY_SOC_PERCENT:
      return v >= 0.0f && v <= 100.0f;
    case VALUE_STATE_BOOL:
      return fabsf(v) <= 1.2f;
    case VALUE_RSSI_DBM:
      return v >= -120.0f && v <= 10.0f;
    case VALUE_UNKNOWN:
    case VALUE_CUSTOM:
    default:
      return fabsf(v) <= 100000.0f;
  }
}
}

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

  bool localJsyConfigured = false;
  bool ticConfigured = false;
  bool ticIsEspNow = false;
  for (JsonObject sensor : config.sensors()) {
    if (!(sensor["enabled"] | true)) continue;
    String id = sensor["id"] | "";
    String type = sensor["type"] | "";
    String sensorSource = sensor["source"] | "local";
    if (!sensorSource.equalsIgnoreCase("espnow") && (id == "jsy_grid" || type == "JSY-MK-194T")) localJsyConfigured = true;
    if (id == "tic_linky" || type == "TIC Linky") {
      ticConfigured = true;
      if (sensorSource.equalsIgnoreCase("espnow")) ticIsEspNow = true;
    }
  }

  // gridPowerSource choisit seulement la mesure officielle du routeur.
  // Les compteurs restent actifs pour diagnostic/comparaison, sauf conflit UART local.
  bool wantJsy = localJsyConfigured;
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
  for (uint8_t i = 0; i < packet.valueCount && i < ESPNOW_MAX_SENSOR_VALUES; i++) {
    if (!espNowRemoteValuePlausible(packet.values[i])) {
      state.addLog(String("ESP-NOW valeur capteur rejetee: node=") + slot->nodeId + " sensorId=" + slot->sensorId + " key=" + packet.values[i].key + " valueType=" + packet.values[i].valueType + " value=" + String(packet.values[i].value, 6));
      continue;
    }
    mergeRemoteValue(*slot, packet.values[i]);
  }

  if (wasTimedOut && packet.sensorOk) {
    state.addLog(String("ESP-NOW capteur distant revenu: node=") + remoteNodeLabel(*slot) + " nodeId=" + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
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

  for (uint8_t i = 0; i < MAX_REMOTE_SENSOR_VALUES; i++) {
    slot->values[i] = EspNowSensorValue{};
  }
  slot->valueCount = 0;
  const uint8_t count = min<uint8_t>(packet.valueCount, ESPNOW_MAX_FAST_VALUES);
  for (uint8_t i = 0; i < count; i++) {
    EspNowSensorValue value{};
    value.valueType = packet.values[i].valueType;
    value.value = packet.values[i].value;
    setDefaultValueMetadata(value);
    if (!espNowRemoteValuePlausible(value)) {
      state.addLog(String("ESP-NOW valeur FAST rejetee: node=") + slot->nodeId + " sensorId=" + slot->sensorId + " key=" + value.key + " valueType=" + value.valueType + " value=" + String(value.value, 6));
      continue;
    }
    if (slot->valueCount < MAX_REMOTE_SENSOR_VALUES) slot->values[slot->valueCount++] = value;
  }

  if (wasTimedOut && packet.sensorOk) {
    state.addLog(String("ESP-NOW capteur distant revenu: node=") + remoteNodeLabel(*slot) + " nodeId=" + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
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
  espNowCopyFixedText(slot->firmwareVersion, sizeof(slot->firmwareVersion), packet.firmwareVersion);
  espNowCopyFixedText(slot->littlefsVersion, sizeof(slot->littlefsVersion), packet.littlefsVersion);
  slot->sensorId = packet.sensorId;
  espNowCopyFixedText(slot->sensorName, sizeof(slot->sensorName), packet.sensorName);
  espNowCopyFixedText(slot->sensorRole, sizeof(slot->sensorRole), packet.sensorRole);
  slot->sensorType = packet.sensorType;
  slot->origin = SENSOR_ORIGIN_ESPNOW;
  slot->lastDiscoveryMs = millis();
  const uint8_t count = min<uint8_t>(packet.valueCount, min<uint8_t>(ESPNOW_MAX_FAST_VALUES, ESPNOW_MAX_SENSOR_VALUES));
  for (uint8_t i = 0; i < count; i++) {
    EspNowSensorValue value{};
    value.valueType = packet.values[i].valueType;
    espNowCopyFixedText(value.key, sizeof(value.key), packet.values[i].key);
    espNowCopyFixedText(value.unit, sizeof(value.unit), packet.values[i].unit);
    mergeRemoteValue(*slot, value);
  }
  syncConfiguredRemoteSensorRole(*slot);
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
  slot->rssiDbm = packet.rssiDbm;
}

void SensorManager::checkRemoteSensorTimeouts(uint32_t now) {
  for (RemoteSensorRuntime &sensor : remoteSensors) {
    if (!sensor.used || sensor.timedOut || !sensor.ok || !sensor.lastUpdateMs) continue;
    if (now < sensor.lastUpdateMs) continue;
    const uint32_t ageMs = now - sensor.lastUpdateMs;
    if (ageMs <= remoteTimeoutForType(sensor.sensorType)) continue;
    sensor.ok = false;
    sensor.timedOut = true;
    state.addLog(String("ESP-NOW capteur distant perdu: node=") + remoteNodeLabel(sensor) + " nodeId=" + sensor.nodeId + " sensorId=" + sensor.sensorId + " name=" + sensor.sensorName + " ageMs=" + String(ageMs) + " timeoutMs=" + String(remoteTimeoutForType(sensor.sensorType)));
  }
}

void SensorManager::remoteSensorsToJson(JsonArray out, bool configuredOnly) {
  const uint32_t now = millis();
  for (const RemoteSensorRuntime &sensor : remoteSensors) {
    if (!sensor.used) continue;
    JsonObject configuredSensor = configuredEspNowSensorFor(sensor);
    if (configuredOnly && configuredSensor.isNull()) continue;
    JsonObject item = out.add<JsonObject>();
    item["origin"] = "ESP-NOW";
    if (!configuredSensor.isNull()) {
      item["id"] = configuredSensor["id"] | "";
      item["name"] = configuredSensor["name"] | configuredSensor["id"] | sensor.sensorName;
      item["type"] = configuredSensor["type"] | espNowSensorTypeText(sensor.sensorType);
      item["role"] = sensor.sensorRole[0] ? sensor.sensorRole : (configuredSensor["role"] | "");
      item["configured"] = true;
      if (configuredSensor["channels"].is<JsonArray>()) item["channels"].set(configuredSensor["channels"]);
    } else {
      item["configured"] = false;
      String fallbackId = String("espnow_") + remoteMacSuffix(sensor.sourceMac) + "_" + String(sensor.sensorId);
      item["id"] = fallbackId;
      if (sensor.sensorName[0]) item["name"] = sensor.sensorName;
      else item["name"] = fallbackId;
      item["type"] = espNowSensorTypeText(sensor.sensorType);
      item["role"] = sensor.sensorRole;
    }
    item["mac"] = sensor.sourceMac;
    item["nodeId"] = sensor.nodeId;
    item["nodeName"] = sensor.nodeName;
    item["firmwareVersion"] = sensor.firmwareVersion;
    item["littlefsVersion"] = sensor.littlefsVersion;
    item["nodeLabel"] = remoteNodeLabel(sensor);
    item["macSuffix"] = remoteMacSuffix(sensor.sourceMac);
    item["sensorId"] = sensor.sensorId;
    item["sensorName"] = sensor.sensorName;
    item["sensorRole"] = sensor.sensorRole;
    item["sensorType"] = sensor.sensorType;
    item["sensorTypeText"] = espNowSensorTypeText(sensor.sensorType);
    const bool neverSeen = sensor.lastUpdateMs == 0;
    const bool timedOut = sensor.timedOut || neverSeen;
    item["ok"] = sensor.ok && !timedOut;
    item["timedOut"] = timedOut;
    item["neverSeen"] = neverSeen;
    item["lastSequence"] = sensor.lastSequence;
    item["lostPackets"] = sensor.lostPackets;
    item["receivedPackets"] = sensor.receivedPackets;
    item["packetLossPercent"] = sensor.receivedPackets ? (100.0f * sensor.lostPackets) / (sensor.receivedPackets + sensor.lostPackets) : 0.0f;
    item["lastError"] = sensor.lastError;
    item["rssiDbm"] = sensor.rssiDbm;
    if (neverSeen) item["ageMs"] = nullptr;
    else item["ageMs"] = now >= sensor.lastUpdateMs ? now - sensor.lastUpdateMs : 0;
    if (sensor.lastUpdateMs) {
      item["lastUpdateMs"] = sensor.lastUpdateMs;
      item["lastSeenMs"] = sensor.lastUpdateMs;
    } else {
      item["lastUpdateMs"] = nullptr;
      item["lastSeenMs"] = nullptr;
    }
    item["lastDiscoveryAgeMs"] = sensor.lastDiscoveryMs ? (now >= sensor.lastDiscoveryMs ? now - sensor.lastDiscoveryMs : 0) : 4294967295UL;
    item["lastDiagnosticAgeMs"] = sensor.lastDiagnosticMs ? (now >= sensor.lastDiagnosticMs ? now - sensor.lastDiagnosticMs : 0) : 4294967295UL;
    JsonObject values = item["values"].to<JsonObject>();
    for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
      const char *key = sensor.values[i].key[0] ? sensor.values[i].key : "VALUE";
      values[key] = sensor.values[i].value;
    }
  }

  if (!configuredOnly) return;
  for (JsonObject cfg : config.sensors()) {
    if (!(cfg["enabled"] | true)) continue;
    String source = cfg["source"] | "local";
    if (!source.equalsIgnoreCase("espnow")) continue;

    String cfgId = cfg["id"] | "";
    bool alreadyAdded = false;
    for (const RemoteSensorRuntime &sensor : remoteSensors) {
      if (!sensor.used) continue;
      JsonObject configuredSensor = configuredEspNowSensorFor(sensor);
      if (configuredSensor.isNull()) continue;
      String runtimeId = configuredSensor["id"] | "";
      if (runtimeId == cfgId) {
        alreadyAdded = true;
        break;
      }
    }
    if (alreadyAdded) continue;

    JsonObject item = out.add<JsonObject>();
    item["id"] = cfgId;
    item["name"] = cfg["name"] | cfgId.c_str();
    item["type"] = cfg["type"] | "";
    item["role"] = cfg["role"] | "";
    item["origin"] = "ESP-NOW";
    item["enabled"] = true;
    item["configured"] = true;
    item["ok"] = false;
    item["timedOut"] = true;
    item["neverSeen"] = true;
    item["ageMs"] = nullptr;
    item["lastUpdateMs"] = nullptr;
    item["lastSeenMs"] = nullptr;
    if (cfg["channels"].is<JsonArray>()) item["channels"].set(cfg["channels"]);
    item["values"].to<JsonObject>();
    if (cfg["mac"].is<const char *>()) item["mac"] = cfg["mac"];
    if (cfg["remoteNode"].is<const char *>()) item["nodeName"] = cfg["remoteNode"];
    if (cfg["remoteSensorId"].is<uint8_t>()) item["sensorId"] = cfg["remoteSensorId"];
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

void SensorManager::mergeRemoteValue(RemoteSensorRuntime &sensor, const EspNowSensorValue &value) {
  String key = value.key;
  key.trim();
  key.toUpperCase();
  for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
    String existingKey = sensor.values[i].key;
    existingKey.trim();
    existingKey.toUpperCase();
    if ((key.length() && existingKey == key) || (!key.length() && sensor.values[i].valueType == value.valueType)) {
      sensor.values[i] = value;
      return;
    }
  }
  if (sensor.valueCount >= MAX_REMOTE_SENSOR_VALUES) return;
  sensor.values[sensor.valueCount++] = value;
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
    if (sensor.used && sensor.sourceMac.equalsIgnoreCase(sourceMac) && sensor.sensorId == sensorId) return &sensor;
    if (!sensor.used && !freeSlot) freeSlot = &sensor;
  }
  if (!freeSlot) return nullptr;
  freeSlot->used = true;
  return freeSlot;
}

String SensorManager::remoteMacSuffix(const String &mac) const {
  String clean;
  for (uint16_t i = 0; i < mac.length(); i++) {
    const char c = mac.charAt(i);
    if (isxdigit(static_cast<unsigned char>(c))) clean += static_cast<char>(toupper(c));
  }
  if (clean.length() <= 6) return clean;
  return clean.substring(clean.length() - 6);
}

String SensorManager::remoteNodeLabel(const RemoteSensorRuntime &sensor) const {
  String label = sensor.nodeName;
  if (!label.length()) label = "ESP";
  const String suffix = remoteMacSuffix(sensor.sourceMac);
  if (suffix.length()) label += " " + suffix;
  return label;
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

bool SensorManager::syncConfiguredRemoteSensorRole(const RemoteSensorRuntime &sensor) {
  String sourceRole = sensor.sensorRole;
  sourceRole.trim();
  if (!sourceRole.length()) return false;

  JsonObject configSensor = configuredEspNowSensorFor(sensor);
  if (configSensor.isNull()) return false;

  String currentRole = configSensor["role"] | "";
  currentRole.trim();
  if (currentRole == sourceRole) return false;

  configSensor["role"] = sourceRole;
  if (!config.saveSensorsConfig()) {
    state.addLog(String("ESP-NOW synchro role echec: mac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId) + " role=" + sourceRole);
    return false;
  }

  state.addLog(String("ESP-NOW role synchronise: mac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId) + " " + currentRole + " -> " + sourceRole);
  return true;
}

void SensorManager::applyRemoteSensorToState(const RemoteSensorRuntime &sensor) {
  for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
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

  String role = sensor.sensorRole;
  if (!role.length() || role.equalsIgnoreCase("autre") || role.equalsIgnoreCase("custom")) role = configSensor["role"] | "";
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
    JsonObject ch1;
    JsonObject ch2;
    JsonArray channels = configSensor["channels"].as<JsonArray>();
    if (!channels.isNull() && channels.size() >= 2) {
      ch1 = channels[0].as<JsonObject>();
      ch2 = channels[1].as<JsonObject>();
    }
    String ch1Role = ch1["role"] | "production";
    String ch2Role = ch2["role"] | "grid";
    ch1Role.toLowerCase();
    ch2Role.toLowerCase();

    if (key == "VOLT" || valueType == VALUE_VOLTAGE_V) state.gridVoltageV = value.value;
    else if (key == "FREQ" || valueType == VALUE_FREQUENCY_HZ) state.gridFrequencyHz = value.value;
    else if (key == "CH1_CURR") {
      state.currentA1 = value.value;
      if (ch1Role == "grid") state.gridCurrentA = value.value;
    } else if (key == "CH1_POWER") {
      state.activePowerW1 = value.value;
      if (ch1Role == "grid") {
        state.jsyGridPowerW = value.value;
        state.gridPowerRawW = value.value;
      } else if (ch1Role == "production") {
        state.productionW = max(0.0f, value.value);
      }
    } else if (key == "CH1_PF") {
      state.powerFactor1 = value.value;
      if (ch1Role == "grid") state.gridPowerFactor = value.value;
    } else if (key == "CH1_EPOS" || key == "CH1_ENERGY_POS") state.jsyImportEnergyWh1 = value.value;
    else if (key == "CH1_ENEG" || key == "CH1_ENERGY_NEG") state.jsyExportEnergyWh1 = value.value;
    else if (key == "CH1_DIR") state.energyDirection1 = value.value < 0 ? "injection" : "consumption";
    else if (key == "CH2_CURR") {
      state.currentA2 = value.value;
      if (ch2Role == "grid") state.gridCurrentA = value.value;
    } else if (key == "CH2_POWER") {
      state.activePowerW2 = value.value;
      if (ch2Role == "grid") {
        state.jsyGridPowerW = value.value;
        state.gridPowerRawW = value.value;
      } else if (ch2Role == "production") {
        state.productionW = max(0.0f, value.value);
      }
    } else if (key == "CH2_PF") {
      state.powerFactor2 = value.value;
      if (ch2Role == "grid") state.gridPowerFactor = value.value;
    } else if (key == "CH2_EPOS" || key == "CH2_ENERGY_POS") state.jsyImportEnergyWh2 = value.value;
    else if (key == "CH2_ENEG" || key == "CH2_ENERGY_NEG") state.jsyExportEnergyWh2 = value.value;
    else if (key == "CH2_DIR") state.energyDirection2 = value.value < 0 ? "injection" : "consumption";
    else if (valueType == VALUE_GRID_POWER_W || key == "GRID" || key == "POWER") {
      state.jsyGridPowerW = value.value;
      state.gridPowerRawW = value.value;
    } else if (valueType == VALUE_CURRENT_A || key == "CURR") state.gridCurrentA = value.value;
    else if (valueType == VALUE_POWER_FACTOR || key == "PF") state.gridPowerFactor = value.value;
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
  const bool isJsy = sensorId == "jsy_grid" || sensorId.startsWith("espnow_");
  if (isJsy && variable == "activePower") return state.gridPowerW;
  if (isJsy && (variable == "voltageV" || variable == "VOLT")) return state.gridVoltageV;
  if (isJsy && (variable == "frequencyHz" || variable == "FREQ")) return state.gridFrequencyHz;
  if (isJsy && (variable == "CH1_CURR" || variable == "currentA1")) return state.currentA1;
  if (isJsy && (variable == "CH1_POWER" || variable == "activePowerW1")) return state.activePowerW1;
  if (isJsy && (variable == "CH1_PF" || variable == "powerFactor1")) return state.powerFactor1;
  if (isJsy && (variable == "CH1_EPOS" || variable == "CH1_ENERGY_POS")) return state.jsyImportEnergyWh1;
  if (isJsy && (variable == "CH1_ENEG" || variable == "CH1_ENERGY_NEG")) return state.jsyExportEnergyWh1;
  if (isJsy && variable == "CH1_DIR") return state.energyDirection1 == "injection" ? -1.0f : 1.0f;
  if (isJsy && (variable == "CH2_CURR" || variable == "currentA2")) return state.currentA2;
  if (isJsy && (variable == "CH2_POWER" || variable == "activePowerW2")) return state.activePowerW2;
  if (isJsy && (variable == "CH2_PF" || variable == "powerFactor2")) return state.powerFactor2;
  if (isJsy && (variable == "CH2_EPOS" || variable == "CH2_ENERGY_POS")) return state.jsyImportEnergyWh2;
  if (isJsy && (variable == "CH2_ENEG" || variable == "CH2_ENERGY_NEG")) return state.jsyExportEnergyWh2;
  if (isJsy && variable == "CH2_DIR") return state.energyDirection2 == "injection" ? -1.0f : 1.0f;
  if (isJsy && variable == "voltageV1") return state.voltageV1;
  if (isJsy && variable == "voltageV2") return state.voltageV2;
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

bool SensorManager::reconfigureJsyTo19200() {
  if (!jsyStarted) {
    jsy.begin();
    jsyStarted = true;
  }
  return jsy.reconfigureTo19200();
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
