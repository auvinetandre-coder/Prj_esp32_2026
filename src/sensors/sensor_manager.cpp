#include "../config/legacy_config_migration.h"
#include "sensor_manager.h"
#include <stdlib.h>
#include <errno.h>

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
      return v >= 0.0f && v <= 300.0f;
    case VALUE_CURRENT_A:
      return fabsf(v) <= 500.0f;
    case VALUE_POWER_FACTOR:
      return fabsf(v) <= 1.2f;
    case VALUE_FREQUENCY_HZ:
      return v >= 45.0f && v <= 55.0f;
    case VALUE_TEMPERATURE_C:
      return v >= -30.0f && v <= 125.0f;
    case VALUE_HUMIDITY_PERCENT:
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
  refreshMqttProduction();
  startMetersForCurrentSource();
  if (jsyStarted) jsy.loop(now);
  if (ticStarted) tic.loop(now);
  ds18b20.loop(now);
  checkRemoteSensorTimeouts(now);
  applyGridPowerSource();
  applyMqttProduction();
}

void SensorManager::refreshMqttProduction() {
  JsonObject cfg = config.system()["mqtt"]["productionSensor"];
  String topic = cfg["topic"] | "maison/solax/productionW";
  topic.trim();
  const bool enabled = cfg["enabled"] | false;
  if (enabled != state.mqttProductionEnabled || topic != mqttProductionTopic) {
    if (state.mqttProductionEnabled) state.productionW = NAN;
    mqttProductionReceived = false;
    mqttProductionW = NAN;
    state.lastMqttProductionMs = 0;
    state.mqttProductionStatus = enabled ? "WAITING" : "DISABLED";
  }
  state.mqttProductionEnabled = enabled;
  mqttProductionTopic = topic;
  state.mqttProductionTimeoutMs = constrain(cfg["timeoutMs"] | 900000UL, 1000UL, 86400000UL);
  String base = config.system()["mqtt"]["baseTopic"] | "routeurSolaire";
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  JsonObject topics = config.system()["mqtt"]["topics"];
  if (enabled && (!topic.length() || topic.indexOf('+') >= 0 || topic.indexOf('#') >= 0 ||
      topic.startsWith(base + "/") || topic == String(topics["state"] | "") ||
      topic == String(topics["command"] | "") || topic == String(topics["availability"] | ""))) {
    mqttProductionReceived = false;
    state.mqttProductionStatus = "INVALID_TOPIC";
  }
  applyMqttProduction();
}

void SensorManager::applyMqttProduction() {
  if (!state.mqttProductionEnabled) {
    state.mqttProductionAvailable = false;
    state.mqttProductionStatus = "DISABLED";
    return;
  }
  if (!(config.system()["mqtt"]["enabled"] | false)) mqttProductionReceived = false;
  const bool expired = mqttProductionReceived && millis() - state.lastMqttProductionMs >= state.mqttProductionTimeoutMs;
  const bool available = mqttProductionReceived && !expired &&
                        (config.system()["mqtt"]["enabled"] | false);
  if (expired && state.mqttProductionStatus != "TIMEOUT") {
    state.logEvent("WARNING", "MQTT_PRODUCTION_TIMEOUT", "Production MQTT sans nouvelle mesure", "SensorManager");
  }
  if (expired) state.mqttProductionStatus = "TIMEOUT";
  else if (!(config.system()["mqtt"]["enabled"] | false)) state.mqttProductionStatus = "MQTT_DISABLED";
  else if (available) state.mqttProductionStatus = "OK";
  else if (state.mqttProductionStatus == "MQTT_DISABLED" || state.mqttProductionStatus == "DISABLED") state.mqttProductionStatus = "WAITING";
  state.mqttProductionAvailable = available;
  state.productionW = available ? mqttProductionW : NAN;
}

