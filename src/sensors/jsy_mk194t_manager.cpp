#include "jsy_mk194t_manager.h"

void JSYMK194TManager::begin() {
  configureFromJson();
  beginSerial();
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
  if (!enabled) {
    if (reading.available) {
      reading.available = false;
      publishRuntime();
    }
    return;
  }

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
  const uint32_t pollingMs = lastRequestMs ? now - lastRequestMs : 0;
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

  // Decodage volontairement aligne sur l'exemple JSY-MK-194:
  // 14 blocs de 4 octets commencent a l'octet 3 de la trame.
  uint32_t data[14];
  uint8_t dataIndex = 3;
  for (uint8_t i = 0; i < 14; i++) {
    data[i] = readU32(rx + dataIndex);
    dataIndex += 4;
  }

  const uint8_t sens1 = rx[27];
  const uint8_t sens2 = rx[28];
  const bool injection = sens1 > 0;
  const bool injection2 = sens2 > 0;

  float power = data[2] / 10000.0f;
  float power2 = data[10] / 10000.0f;

  const float voltage1 = data[0] / 10000.0f;
  const float current1 = data[1] / 10000.0f;
  const float powerFactor1 = data[4] / 1000.0f;
  const String direction1 = injection ? "injection" : "consumption";
  const float voltage2 = data[8] / 10000.0f;
  const float voltage2ForPower = voltage2 > 1.0f ? voltage2 : voltage1;
  const float current2 = data[9] / 10000.0f;
  const float powerFactor2 = data[12] / 1000.0f;
  const String direction2 = injection2 ? "injection" : "consumption";
  auto calculateActivePower = [](float measuredPower, float voltage, float current, float powerFactor, bool isInjection) {
    float activePower = isInjection ? -measuredPower : measuredPower;
    if (fabs(activePower) >= 5.0f || current <= 0.2f || voltage <= 50.0f) return activePower;
    const float usablePowerFactor = powerFactor > 0.05f && powerFactor <= 1.2f ? powerFactor : 1.0f;
    const float estimatedPower = voltage * current * usablePowerFactor;
    return isInjection ? -estimatedPower : estimatedPower;
  };
  const float activePower1 = calculateActivePower(power, voltage1, current1, powerFactor1, injection);
  const float activePower2 = calculateActivePower(power2, voltage2ForPower, current2, powerFactor2, injection2);
  const bool estimatedPower1 = fabs(power) < 5.0f && fabs(activePower1) >= 5.0f;
  const bool estimatedPower2 = fabs(power2) < 5.0f && fabs(activePower2) >= 5.0f;

  state.voltageV1 = voltage1;
  state.currentA1 = current1;
  state.activePowerW1 = activePower1;
  state.powerFactor1 = powerFactor1;
  state.energyDirection1 = direction1;
  // Certains JSY double pince renvoient une seule tension commune sur la voie 1.
  // Dans ce cas, on affiche et utilise cette tension commune pour la pince 2.
  state.voltageV2 = voltage2ForPower;
  state.currentA2 = current2;
  state.activePowerW2 = activePower2;
  state.powerFactor2 = powerFactor2;
  state.energyDirection2 = direction2;
  state.jsyImportEnergyWh1 = data[3] / 10.0f;
  state.jsyExportEnergyWh1 = data[5] / 10.0f;
  state.jsyImportEnergyWh2 = data[11] / 10.0f;
  state.jsyExportEnergyWh2 = data[13] / 10.0f;
  state.jsyPollingMs = pollingMs;

  if (clamp2Role == "grid") {
    reading.voltageV = voltage2ForPower;
    reading.currentA = current2;
    reading.activePowerW = activePower2;
    reading.gridPowerW = activePower2;
    reading.powerFactor = powerFactor2;
    reading.energyDirection = direction2;
  } else {
    reading.voltageV = voltage1;
    reading.currentA = current1;
    reading.activePowerW = activePower1;
    reading.gridPowerW = activePower1;
    reading.powerFactor = powerFactor1;
    reading.energyDirection = direction1;
  }
  reading.frequencyHz = data[7] / 100.0f;
  reading.available = true;
  reading.lastValidReadMs = now;

  if (clamp1Role == "production") state.productionW = max(0.0f, activePower1);
  else if (clamp2Role == "production") state.productionW = max(0.0f, activePower2);
  else state.productionW = 0.0f;

  if (now - lastAnomalyLogMs > 10000) {
    lastAnomalyLogMs = now;
    String msg = "JSY decode:";
    msg += " D0=" + String(data[0]);
    msg += " D8=" + String(data[8]);
    msg += " S1=" + String(sens1);
    msg += " S2=" + String(sens2);
    msg += " P1 U=" + String(voltage1, 1) + "V";
    msg += " I=" + String(current1, 2) + "A";
    msg += " W=" + String(activePower1, 1);
    msg += " PF=" + String(powerFactor1, 3);
    msg += " sens=" + direction1;
    msg += " | P2 U=" + String(voltage2ForPower, 1) + "V";
    msg += " I=" + String(current2, 2) + "A";
    msg += " W=" + String(activePower2, 1);
    msg += " PF=" + String(powerFactor2, 3);
    msg += " sens=" + direction2;
    if (estimatedPower1) {
      msg += " P1_W_ESTIME";
    }
    if (estimatedPower2) {
      msg += " P2_W_ESTIME";
    }
    Serial.println(msg);
    state.addLog(msg);
  }

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
  Serial.print(F("enabled=")); Serial.println(enabled ? F("true") : F("false"));
  Serial.print(F("Serial2 RX=")); Serial.print(rxPin);
  Serial.print(F(" TX=")); Serial.print(txPin);
  Serial.print(F(" baud=")); Serial.print(baudrate);
  Serial.print(F(" addr=")); Serial.println(modbusAddress);
  Serial.print(F("available=")); Serial.println(reading.available ? F("true") : F("false"));
  Serial.print(F("gridPowerW=")); Serial.println(reading.gridPowerW);
  Serial.print(F("injectionW=")); Serial.println(reading.injectionW);
  Serial.print(F("surplusW=")); Serial.println(reading.surplusW);
}

