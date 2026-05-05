#include "ds18b20_manager.h"

void DS18B20Manager::begin() {
  JsonObject bus = config.sensorsDoc()["oneWireBus"];
  oneWireGpio = bus["gpio"] | 4;
  scanOnBoot = bus["scanOnBoot"] | true;
  readIntervalMs = bus["readIntervalMs"] | 2000;
  readIntervalMs = constrain(readIntervalMs, 1000UL, 60000UL);
  loadConfig();
  dallas.begin();
  dallas.setWaitForConversion(false);
  if (scanOnBoot) scanBus();
  printSensorsStatus();
}

void DS18B20Manager::loop() {
  loop(millis());
}

void DS18B20Manager::loop(uint32_t now) {
  if (!conversionPending && now - lastRequestMs >= readIntervalMs) {
    requestTemperatures();
    lastRequestMs = now;
    conversionPending = true;
    return;
  }
  if (conversionPending && now - lastRequestMs >= 800) {
    conversionPending = false;
    updateReadings(now);
  }
}

void DS18B20Manager::scanBus() {
  detectedCount = 0;
  DeviceAddress address;
  uint8_t count = min<uint8_t>(dallas.getDeviceCount(), 8);
  Serial.print(F("DS18B20 detectees: "));
  Serial.println(count);
  for (uint8_t i = 0; i < count; i++) {
    if (dallas.getAddress(address, i)) {
      detectedAddresses[detectedCount] = addressToString(address);
      Serial.print(F(" - "));
      Serial.println(detectedAddresses[detectedCount]);
      detectedCount++;
    }
  }
}

void DS18B20Manager::requestTemperatures() {
  dallas.requestTemperatures();
}

void DS18B20Manager::updateReadings() {
  updateReadings(millis());
}

void DS18B20Manager::updateReadings(uint32_t now) {
  for (uint8_t i = 0; i < sensorCount; i++) {
    DS18B20Sensor &s = sensors[i];
    if (!s.enabled) {
      s.available = false;
      continue;
    }
    DeviceAddress address;
    float value = NAN;
    if (s.address.length() && stringToAddress(s.address, address)) {
      value = dallas.getTempC(address);
    } else if (i < dallas.getDeviceCount()) {
      value = dallas.getTempCByIndex(i);
    }

    if (validTemperature(value)) {
      s.temperatureC = value;
      s.available = true;
      s.lastReadMs = now;
    } else {
      s.available = false;
      s.errorCount++;
    }
  }
  publishRuntimeState();
  publishConfigRuntimeFields();
}

float DS18B20Manager::getTemperatureById(const String &sensorId) {
  for (uint8_t i = 0; i < sensorCount; i++) if (sensors[i].id == sensorId) return sensors[i].temperatureC;
  return NAN;
}

float DS18B20Manager::getTemperatureByRole(const String &role) {
  for (uint8_t i = 0; i < sensorCount; i++) if (sensors[i].role == role) return sensors[i].temperatureC;
  return NAN;
}

bool DS18B20Manager::isSensorAvailable(const String &sensorId) {
  for (uint8_t i = 0; i < sensorCount; i++) if (sensors[i].id == sensorId) return sensors[i].available;
  return false;
}

bool DS18B20Manager::isCriticalSensorMissing() {
  for (uint8_t i = 0; i < sensorCount; i++) {
    if (sensors[i].enabled && sensors[i].critical && !sensors[i].available) return true;
  }
  return false;
}

String DS18B20Manager::getCriticalMissingSensors() {
  String out;
  for (uint8_t i = 0; i < sensorCount; i++) {
    if (sensors[i].enabled && sensors[i].critical && !sensors[i].available) {
      if (out.length()) out += ",";
      out += sensors[i].id;
    }
  }
  return out;
}