bool SensorManager::receiveMqttProduction(const String &payload) {
  if (!state.mqttProductionEnabled) return false;
  char *end = nullptr;
  errno = 0;
  const float value = strtof(payload.c_str(), &end);
  const bool valid = payload.length() > 0 && payload.length() <= 64 &&
                     end != payload.c_str() && end == payload.c_str() + payload.length() && errno != ERANGE && isfinite(value) && value >= 0 && value <= 100000;
  if (!valid) {
    if (state.mqttProductionStatus != "INVALID") {
      state.logEvent("WARNING", "MQTT_PRODUCTION_INVALID", "Production MQTT invalide: nombre en watts attendu", "SensorManager");
    }
    mqttProductionReceived = false;
    state.mqttProductionStatus = "INVALID";
    applyMqttProduction();
    return false;
  }
  if (!state.mqttProductionAvailable) {
    state.logEvent("INFO", "MQTT_PRODUCTION_RECEIVED", "Production MQTT disponible", "SensorManager");
  }
  mqttProductionW = value;
  mqttProductionReceived = true;
  state.lastMqttProductionMs = millis();
  applyMqttProduction();
  return true;
}

void SensorManager::mqttProductionToJson(JsonObject out) {
  refreshMqttProduction();
  out["enabled"] = state.mqttProductionEnabled;
  out["available"] = state.mqttProductionAvailable;
  out["status"] = state.mqttProductionStatus;
  out["topic"] = mqttProductionTopic;
  out["unit"] = "W";
  if (state.mqttProductionAvailable) out["powerW"] = mqttProductionW;
  else out["powerW"] = nullptr;
  out["lastSeenAgeMs"] = mqttProductionReceived ? millis() - state.lastMqttProductionMs : 4294967295UL;
  out["timeoutMs"] = state.mqttProductionTimeoutMs;
}

void SensorManager::mqttProductionSensorToJson(JsonArray out) {
  refreshMqttProduction();
  if (!state.mqttProductionEnabled) return;
  JsonObject item = out.add<JsonObject>();
  mqttProductionToJson(item);
  item["id"] = "mqtt_production";
  item["name"] = "Production solaire MQTT";
  item["type"] = "Power";
  item["role"] = "production";
  item["origin"] = "MQTT";
  item["ok"] = state.mqttProductionAvailable;
  item["lost"] = !state.mqttProductionAvailable;
  item["state"] = state.mqttProductionStatus;
  item["neverSeen"] = !mqttProductionReceived;
  item["timedOut"] = state.mqttProductionStatus == "TIMEOUT";
  if (mqttProductionReceived) {
    item["ageMs"] = millis() - state.lastMqttProductionMs;
    item["lastUpdateMs"] = state.lastMqttProductionMs;
    item["lastSeenMs"] = state.lastMqttProductionMs;
  } else {
    item["ageMs"] = nullptr;
    item["lastUpdateMs"] = nullptr;
    item["lastSeenMs"] = nullptr;
  }
  item["values"]["POWER"] = item["powerW"];
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
  // Resolve the network usage before selecting JSY/TIC; no implicit channel fallback.
  state.jsyGridPowerW = jsyRoleValue("grid", "POWER");
  String source = configuredGridPowerSource();
  float selected = NAN;

  if (source == "TIC") {
    if (state.ticAvailable && !isnan(state.ticGridPowerW)) selected = state.ticGridPowerW;
  } else if (source == "AUTO") {
    if (state.ticAvailable && !isnan(state.ticGridPowerW)) selected = state.ticGridPowerW;
    else if (isfinite(state.jsyGridPowerW)) selected = state.jsyGridPowerW;
  } else {
    source = "JSY";
    if (isfinite(state.jsyGridPowerW)) selected = state.jsyGridPowerW;
  }

  if (isnan(selected)) {
    state.gridPowerSource = source;
    state.gridPowerRawW = NAN;
    state.gridPowerFilteredW = NAN;
    state.gridPowerW = NAN;
    state.gridEnergyDirection = "unknown";
    state.injectionW = 0;
    state.consumptionW = 0;
    state.surplusW = 0;
    return;
  }
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
  if (packet.sensorType != SENSOR_LINKY && packet.sensorType != SENSOR_JSY && packet.sensorType != SENSOR_DS18B20) return false;
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
  if (packet.sensorOk) {
    slot->timedOut = false;
    slot->nodeLost = false;
  }
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
    state.addLog(String("SENSOR RESTORED remoteMac=") + sourceMac + " sensorId=" + String(slot->sensorId));
    state.addLog(String("ESP-NOW capteur distant revenu: node=") + remoteNodeLabel(*slot) + " nodeId=" + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
  }
  applyRemoteSensorToState(*slot);
  return true;
}

