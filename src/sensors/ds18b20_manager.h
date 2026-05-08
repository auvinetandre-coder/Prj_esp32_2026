#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "../config/config_manager.h"
#include "../runtime/runtime_state.h"

struct DS18B20Sensor {
  String id;
  String name;
  String role;
  String address;
  bool enabled = true;
  bool critical = false;
  float temperatureC = NAN;
  bool available = false;
  uint32_t lastReadMs = 0;
  uint16_t errorCount = 0;
};

class DS18B20Manager {
public:
  DS18B20Manager(ConfigManager &config, RuntimeState &state) : config(config), state(state), oneWire(4), dallas(&oneWire) {}

  void begin();
  void loop();
  void loop(uint32_t now);
  void scanBus();
  void requestTemperatures();
  void updateReadings();
  void updateReadings(uint32_t now);
  float getTemperatureById(const String &sensorId);
  float getTemperatureByRole(const String &role);
  bool isSensorAvailable(const String &sensorId);
  bool isCriticalSensorMissing();
  String getCriticalMissingSensors();
  String getSensorStatusJson();
  void printSensorsStatus();
  String detectedAddressesJson();
  bool assignAddress(const String &sensorId, const String &address);
  void reloadConfig();

private:
  static const uint8_t MAX_DS18B20 = 3;

  ConfigManager &config;
  RuntimeState &state;
  OneWire oneWire;
  DallasTemperature dallas;
  DS18B20Sensor sensors[MAX_DS18B20];
  uint8_t sensorCount = 0;
  String detectedAddresses[8];
  uint8_t detectedCount = 0;
  uint32_t lastRequestMs = 0;
  bool conversionPending = false;
  uint8_t oneWireGpio = 13;
  uint32_t readIntervalMs = 2000;
  bool scanOnBoot = true;
  bool busEnabled = true;

  void configureBusFromConfig();
  void loadConfig();
  void publishRuntimeState();
  void publishConfigRuntimeFields();
  String addressToString(const DeviceAddress address);
  bool stringToAddress(const String &text, DeviceAddress address);
  bool validTemperature(float value);
};