void JSYMK194TManager::reloadConfig() {
  uint8_t previousRx = rxPin;
  uint8_t previousTx = txPin;
  uint32_t previousBaudrate = baudrate;
  configureFromJson();
  if (previousRx != rxPin || previousTx != txPin || previousBaudrate != baudrate) {
    beginSerial();
  }
  waiting = false;
  rxLen = 0;
  expectedLen = 0;
  lastRequestMs = 0;
  reading.available = false;
  publishRuntime();
  state.addLog("Configuration JSY rechargee");
}

void JSYMK194TManager::stop() {
  waiting = false;
  rxLen = 0;
  expectedLen = 0;
  reading.available = false;
  Serial2.end();
  publishRuntime();
}

void JSYMK194TManager::configureFromJson() {
  JsonObject router = config.system()["router"];
  minInjectionStartW = router["minInjectionStartW"] | 200.0f;
  stopBelowInjectionW = router["stopBelowInjectionW"] | 80.0f;

  JsonObject jsy;
  for (JsonObject sensor : config.sensors()) {
    const char *id = sensor["id"] | "";
    const char *type = sensor["type"] | "";
    if (strcmp(id, "jsy_grid") == 0 || strcmp(type, "JSY-MK-194T") == 0) {
      jsy = sensor;
      break;
    }
  }

  enabled = jsy.isNull() ? true : (jsy["enabled"] | true);
  int address = jsy["modbusAddress"] | (jsy["address"] | 1);
  modbusAddress = constrain(address, 1, 247);
  rxPin = constrain(jsy["rx"] | 26, 0, 39);
  txPin = constrain(jsy["tx"] | 27, 0, 39);
  baudrate = jsy["baudrate"] | 4800;
  if (baudrate != 4800 && baudrate != 9600 && baudrate != 19200 && baudrate != 38400) baudrate = 4800;
  readIntervalMs = jsy["readIntervalMs"] | config.system()["router"]["jsyReadIntervalMs"] | 100;
  readIntervalMs = constrain(readIntervalMs, 80UL, 10000UL);
  timeoutMs = jsy["timeoutMs"] | 300;
  timeoutMs = constrain(timeoutMs, 80UL, 3000UL);
  rs485DirPin = jsy["rs485DirPin"] | jsy["dePin"] | -1;
  if (rs485DirPin < 0 || rs485DirPin > 39) rs485DirPin = -1;

  clamp1Role = "production";
  clamp2Role = "grid";
  JsonArray channels = jsy["channels"].as<JsonArray>();
  for (JsonObject channel : channels) {
    String id = channel["id"] | "";
    String role = channel["role"] | "";
    role.toLowerCase();
    if (id == "clamp1") clamp1Role = role.length() ? role : "production";
    if (id == "clamp2") clamp2Role = role.length() ? role : "grid";
  }
}

void JSYMK194TManager::beginSerial() {
  if (rs485DirPin >= 0) {
    pinMode(rs485DirPin, OUTPUT);
    digitalWrite(rs485DirPin, LOW);
  }
  Serial2.end();
  Serial2.begin(baudrate, SERIAL_8N1, rxPin, txPin);
  Serial.print(F("JSY-MK-194T Serial2: RX"));
  Serial.print(rxPin);
  Serial.print(F(" TX"));
  Serial.print(txPin);
  Serial.print(F(" "));
  Serial.print(baudrate);
  Serial.print(F(" 8N1 adresse "));
  Serial.print(modbusAddress);
  if (rs485DirPin >= 0) {
    Serial.print(F(" DE/RE GPIO"));
    Serial.print(rs485DirPin);
  }
  Serial.println();
}

void JSYMK194TManager::sendRequest() {
  uint8_t frame[8] = {modbusAddress, 0x03, 0x00, 0x48, 0x00, 0x0E, 0x00, 0x00};
  uint16_t crc = crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;
  if (rs485DirPin >= 0) digitalWrite(rs485DirPin, HIGH);
  Serial2.write(frame, sizeof(frame));
  Serial2.flush();
  if (rs485DirPin >= 0) digitalWrite(rs485DirPin, LOW);
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
  state.gridPowerRawW = reading.gridPowerW;
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