bool SensorManager::updateRemoteSensorFast(const String &sourceMac, const EspNowFastSensorPacket &packet) {
  if (packet.sensorType != SENSOR_LINKY && packet.sensorType != SENSOR_JSY && packet.sensorType != SENSOR_DS18B20) return false;
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
  if (packet.sensorOk) {
    slot->timedOut = false;
    slot->nodeLost = false;
  }
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
    if (slot->valueCount < MAX_REMOTE_SENSOR_VALUES) { slot->valueUpdatedMs[slot->valueCount] = millis(); slot->values[slot->valueCount++] = value; }
  }

  if (wasTimedOut && packet.sensorOk) {
    state.addLog(String("SENSOR RESTORED remoteMac=") + sourceMac + " sensorId=" + String(slot->sensorId));
    state.addLog(String("ESP-NOW capteur distant revenu: node=") + remoteNodeLabel(*slot) + " nodeId=" + slot->nodeId + " sensorId=" + slot->sensorId + " name=" + slot->sensorName);
  }
  applyRemoteSensorToState(*slot);
  return true;
}

bool SensorManager::updateRemoteSensorDiscovery(const String &sourceMac, const EspNowSensorDiscoveryPacket &packet) {
  if (packet.sensorType != SENSOR_LINKY && packet.sensorType != SENSOR_JSY && packet.sensorType != SENSOR_DS18B20) return false;
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
    mergeRemoteValue(*slot, value, false);
  }
  syncConfiguredRemoteSensorMetadata(*slot);
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
    const uint32_t timeoutMs = remoteTimeoutForSensor(sensor);
    if (ageMs <= timeoutMs) continue;
    sensor.ok = false;
    sensor.timedOut = true;
    applyRemoteSensorLostToState(sensor);
    state.addLog(String("ESPNOW SENSOR STALE remoteMac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId) + " type=" + espNowSensorTypeText(sensor.sensorType) + " ageMs=" + String(ageMs) + " timeoutMs=" + String(timeoutMs));
  }
}

