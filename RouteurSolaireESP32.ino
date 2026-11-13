#include <Arduino.h>
#include <LittleFS.h>
#include <esp_core_dump.h>

#include "src/config/config_manager.h"
#include "src/network/wifi_manager.h"
#include "src/web/web_ui.h"
#include "src/communication/espnow_manager.h"
#include "src/communication/mqtt_manager.h"
#include "src/sensors/sensor_manager.h"
#include "src/actuators/actuator_manager.h"
#include "src/logic/rule_engine.h"
#include "src/logic/automate_manager.h"
#include "src/logic/pid_controller.h"
#include "src/safety/safety_manager.h"
#include "src/runtime/runtime_state.h"
#include "src/logger/logger.h"
#include "src/status/status_led.h"
#include "src/display/display_manager.h"
#include "src/boot/boot_manager.h"
#include "src/diagnostics/serial_debug.h"
#include "src/safety/connectivity_watchdog.h"
#include "src/diagnostics/serial_capture.h"

ConfigManager configManager;
RuntimeState runtimeState;
SerialDebug serialDebug(runtimeState);
SolarWiFiManager solarWiFi(configManager, runtimeState);
EspNowManager espNowManager(configManager, runtimeState);
SensorManager sensorManager(configManager, runtimeState);
ActuatorManager actuatorManager(configManager, runtimeState, espNowManager);
MqttManager mqttManager(configManager, runtimeState, actuatorManager, sensorManager);
PIDController pidController(configManager, runtimeState, actuatorManager);
RuleEngine ruleEngine(configManager, runtimeState, actuatorManager, pidController, sensorManager);
AutomateManager automateManager(configManager, runtimeState, actuatorManager, ruleEngine);
SafetyManager safetyManager(configManager, runtimeState, actuatorManager);
StatusLed statusLed(configManager, runtimeState);
WebUi webUi(configManager, runtimeState, solarWiFi, espNowManager, sensorManager, actuatorManager, ruleEngine, safetyManager, statusLed, serialDebug);
DisplayManager displayManager(configManager, runtimeState);
ConnectivityWatchdog connectivityWatchdog(configManager, runtimeState, espNowManager, actuatorManager);

static uint32_t lastSensorTick = 0;
static uint32_t lastLogicTick = 0;
static uint32_t lastSafetyTick = 0;
static uint32_t lastWatchdogTick = 0;

static void recordTaskDuration(const char *taskName, uint32_t startedMs) {
  const uint32_t elapsedMs = millis() - startedMs;
  if (elapsedMs >= runtimeState.slowTaskThresholdMs) {
    runtimeState.lastSlowTask = taskName;
    runtimeState.lastSlowTaskMs = elapsedMs;
  }
}

static void recordLoopModule(LoopPerfModule module, const char *taskName, uint32_t startedMs) {
  const uint32_t elapsedMs = millis() - startedMs;
  runtimeState.recordLoopPerf(module, elapsedMs);
  serialDebug.observeModule(module, elapsedMs);
  if (elapsedMs >= runtimeState.slowTaskThresholdMs) {
    runtimeState.lastSlowTask = taskName;
    runtimeState.lastSlowTaskMs = elapsedMs;
  }
}

