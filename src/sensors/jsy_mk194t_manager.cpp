#include "jsy_mk194t_manager.h"

void JSYMK194TManager::begin() {
  JsonObject router = config.system()["router"];
  minInjectionStartW = router["minInjectionStartW"] | 200.0f;
  stopBelowInjectionW = router["stopBelowInjectionW"] | 80.0f;
  Serial2.begin(baudrate, SERIAL_8N1, rxPin, txPin);
  Serial.println(F("JSY-MK-194T Serial2 pret: RX16 TX17 4800 8N1 adresse 1"));
}

void JSYMK194TManager::loop() {
  loop(millis());
}

void JSYMK194TManager::loop(uint32_t now) {
  readFrame(now);
  if (reading.available && now - reading.lastValidReadMs > 3000) {
    reading.available = false;
    publishRuntime();
  }
}

void JSYMK194TManager::readFrame(uint32_t now) {
  if (!waiting && now - lastRequestMs >= readIntervalMs) {
    sendRequest();
    lastRequestMs = now;
    waiting = true;
    rxLen = 0;
    expectedLen = 0;
  }

  while (Serial2.available() && rxLen < sizeof(rx)) {
    rx[rxLen++] = Serial2.read();
    if (rxLen == 3) expectedLen = 3 + rx[2] + 2;
    if (expectedLen && rxLen >= expectedLen) {
      parseFrame(now);
      waiting = false;
      break;
    }
  }

  if (waiting && now - lastRequestMs > timeoutMs) {
    waiting = false;
    reading.available = false;
    reading.errorCount++;
    publishRuntime();
    logError(F("JSY timeout Modbus"), now);
  }
}

bool JSYMK194TManager::parseFrame(uint32_t now) {
  if (rxLen < 61 || rx[0] != modbusAddress || rx[1] != 0x03 || rx[2] != 56) {
    reading.errorCount++;
    logError(F("JSY trame invalide"), now);
    return false;
  }

  uint16_t expected = crc16(rx, rxLen - 2);
  uint16_t received = static_cast<uint16_t>(rx[rxLen - 2]) | (static_cast<uint16_t>(rx[rxLen - 1]) << 8);
  if (expected != received) {
    reading.errorCount++;
    logError(F("JSY CRC invalide"), now);
    return false;
  }

  const uint8_t *p = &rx[3];
  uint32_t voltageRaw = readU32(p + 0);
  uint32_t currentRaw = readU32(p + 4);
  uint32_t powerRaw = readU32(p + 8);
  uint32_t powerFactorRaw = readU32(p + 16);
  uint32_t directionRaw = readU32(p + 24);
  uint32_t frequencyRaw = readU32(p + 28);
  uint32_t voltage2Raw = readU32(p + 32);
  uint32_t current2Raw = readU32(p + 36);
  uint32_t power2Raw = readU32(p + 40);
  uint32_t powerFactor2Raw = readU32(p + 48);

  bool injection = ((directionRaw >> 24) & 0xFF) == 0x01;
  bool injection2 = ((directionRaw >> 16) & 0xFF) == 0x01;
  float power = powerRaw / 10000.0f;
  float power2 = power2Raw / 10000.0f;

  reading.voltageV = voltageRaw / 10000.0f;
  reading.currentA = currentRaw / 10000.0f;
  reading.activePowerW = injection ? -power : power;
  reading.gridPowerW = reading.activePowerW;
  reading.powerFactor = powerFactorRaw / 1000.0f;
  reading.frequencyHz = frequencyRaw / 100.0f;
  reading.energyDirection = injection ? "injection" : "consumption";
  reading.available = true;
  reading.lastValidReadMs = now;

  state.voltageV1 = reading.voltageV;
  state.currentA1 = reading.currentA;
  state.activePowerW1 = reading.activePowerW;
  state.powerFactor1 = reading.powerFactor;
  state.energyDirection1 = reading.energyDirection;
  state.voltageV2 = voltage2Raw / 10000.0f;
  state.currentA2 = current2Raw / 10000.0f;
  state.activePowerW2 = injection2 ? -power2 : power2;
  state.powerFactor2 = powerFactor2Raw / 1000.0f;
  state.energyDirection2 = injection2 ? "injection" : "consumption";
  state.productionW = max(0.0f, state.activePowerW2);

  calculatePowerDirection();
  publishRuntime();
  return true;
}

void JSYMK194TManager::calculatePowerDirection() {
  if (reading.gridPowerW < -minInjectionStartW) injectionActive = true;
  if (reading.gridPowerW > -stopBelowInjectionW) injectionActive = false;

  reading.injectionW = injectionActive && reading.gridPowerW < 0 ? -reading.gridPowerW : 0;
  reading.consumptionW = reading.gridPowerW > 0 ? reading.gridPowerW : 0;
  reading.surplusW = reading.injectionW;
}

void JSYMK194TManager::printStatus() {
  Serial.println(F("=== JSY-MK-194T status ==="));
  Serial.print(F("available=")); Serial.println(reading.available ? F("true") : F("false"));
  Serial.print(F("gridPowerW=")); Serial.println(reading.gridPowerW);
  Serial.print(F("injectionW=")); Serial.println(reading.injectionW);
  Serial.print(F("surplusW=")); Serial.println(reading.surplusW);
}

void JSYMK194TManager::sendRequest() {
  uint8_t frame[8] = {modbusAddress, 0x03, 0x00, 0x48, 0x00, 0x0E, 0x00, 0x00};
  uint16_t crc = crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;
  Serial2.write(frame, sizeof(frame));
}

uint16_t JSYMK194TManager::crc16(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

uint32_t JSYMK194TManager::readU32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

void JSYMK194TManager::publishRuntime() {
  state.gridVoltageV = reading.voltageV;
  state.gridCurrentA = reading.currentA;
  state.jsyGridPowerW = reading.gridPowerW;
  state.gridPowerW = reading.gridPowerW;
  state.gridPowerFactor = reading.powerFactor;
  state.gridFrequencyHz = reading.frequencyHz;
  state.gridEnergyDirection = reading.energyDirection;
  state.injectionW = reading.injectionW;
  state.consumptionW = reading.consumptionW;
  state.surplusW = reading.surplusW;
  state.jsyOnline = reading.available;
  state.lastJsyReadMs = reading.lastValidReadMs;
}

void JSYMK194TManager::logError(const __FlashStringHelper *message, uint32_t now) {
  if (now - lastErrorLogMs < 5000) return;
  lastErrorLogMs = now;
  Serial.println(message);
  state.addLog(String(message));
}