void SensorManager::setRemoteNodeLost(const String &sourceMac, bool lost) {
  for (RemoteSensorRuntime &sensor : remoteSensors) {
    if (!sensor.used || !sensor.sourceMac.equalsIgnoreCase(sourceMac)) continue;
    if (lost) {
      if (!sensor.nodeLost) {
        state.addLog(String("SENSOR LOST remoteMac=") + sourceMac + " sensorId=" + String(sensor.sensorId));
      }
      sensor.nodeLost = true;
      sensor.ok = false;
      sensor.timedOut = true;
      applyRemoteSensorLostToState(sensor);
    } else {
      sensor.nodeLost = false;
    }
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
      const char *localName = configuredSensor["localName"] | "";
      const char *remoteName = configuredSensor["remoteName"] | sensor.sensorName;
      const char *displayName = localName[0] ? localName : (remoteName[0] ? remoteName : (configuredSensor["name"] | configuredSensor["id"] | sensor.sensorName));
      item["localName"] = localName;
      item["remoteName"] = remoteName;
      item["displayName"] = displayName;
      item["name"] = displayName;
      item["type"] = configuredSensor["type"] | espNowSensorTypeText(sensor.sensorType);
      item["remoteType"] = configuredSensor["remoteType"] | espNowSensorTypeText(sensor.sensorType);
      item["remoteSensorType"] = configuredSensor["remoteSensorType"] | sensor.sensorType;
      const char *localRole = configuredSensor["localRole"] | "";
      const char *remoteRole = configuredSensor["remoteRole"] | sensor.sensorRole;
      const char *fallbackRole = configuredSensor["role"] | sensor.sensorRole;
      item["localRole"] = localRole;
      item["remoteRole"] = remoteRole;
      item["displayRole"] = localRole[0] ? localRole : (remoteRole[0] ? remoteRole : fallbackRole);
      item["role"] = item["displayRole"];
      item["configured"] = true;
      if (configuredSensor["channels"].is<JsonArray>()) item["channels"].set(configuredSensor["channels"]);
      if (configuredSensor["valueKeys"].is<JsonArray>()) item["valueKeys"].set(configuredSensor["valueKeys"]);
      if (configuredSensor["units"].is<JsonObject>()) item["units"].set(configuredSensor["units"]);
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
    const bool timedOut = sensor.timedOut || sensor.nodeLost || neverSeen;
    item["ok"] = sensor.ok && !timedOut;
    item["timedOut"] = timedOut;
    item["neverSeen"] = neverSeen;
    item["nodeOk"] = !sensor.nodeLost;
    item["nodeLost"] = sensor.nodeLost;
    item["state"] = sensor.nodeLost ? "LOST" : (timedOut ? "STALE" : (sensor.ok ? "OK" : "ERROR"));
    item["lastSequence"] = sensor.lastSequence;
    item["lostPackets"] = sensor.lostPackets;
    item["receivedPackets"] = sensor.receivedPackets;
    item["packetLossPercent"] = sensor.receivedPackets ? (100.0f * sensor.lostPackets) / (sensor.receivedPackets + sensor.lostPackets) : 0.0f;
    if (sensor.nodeLost) item["lastError"] = "NODE_LOST";
    else item["lastError"] = sensor.lastError;
    item["timeoutMs"] = remoteTimeoutForSensor(sensor);
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
    const char *localName = cfg["localName"] | "";
    const char *remoteName = cfg["remoteName"] | "";
    const char *localRole = cfg["localRole"] | "";
    const char *remoteRole = cfg["remoteRole"] | "";
    item["localName"] = localName;
    item["remoteName"] = remoteName;
    item["displayName"] = localName[0] ? localName : (remoteName[0] ? remoteName : (cfg["name"] | cfgId.c_str()));
    item["localRole"] = localRole;
    item["remoteRole"] = remoteRole;
    item["displayRole"] = localRole[0] ? localRole : (remoteRole[0] ? remoteRole : (cfg["role"] | ""));
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

uint32_t SensorManager::getEspNowSensorTimeoutMs(uint8_t sensorType) {
  switch (sensorType) {
    case SENSOR_JSY: return 5000UL;
    case SENSOR_LINKY: return 15000UL;
    case SENSOR_DS18B20:
    case SENSOR_TEMP_HUM: return 30000UL;
    default: return REMOTE_SENSOR_TIMEOUT_MS;
  }
}

uint32_t SensorManager::remoteTimeoutForSensor(const RemoteSensorRuntime &sensor) {
  uint8_t type = sensor.sensorType;
  if (type == SENSOR_UNKNOWN && sensor.sensorId >= 20 && sensor.sensorId <= 22) type = SENSOR_DS18B20;
  uint32_t timeoutMs = getEspNowSensorTimeoutMs(type);
  JsonObject cfg = configuredEspNowSensorFor(sensor);
  if (!cfg.isNull()) {
    uint32_t announcedIntervalMs = cfg["remoteExportIntervalMs"] | 0UL;
    if (!announcedIntervalMs) announcedIntervalMs = cfg["espNowExportIntervalMs"] | 0UL;
    if (announcedIntervalMs > 0) timeoutMs = max(timeoutMs, announcedIntervalMs * 3UL);
    uint32_t explicitTimeoutMs = cfg["remoteTimeoutMs"] | 0UL;
    if (!explicitTimeoutMs) explicitTimeoutMs = cfg["espNowTimeoutMs"] | 0UL;
    if (explicitTimeoutMs > 0) timeoutMs = max(timeoutMs, explicitTimeoutMs);
  }
  if (type == SENSOR_DS18B20 || type == SENSOR_TEMP_HUM || type == SENSOR_UNKNOWN) timeoutMs = max(timeoutMs, 30000UL);
  return timeoutMs;
}

void SensorManager::updateRemotePacketLoss(RemoteSensorRuntime &sensor, uint32_t sequence) {
  if (sensor.receivedPackets > 0 && sequence > sensor.lastSequence + 1) {
    sensor.lostPackets += sequence - sensor.lastSequence - 1;
  }
  sensor.lastSequence = sequence;
  sensor.receivedPackets++;
}

void SensorManager::mergeRemoteValue(RemoteSensorRuntime &sensor, const EspNowSensorValue &value, bool sample) {
  String key = value.key;
  key.trim();
  key.toUpperCase();
  for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
    String existingKey = sensor.values[i].key;
    existingKey.trim();
    existingKey.toUpperCase();
    if ((key.length() && existingKey == key) || (!key.length() && sensor.values[i].valueType == value.valueType)) {
      if (sample) {
        sensor.values[i] = value;
        sensor.valueUpdatedMs[i] = millis();
      }
      return;
    }
  }
  if (sensor.valueCount >= MAX_REMOTE_SENSOR_VALUES) return;
  sensor.valueUpdatedMs[sensor.valueCount] = sample ? millis() : 0;
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
    return sensor;
  }
  return JsonObject();
}