void setup() {
  Serial.setTxBufferSize(512);
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== RouteurSolaireESP32 boot ==="));
  Serial.println(F("Serial OK - baud 115200"));
  Serial.print(F("Running partition: "));
  Serial.println(BootManager::getRunningPartitionInfo());
  Serial.print(F("Configured boot partition: "));
  Serial.println(BootManager::getBootPartitionInfo());
  esp_core_dump_image_erase();
  Serial.println(F("Core dump flash area cleared"));

  actuatorManager.forceAllOff();
  Serial.println(F("Actuators forced OFF"));

  if (!LittleFS.begin(false)) {
    Serial.println(F("LittleFS init failed"));
    runtimeState.littleFsOk = false;
  } else {
    Serial.println(F("LittleFS OK"));
    runtimeState.littleFsOk = true;
  }

  Serial.println(F("Loading configuration..."));
  configManager.begin();
  Serial.println(F("Configuration OK"));
  statusLed.begin();
  Serial.println(F("Status LEDs initialized"));
  runtimeState.begin(configManager);
  Logger::begin(runtimeState);

  Serial.println(F("Starting actuators..."));
  actuatorManager.begin();
  safetyManager.begin();
  Serial.println(F("Starting sensors..."));
  sensorManager.begin();
  espNowManager.setSensorManager(&sensorManager);
  Serial.println(F("Starting WiFi manager..."));
  solarWiFi.begin();
  Serial.println(F("Starting ESP-NOW..."));
  espNowManager.initEspNow();
  actuatorManager.forceAllOff();
  Serial.println(F("Starting Web UI..."));
  webUi.begin();
  Serial.println(F("Starting MQTT manager..."));
  mqttManager.begin();
  ruleEngine.begin();
  pidController.begin();
  automateManager.begin(millis());
  displayManager.begin();

  runtimeState.addLog("Boot completed");
  if (BootManager::setNextBootMain()) {
    runtimeState.addLog("Next boot kept on ota_0");
    Serial.println(F("Next boot kept on ota_0"));
  } else {
    runtimeState.addLog("WARNING: unable to keep next boot on ota_0");
    Serial.println(F("WARNING: unable to keep next boot on ota_0"));
  }
  Serial.print(F("Boot completed. Network mode: "));
  Serial.println(runtimeState.networkMode);
  Serial.print(F("IP: "));
  Serial.println(runtimeState.localIp);
  Serial.print(F("Web UI STA: http://"));
  Serial.println(runtimeState.stationIp);
  Serial.print(F("Web UI AP : http://"));
  Serial.println(runtimeState.apIp);
  serialDebug.setRadio(&espNowManager);
  serialDebug.setWifi(&solarWiFi);
  serialDebug.begin();
  connectivityWatchdog.begin();
}

void loop() {
  const uint32_t loopStartMs = millis();
  const uint32_t now = millis();
  serialDebug.loop();

  uint32_t taskStartMs = millis();
  webUi.loop();
  recordLoopModule(PERF_WEBUI, "webUi.loop", taskStartMs);
  taskStartMs = millis();
  solarWiFi.loop();
  recordLoopModule(PERF_WIFI, "wifi.loop", taskStartMs);
  taskStartMs = millis();
  espNowManager.loop();
  runtimeState.espNowProcessMs = millis() - taskStartMs;
  recordLoopModule(PERF_ESPNOW, "espNow.loop", taskStartMs);
  taskStartMs = millis();
  mqttManager.loop(now);
  recordLoopModule(PERF_MQTT, "mqtt.loop", taskStartMs);
  taskStartMs = millis();
  actuatorManager.loop(now);
  recordLoopModule(PERF_ACTUATOR, "actuator.loop", taskStartMs);
  taskStartMs = millis();
  statusLed.loop(now);
  recordLoopModule(PERF_STATUS_LED, "statusLed.loop", taskStartMs);
  taskStartMs = millis();
  displayManager.loop(now);
  recordLoopModule(PERF_DISPLAY, "display.loop", taskStartMs);
  if (now - lastSensorTick >= 100) {
    lastSensorTick = now;
    taskStartMs = millis();
    sensorManager.loop(now);
    recordLoopModule(PERF_SENSOR, "sensor.loop", taskStartMs);
  }

  if (now - lastSafetyTick >= 100) {
    lastSafetyTick = now;
    taskStartMs = millis();
    safetyManager.evaluate(now);
    recordLoopModule(PERF_SAFETY, "safety.evaluate", taskStartMs);
  }

  if (now - lastLogicTick >= 250) {
    lastLogicTick = now;
    if (!runtimeState.safetyTripped) {
      taskStartMs = millis();
      ruleEngine.loop(now);
      recordLoopModule(PERF_RULE_ENGINE, "ruleEngine.loop", taskStartMs);
    }
    automateManager.loop(now);
  }

  if (now - lastWatchdogTick >= 1000) {
    lastWatchdogTick = now;
    runtimeState.watchdogSeenMs = now;
    safetyManager.softwareWatchdog(now);
  }

  const uint32_t loopElapsedMs = millis() - loopStartMs;
  runtimeState.loopCount++;
  runtimeState.loopDurationMs = runtimeState.loopDurationMs == 0
                                  ? loopElapsedMs
                                  : ((runtimeState.loopDurationMs * 7UL) + loopElapsedMs) / 8UL;
  if (loopElapsedMs > runtimeState.maxLoopDurationMs) runtimeState.maxLoopDurationMs = loopElapsedMs;
  if (loopElapsedMs >= runtimeState.slowTaskThresholdMs) {
    runtimeState.lastSlowTask = "loop";
    runtimeState.lastSlowTaskMs = loopElapsedMs;
  }
  connectivityWatchdog.loop();
}
