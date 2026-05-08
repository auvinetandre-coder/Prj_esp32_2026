#include <Arduino.h>
#include <LittleFS.h>
#include <esp_core_dump.h>

#include "src/config/config_manager.h"
#include "src/network/wifi_manager.h"
#include "src/web/web_ui.h"
#include "src/communication/espnow_manager.h"
#include "src/communication/redundancy_manager.h"
#include "src/sensors/sensor_manager.h"
#include "src/actuators/actuator_manager.h"
#include "src/logic/rule_engine.h"
#include "src/logic/solar_router.h"
#include "src/safety/safety_manager.h"
#include "src/runtime/runtime_state.h"
#include "src/logger/logger.h"
#include "src/simulation/simulation_manager.h"
#include "src/status/status_led.h"
#include "src/display/display_manager.h"

ConfigManager configManager;
RuntimeState runtimeState;
SolarWiFiManager solarWiFi(configManager, runtimeState);
EspNowManager espNowManager(configManager, runtimeState);
RedundancyManager redundancyManager(configManager, runtimeState, espNowManager);
SensorManager sensorManager(configManager, runtimeState);
ActuatorManager actuatorManager(configManager, runtimeState, espNowManager);
RuleEngine ruleEngine(configManager, runtimeState, actuatorManager);
SolarRouter solarRouter(configManager, runtimeState, actuatorManager);
SafetyManager safetyManager(configManager, runtimeState, actuatorManager);
SimulationManager simulationManager(configManager, runtimeState);
WebUi webUi(configManager, runtimeState, solarWiFi, espNowManager, redundancyManager, sensorManager, actuatorManager, ruleEngine, safetyManager, simulationManager);
StatusLed statusLed(runtimeState);
DisplayManager displayManager(configManager, runtimeState);

static uint32_t lastSensorTick = 0;
static uint32_t lastLogicTick = 0;
static uint32_t lastWatchdogTick = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== RouteurSolaireESP32 boot ==="));
  Serial.println(F("Serial OK - baud 115200"));
  esp_core_dump_image_erase();
  Serial.println(F("Core dump flash area cleared"));

  actuatorManager.forceAllOff();
  Serial.println(F("Actuators forced OFF"));
  statusLed.begin();
  Serial.println(F("Status LED initialized on GPIO2"));

  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS init failed"));
    runtimeState.littleFsOk = false;
  } else {
    Serial.println(F("LittleFS OK"));
    runtimeState.littleFsOk = true;
  }

  Serial.println(F("Loading configuration..."));
  configManager.begin();
  Serial.println(F("Configuration OK"));
  runtimeState.begin(configManager);
  Logger::begin(runtimeState);
  simulationManager.begin();

  Serial.println(F("Starting actuators..."));
  actuatorManager.begin();
  safetyManager.begin();
  Serial.println(F("Starting sensors..."));
  sensorManager.begin();
  Serial.println(F("Starting WiFi manager..."));
  solarWiFi.begin();
  Serial.println(F("Starting ESP-NOW..."));
  espNowManager.initEspNow();
  Serial.println(F("Starting redundancy manager..."));
  redundancyManager.begin();
  Serial.println(F("Starting Web UI..."));
  webUi.begin();
  ruleEngine.begin();
  displayManager.begin();

  runtimeState.addLog("Boot completed");
  Serial.print(F("Boot completed. Network mode: "));
  Serial.println(runtimeState.networkMode);
  Serial.print(F("IP: "));
  Serial.println(runtimeState.localIp);
  Serial.print(F("Web UI STA: http://"));
  Serial.println(runtimeState.stationIp);
  Serial.print(F("Web UI AP : http://"));
  Serial.println(runtimeState.apIp);
}

void loop() {
  const uint32_t now = millis();

  webUi.loop();
  solarWiFi.loop();
  espNowManager.loop();
  actuatorManager.loop(now);
  statusLed.loop(now);
  displayManager.loop(now);
  simulationManager.loop(now);

  if (now - lastSensorTick >= 500) {
    lastSensorTick = now;
    sensorManager.loop(now);
  }

  redundancyManager.loop(now);

  if (now - lastLogicTick >= 250) {
    lastLogicTick = now;
    safetyManager.evaluate(now);
    if (!runtimeState.safetyTripped) {
      ruleEngine.loop(now);
      solarRouter.loop(now);
    }
  }

  if (now - lastWatchdogTick >= 1000) {
    lastWatchdogTick = now;
    runtimeState.watchdogSeenMs = now;
    safetyManager.softwareWatchdog(now);
  }
}