bool SensorManager::syncConfiguredRemoteSensorMetadata(const RemoteSensorRuntime &sensor) {
  JsonObject configSensor = configuredEspNowSensorFor(sensor);
  if (configSensor.isNull()) return false;

  bool changed = false;
  bool skippedOverride = false;

  String sourceName = sensor.sensorName;
  sourceName.trim();
  String currentName = configSensor["name"] | "";
  currentName.trim();
  String localName = configSensor["localName"] | "";
  localName.trim();
  String remoteName = configSensor["remoteName"] | "";
  remoteName.trim();
  if (!remoteName.length() && currentName.length()) {
    remoteName = currentName;
    configSensor["remoteName"] = remoteName;
    changed = true;
  }
  if (sourceName.length() && remoteName != sourceName) {
    state.addLog(String("ESPNOW SENSOR META UPDATE remoteMac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId) + " name old=" + remoteName + " new=" + sourceName);
    configSensor["remoteName"] = sourceName;
    changed = true;
    if (!localName.length()) configSensor["name"] = sourceName;
    else skippedOverride = true;
  }

  String sourceRole = sensor.sensorRole;
  sourceRole.trim();
  String currentRole = configSensor["role"] | "";
  currentRole.trim();
  String localRole = configSensor["localRole"] | "";
  localRole.trim();
  String remoteRole = configSensor["remoteRole"] | "";
  remoteRole.trim();
  if (!remoteRole.length() && currentRole.length()) {
    remoteRole = currentRole;
    configSensor["remoteRole"] = remoteRole;
    changed = true;
  }
  if (sourceRole.length() && remoteRole != sourceRole) {
    state.addLog(String("ESPNOW SENSOR META UPDATE remoteMac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId) + " role old=" + remoteRole + " new=" + sourceRole);
    configSensor["remoteRole"] = sourceRole;
    changed = true;
    if (!localRole.length()) configSensor["role"] = sourceRole;
    else skippedOverride = true;
  }

  String remoteType = espNowSensorTypeText(sensor.sensorType);
  if (remoteType.length() && String(configSensor["remoteType"] | "") != remoteType) {
    configSensor["remoteType"] = remoteType;
    configSensor["remoteSensorType"] = sensor.sensorType;
    changed = true;
  }

  String valueSignature;
  for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
    const char *key = sensor.values[i].key[0] ? sensor.values[i].key : "VALUE";
    if (valueSignature.length()) valueSignature += ",";
    valueSignature += key;
    valueSignature += ":";
    valueSignature += sensor.values[i].unit;
  }
  if (sensor.valueCount > 0 && String(configSensor["remoteValueSignature"] | "") != valueSignature) {
    configSensor["remoteValueSignature"] = valueSignature;
    JsonArray keys = configSensor["valueKeys"].to<JsonArray>();
    keys.clear();
    JsonObject units = configSensor["units"].to<JsonObject>();
    units.clear();
    for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
      const char *key = sensor.values[i].key[0] ? sensor.values[i].key : "VALUE";
      keys.add(key);
      if (sensor.values[i].unit[0]) units[key] = sensor.values[i].unit;
    }
    changed = true;
  }

  if (skippedOverride) state.addLog("ESPNOW SENSOR META SKIP local override active");
  if (!changed) return false;

  if (!config.saveSensorsConfig()) {
    state.addLog(String("ESP-NOW synchro meta echec: mac=") + sensor.sourceMac + " sensorId=" + String(sensor.sensorId));
    return false;
  }
  return true;
}

void SensorManager::applyRemoteSensorToState(const RemoteSensorRuntime &sensor) {
  for (uint8_t i = 0; i < sensor.valueCount && i < MAX_REMOTE_SENSOR_VALUES; i++) {
    applyRemoteValueToState(sensor, sensor.values[i]);
  }
  applyMqttProduction();
}

void SensorManager::applyRemoteSensorLostToState(const RemoteSensorRuntime &sensor) {
  JsonObject configSensor = configuredEspNowSensorFor(sensor);
  if (configSensor.isNull()) return;

  if (sensor.sensorType == SENSOR_LINKY) {
    state.ticAvailable = false;
    state.ticStatus = "ESP_NOW_NODE_LOST";
    return;
  }

  if (sensor.sensorType == SENSOR_JSY) {
    state.jsyOnline = false;
    state.jsyLastError = "ESP_NOW_NODE_LOST";
    return;
  }

  if (sensor.sensorType == SENSOR_DS18B20 || sensor.sensorType == SENSOR_TEMP_HUM) {
    String role = configSensor["role"] | sensor.sensorRole;
    role.toLowerCase();
    if (role.indexOf("haut") >= 0 || role.indexOf("top") >= 0 || role == "temperature_1") state.ds18b20Available[0] = false;
    else if (role.indexOf("milieu") >= 0 || role.indexOf("middle") >= 0 || role == "temperature_2") state.ds18b20Available[1] = false;
    else if (role.indexOf("bas") >= 0 || role.indexOf("bottom") >= 0 || role == "temperature_3") state.ds18b20Available[2] = false;
    return;
  }


}

void SensorManager::applyRemoteValueToState(const RemoteSensorRuntime &sensor, const EspNowSensorValue &value) {
  if (sensor.nodeLost || sensor.timedOut || !sensor.ok) return;
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

  String role = configSensor["role"] | sensor.sensorRole;
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
    if (role.indexOf("haut") >= 0 || role.indexOf("top") >= 0 || role == "temperature_1") {
      state.temperature1C = value.value;
      state.ds18b20Temps[0] = value.value;
      state.ds18b20Available[0] = sensor.ok;
      state.ds18b20LastReadMs[0] = now;
    } else if (role.indexOf("milieu") >= 0 || role.indexOf("middle") >= 0 || role == "temperature_2") {
      state.temperature2C = value.value;
      state.ds18b20Temps[1] = value.value;
      state.ds18b20Available[1] = sensor.ok;
      state.ds18b20LastReadMs[1] = now;
    } else if (role.indexOf("bas") >= 0 || role.indexOf("bottom") >= 0 || role == "temperature_3") {
      state.temperature3C = value.value;
      state.ds18b20Temps[2] = value.value;
      state.ds18b20Available[2] = sensor.ok;
      state.ds18b20LastReadMs[2] = now;
    }
    return;
  }




}

float SensorManager::jsyRoleValue(const String &role, const String &metric, const String &actuatorId) {
  float result = NAN;
  unsigned matches = 0;
  const uint32_t now = millis();
  for (JsonObject cfg : config.sensors()) {
    if (!(cfg["enabled"] | true)) continue;
    String type = cfg["type"] | "";
    if (type != "JSY-MK-194T") continue;
    String origin = cfg["source"] | "local";
    const bool remote = origin.equalsIgnoreCase("espnow");
    RemoteSensorRuntime *runtime = nullptr;
    if (remote) {
      for (auto &slot : remoteSensors) {
        if (!slot.used || slot.sensorType != SENSOR_JSY) continue;
        if (configuredEspNowSensorFor(slot) == cfg) { runtime = &slot; break; }
      }
    }
    JsonArray channels = cfg["channels"].as<JsonArray>();
    for (uint8_t index = 0; index < 2; ++index) {
      // Historical defaults are preserved only when channels have not been configured.
      String channelId = index == 0 ? "clamp1" : "clamp2";
      JsonObject channel;
      for (JsonObject item : channels) {
        if (String(item["id"] | "") == channelId) { channel = item; break; }
      }
      String assignedRole = channel["role"] | (index == 0 ? "production" : "grid");
      assignedRole.toLowerCase();
      if (assignedRole != role) continue;
      if (actuatorId.length() && String(channel["actuatorId"] | "") != actuatorId) continue;
      ++matches;
      float value = NAN;
      if (remote) {
        if (runtime && runtime->ok && !runtime->timedOut && !runtime->nodeLost &&
            runtime->lastUpdateMs && now - runtime->lastUpdateMs < remoteTimeoutForSensor(*runtime)) {
          const String suffix = role == "load" && metric == "POWER" ? String("W_RAW")
              : (metric == "ENERGY_POS" ? String("EPOS") : metric);
          String key = String(index == 0 ? "CH1_" : "CH2_") + suffix;
          for (uint8_t n = 0; n < runtime->valueCount; ++n) {
            if (String(runtime->values[n].key) == key && runtime->valueUpdatedMs[n] &&
                now - runtime->valueUpdatedMs[n] < remoteTimeoutForSensor(*runtime)) {
              value = runtime->values[n].value; break;
            }
          }
        }
      } else if (jsyStarted) {
        value = jsy.channelValue(index, role == "load" && metric == "POWER" ? String("W_RAW") : metric);
      }
      result = value;
    }
  }
  // Never choose silently between two channels declaring the same usage.
  return matches == 1 && isfinite(result) ? result : NAN;
}

float SensorManager::valueFor(const String &sensorId, const String &variable) {
  if (sensorId == "mqtt_production" && (variable == "powerW" || variable == "POWER" || variable == "activePower")) {
    refreshMqttProduction();
    return state.mqttProductionAvailable ? mqttProductionW : NAN;
  }
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
  if (sensorId == "solar" && variable == "powerW") return state.productionW;
  if (variable == "temperatureC") return ds18b20.getTemperatureById(sensorId);
  if (genericTemperatureId(sensorId) == "temperature_1" && variable == "temperature") return state.temperature1C;
  if (genericTemperatureId(sensorId) == "temperature_2" && variable == "temperature") return state.temperature2C;
  if (genericTemperatureId(sensorId) == "temperature_3" && variable == "temperature") return state.temperature3C;
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

SensorManager::PowerSample SensorManager::gridPowerSample(const String &sensorId, const String &variable, uint32_t now) {
  PowerSample result;
  if (variable != "gridPowerW" && variable != "jsyGridPowerW") return result;
  if (sensorId == "grid") {
    // Resolve AUTO in the sensor layer. The PID sees only a canonical sample.
    const String choice = configuredGridPowerSource();
    PowerSample jsySample, ticSample;
    unsigned jsyCount = 0, ticCount = 0;
    for (JsonObject cfg : config.sensors()) {
      if (!(cfg["enabled"] | true)) continue;
      const String type = cfg["type"] | "";
      if (type == "JSY-MK-194T") {
        PowerSample sample = gridPowerSample(cfg["id"] | "", variable, now);
        if (sample.valid) { jsySample = sample; ++jsyCount; }
      } else if (type == "TIC Linky") {
        PowerSample sample = gridPowerSample(cfg["id"] | "", variable, now);
        if (sample.valid) { ticSample = sample; ++ticCount; }
      }
    }
    if (choice == "TIC" || (choice == "AUTO" && ticCount)) return ticCount == 1 ? ticSample : result;
    return jsyCount == 1 ? jsySample : result;
  }
  JsonObject cfg;
  for (JsonObject item : config.sensors()) {
    if (sensorId == (item["id"] | "") && (item["enabled"] | true)) { cfg = item; break; }
  }
  if (cfg.isNull()) return result;
  const String type = cfg["type"] | "";
  const String origin = cfg["source"] | "local";
  const bool remote = origin.equalsIgnoreCase("espnow");
  const bool isJsy = type == "JSY-MK-194T";
  const bool isTic = type == "TIC Linky";
  const bool declaredGrid = String(cfg["powerSemantics"] | "") == "signed_grid";
  if (!isJsy && !isTic && !declaredGrid) return result;
  String key = "GRID";
  int gridChannel = -1;
  if (isJsy) {
    unsigned count = 0;
    for (uint8_t n = 0; n < 2; ++n) {
      JsonObject channel;
      for (JsonObject item : cfg["channels"].as<JsonArray>())
        if (String(item["id"] | "") == (n == 0 ? "clamp1" : "clamp2")) channel = item;
      if (String(channel["role"] | (n == 0 ? "production" : "grid")) == "grid") { gridChannel = n; ++count; }
    }
    if (count != 1) return result;
    // Regulation needs signed active watts, never the historical U*I*PF fallback.
    key = gridChannel == 0 ? "CH1_W_RAW" : "CH2_W_RAW";
  }
  // JSY timeoutMs is the Modbus transaction deadline, not measurement freshness.
  uint32_t ttl = isJsy ? (cfg["measurementTimeoutMs"] | 3000UL) : (cfg["timeoutMs"] | 5000UL);
  if (remote) {
    for (auto &slot : remoteSensors) {
      if (!slot.used || !slot.ok || slot.timedOut || slot.nodeLost || configuredEspNowSensorFor(slot) != cfg) continue;
      ttl = min(ttl, remoteTimeoutForSensor(slot));
      for (uint8_t n = 0; n < slot.valueCount; ++n) {
        if (String(slot.values[n].key) != key) continue;
        // Enforce manual per-key import selection, just as the normal sensor path.
        String imported = cfg["remoteKey"] | cfg["key"] | "ALL";
        imported.toUpperCase();
        const String channelPowerKey = gridChannel == 0 ? "CH1_POWER" : "CH2_POWER";
        // A historical per-channel import authorizes that same channel's raw watts.
        // It never authorizes the other clamp or creates an import automatically.
        if (imported.length() && imported != "ALL" && imported != "*" && imported != key &&
            !(isJsy && imported == channelPowerKey)) continue;
        result.watts = slot.values[n].value; result.timestampMs = slot.valueUpdatedMs[n];
        break;
      }
      break;
    }
  } else if (isJsy && jsyStarted) {
    result.watts = jsy.channelValue(gridChannel, "W_RAW"); result.timestampMs = jsy.powerSampleMs();
  } else if (isTic && ticStarted && tic.isAvailable()) {
    result.watts = tic.getGridPowerW(); result.timestampMs = tic.powerSampleMs();
  }
  if (declaredGrid && String(cfg["powerSign"] | "IMPORT_POSITIVE") == "EXPORT_POSITIVE") result.watts = -result.watts;
  result.source = sensorId + ":" + key;
  // A radio callback may run after the main loop captured its `now` argument.
  // Check freshness against the current local clock, not that older loop tick.
  result.valid = isfinite(result.watts) && result.timestampMs && uint32_t(millis() - result.timestampMs) < ttl;
  return result;
}