String DS18B20Manager::getSensorStatusJson() {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject item = arr.add<JsonObject>();
    item["id"] = sensors[i].id;
    item["name"] = sensors[i].name;
    item["role"] = sensors[i].role;
    item["address"] = sensors[i].address;
    item["enabled"] = sensors[i].enabled;
    item["critical"] = sensors[i].critical;
    item["temperatureC"] = sensors[i].temperatureC;
    item["available"] = sensors[i].available;
    item["lastReadMs"] = sensors[i].lastReadMs;
    item["errorCount"] = sensors[i].errorCount;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void DS18B20Manager::printSensorsStatus() {
  Serial.println(F("=== DS18B20 status ==="));
  for (uint8_t i = 0; i < sensorCount; i++) {
    Serial.print(sensors[i].id);
    Serial.print(F(" role="));
    Serial.print(sensors[i].role);
    Serial.print(F(" critical="));
    Serial.print(sensors[i].critical ? F("true") : F("false"));
    Serial.print(F(" address="));
    Serial.println(sensors[i].address.length() ? sensors[i].address : F("(auto)"));
  }
}

String DS18B20Manager::detectedAddressesJson() {
  DynamicJsonDocument doc(512);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < detectedCount; i++) arr.add(detectedAddresses[i]);
  String out;
  serializeJson(doc, out);
  return out;
}

bool DS18B20Manager::assignAddress(const String &sensorId, const String &address) {
  JsonArray arr = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (JsonObject sensor : arr) {
    if (sensor["id"].as<String>() == sensorId) {
      sensor["address"] = address;
      loadConfig();
      return config.saveSensorsConfig();
    }
  }
  return false;
}

void DS18B20Manager::loadConfig() {
  sensorCount = 0;
  JsonArray arr = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (JsonObject sensor : arr) {
    if (sensorCount >= MAX_DS18B20) break;
    DS18B20Sensor &s = sensors[sensorCount++];
    s.id = sensor["id"] | "";
    s.name = sensor["name"] | "";
    s.role = sensor["role"] | "autre";
    s.address = sensor["address"] | "";
    s.enabled = sensor["enabled"] | true;
    s.critical = sensor["critical"] | false;
    s.temperatureC = NAN;
    s.available = false;
    s.lastReadMs = 0;
    s.errorCount = 0;
  }
}

void DS18B20Manager::publishRuntimeState() {
  for (uint8_t i = 0; i < 3; i++) {
    state.ds18b20Temps[i] = i < sensorCount ? sensors[i].temperatureC : NAN;
    state.ds18b20Available[i] = i < sensorCount ? sensors[i].available : false;
    state.ds18b20LastReadMs[i] = i < sensorCount ? sensors[i].lastReadMs : 0;
    state.ds18b20ErrorCount[i] = i < sensorCount ? sensors[i].errorCount : 0;
  }
  state.tankTopC = getTemperatureByRole("ballon_haut");
  state.tankMiddleC = getTemperatureByRole("ballon_milieu");
  state.tankBottomC = getTemperatureByRole("ballon_bas");
  state.ds18b20CriticalMissing = isCriticalSensorMissing();
  state.ds18b20CriticalMissingList = getCriticalMissingSensors();
}

void DS18B20Manager::publishConfigRuntimeFields() {
  JsonArray arr = config.sensorsDoc()["ds18b20"].as<JsonArray>();
  for (JsonObject item : arr) {
    String id = item["id"] | "";
    for (uint8_t i = 0; i < sensorCount; i++) {
      if (sensors[i].id == id) {
        if (sensors[i].available) item["temperatureC"] = sensors[i].temperatureC;
        else item["temperatureC"] = nullptr;
        item["available"] = sensors[i].available;
        item["lastReadMs"] = sensors[i].lastReadMs;
        item["errorCount"] = sensors[i].errorCount;
      }
    }
  }
}

String DS18B20Manager::addressToString(const DeviceAddress address) {
  char out[24];
  snprintf(out, sizeof(out), "%02X%02X%02X%02X%02X%02X%02X%02X",
           address[0], address[1], address[2], address[3], address[4], address[5], address[6], address[7]);
  return String(out);
}

bool DS18B20Manager::stringToAddress(const String &text, DeviceAddress address) {
  if (text.length() != 16) return false;
  for (uint8_t i = 0; i < 8; i++) {
    char part[3] = {text[i * 2], text[i * 2 + 1], 0};
    address[i] = static_cast<uint8_t>(strtoul(part, nullptr, 16));
  }
  return true;
}

bool DS18B20Manager::validTemperature(float value) {
  if (isnan(value)) return false;
  if (value <= -126.0f) return false;
  if (abs(value - 85.0f) < 0.01f) return false;
  return value > -55.0f && value < 125.0f;
}
