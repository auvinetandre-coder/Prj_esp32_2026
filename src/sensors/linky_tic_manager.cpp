#include "linky_tic_manager.h"

static const uint16_t TIC_MAX_LINE_LENGTH = 192;

void LinkyTICManager::begin() {
  loadConfig();

  if (!configured) {
    setStatus(TIC_NOT_CONFIGURED);
    publishRuntime();
    Serial.println(F("TIC Linky non configuree"));
    return;
  }

  Serial1.begin(baudrate, SERIAL_7E1, rxPin, -1);
  setStatus(TIC_TIMEOUT);
  publishRuntime();
  Serial.print(F("TIC Linky Serial1 pret: RX"));
  Serial.print(rxPin);
  Serial.print(F(" "));
  Serial.print(baudrate);
  Serial.println(F(" bauds 7E1"));
}

void LinkyTICManager::loop() {
  loop(millis());
}

void LinkyTICManager::loop(uint32_t now) {
  if (!configured) return;

  while (Serial1.available()) {
    char c = static_cast<char>(Serial1.read());
    lastByteMs = now;

    if (c == 0x02) {
      frameActive = true;
      frameHasValidLine = false;
      lineOverflow = false;
      lineBuffer = "";
      frameBuffer = "";
      continue;
    }

    if (c == 0x03) {
      if (parseFrame()) {
        reading.available = true;
        reading.lastValidReadMs = now;
        setStatus(TIC_OK);
      } else {
        reading.errorCount++;
        setStatus(TIC_FRAME_ERROR);
        logError(F("TIC trame invalide"), now);
      }
      publishRuntime();
      frameActive = false;
      lineOverflow = false;
      lineBuffer = "";
      continue;
    }

    if (!frameActive && c != '\n' && c != '\r') frameActive = true;

    if (c == '\n' || c == '\r') {
      lineOverflow = false;
      if (lineBuffer.length()) {
        if (parseLine(lineBuffer)) {
          frameHasValidLine = true;
          reading.available = true;
          reading.lastValidReadMs = now;
          setStatus(TIC_OK);
          publishRuntime();
        }
        frameBuffer += lineBuffer;
        frameBuffer += '\n';
        lineBuffer = "";
      }
      continue;
    }

    if (lineOverflow) continue;
    if (c < 0x20 && c != '\t') continue;

    if (lineBuffer.length() < TIC_MAX_LINE_LENGTH) {
      lineBuffer += c;
    } else {
      lineBuffer = "";
      lineOverflow = true;
      reading.errorCount++;
      setStatus(TIC_FRAME_ERROR);
      logError(F("TIC ligne trop longue"), now);
    }
  }

  if (reading.available && now - reading.lastValidReadMs > timeoutMs) {
    reading.available = false;
    setStatus(TIC_TIMEOUT);
    publishRuntime();
    logError(F("TIC timeout"), now);
  }

  logPeriodicValues(now);
}

bool LinkyTICManager::parseLine(const String &rawLine) {
  String line = rawLine;
  while (line.length() && (line.charAt(0) == '\r' || line.charAt(0) == '\n')) line.remove(0, 1);
  while (line.length() && (line.charAt(line.length() - 1) == '\r' || line.charAt(line.length() - 1) == '\n')) line.remove(line.length() - 1);
  if (line.length() < 4) return false;
  if (!validateChecksum(line)) return false;

  String payload = line.substring(0, line.length() - 2);
  payload.trim();

  char separator = payload.indexOf('\t') >= 0 ? '\t' : ' ';
  int first = payload.indexOf(separator);
  if (first <= 0) return false;

  String label = payload.substring(0, first);
  String rest = payload.substring(first + 1);
  rest.trim();

  String value = rest;
  int next = rest.indexOf(separator);
  if (next >= 0) {
    // En TIC standard certains groupes contiennent une horodate avant la valeur.
    String maybeTime = rest.substring(0, next);
    String after = rest.substring(next + 1);
    after.trim();
    if (maybeTime.length() >= 10 && after.length()) value = after;
  }

  value.trim();
  applyLabelValue(label, value);
  return true;
}

bool LinkyTICManager::parseFrame() {
  if (lineBuffer.length()) {
    if (parseLine(lineBuffer)) frameHasValidLine = true;
    frameBuffer += lineBuffer;
    frameBuffer += '\n';
    lineBuffer = "";
  }
  if (!frameHasValidLine) return false;
  reading.lastFrame = frameBuffer;
  return true;
}

bool LinkyTICManager::validateChecksum(const String &line) {
  if (line.length() < 3) return false;
  char received = line.charAt(line.length() - 1);
  char separator = line.charAt(line.length() - 2);
  if (separator != ' ' && separator != '\t') return false;

  uint16_t sum = 0;
  for (uint16_t i = 0; i < line.length() - 2; i++) sum += static_cast<uint8_t>(line.charAt(i));
  char expected = static_cast<char>((sum & 0x3F) + 0x20);
  return expected == received;
}

void LinkyTICManager::applyLabelValue(const String &label, const String &value) {
  if (label == "PAPP") {
    reading.apparentPowerVA = value.toFloat();
    reading.gridPowerW = reading.apparentPowerVA;
  } else if (label == "SINSTS") {
    reading.apparentPowerVA = value.toFloat();
    reading.gridPowerW = reading.apparentPowerVA;
  } else if (label == "SINSTI") {
    float injectedVA = value.toFloat();
    reading.apparentPowerVA = injectedVA;
    reading.gridPowerW = injectedVA > 0 ? -injectedVA : 0;
  } else if (label == "IINST" || label == "IRMS1") {
    reading.currentA = value.toFloat();
  } else if (label == "BASE" || label == "HCHC" || label == "EAST") {
    reading.energyWh = strtoull(value.c_str(), nullptr, 10);
  } else if (label == "OPTARIF" || label == "NGTF") {
    reading.tariff = value;
  } else if (label == "PTEC" || label == "LTARF") {
    reading.period = value;
  }
}

void LinkyTICManager::printStatus() {
  Serial.println(F("=== TIC Linky status ==="));
  Serial.print(F("status=")); Serial.println(statusText(reading.status));
  Serial.print(F("available=")); Serial.println(reading.available ? F("true") : F("false"));
  Serial.print(F("apparentPowerVA=")); Serial.println(reading.apparentPowerVA);
  Serial.print(F("currentA=")); Serial.println(reading.currentA);
  Serial.print(F("tariff=")); Serial.println(reading.tariff);
  Serial.print(F("period=")); Serial.println(reading.period);
}

void LinkyTICManager::reloadConfig() {
  uint8_t previousRx = rxPin;
  uint32_t previousBaudrate = baudrate;
  loadConfig();
  frameActive = false;
  frameHasValidLine = false;
  lineOverflow = false;
  lineBuffer = "";
  frameBuffer = "";
  if (!configured) {
    setStatus(TIC_NOT_CONFIGURED);
    publishRuntime();
    return;
  }
  if (previousRx != rxPin || previousBaudrate != baudrate) {
    Serial1.end();
    Serial1.begin(baudrate, SERIAL_7E1, rxPin, -1);
  }
  setStatus(TIC_TIMEOUT);
  publishRuntime();
  state.addLog("Configuration TIC rechargee");
}

void LinkyTICManager::setConfigured(bool value) {
  configured = value;
  if (!configured) {
    setStatus(TIC_NOT_CONFIGURED);
    publishRuntime();
  }
}

void LinkyTICManager::stop() {
  configured = false;
  frameActive = false;
  frameHasValidLine = false;
  lineOverflow = false;
  lineBuffer = "";
  frameBuffer = "";
  Serial1.end();
  setStatus(TIC_NOT_CONFIGURED);
  publishRuntime();
}

void LinkyTICManager::loadConfig() {
  configured = true;
  JsonArray sensors = config.sensors();
  for (JsonObject sensor : sensors) {
    if (String(sensor["id"] | "") == "tic_linky") {
      configured = sensor["enabled"] | true;
      mode = sensor["mode"] | "historique";
      rxPin = sensor["rx"] | 26;
      txPin = sensor["tx"] | 27;
      baudrate = sensor["baudrate"] | (mode == "standard" ? 9600 : 1200);
      timeoutMs = sensor["timeoutMs"] | 5000;
      break;
    }
  }
}

void LinkyTICManager::publishRuntime() {
  state.ticAvailable = reading.available;
  state.ticStatus = statusText(reading.status);
  state.ticApparentPowerVA = reading.apparentPowerVA;
  state.ticGridPowerW = reading.gridPowerW;
  state.ticCurrentA = reading.currentA;
  state.ticEnergyWh = reading.energyWh;
  state.ticTariff = reading.tariff;
  state.ticPeriod = reading.period;
  state.lastTicReadMs = reading.lastValidReadMs;
  state.ticErrorCount = reading.errorCount;
}

void LinkyTICManager::setStatus(LinkyTICStatus status) {
  reading.status = status;
  if (status != TIC_OK) reading.available = false;
}

const char *LinkyTICManager::statusText(LinkyTICStatus status) const {
  switch (status) {
    case TIC_OK: return "TIC_OK";
    case TIC_FRAME_ERROR: return "TIC_FRAME_ERROR";
    case TIC_NOT_CONFIGURED: return "TIC_NOT_CONFIGURED";
    default: return "TIC_TIMEOUT";
  }
}

void LinkyTICManager::logError(const __FlashStringHelper *message, uint32_t now) {
  if (now - lastErrorLogMs < 5000) return;
  lastErrorLogMs = now;
  Serial.println(message);
  state.addLog(String(message));
}

void LinkyTICManager::logPeriodicValues(uint32_t now) {
  if (now - lastPeriodicLogMs < 60000UL) return;
  lastPeriodicLogMs = now;

  String line = "TIC valeurs: status=";
  line += statusText(reading.status);
  line += " available=";
  line += reading.available ? "true" : "false";
  line += " mode=";
  line += mode;
  line += " rx=GPIO";
  line += String(rxPin);
  line += " baud=";
  line += String(baudrate);
  line += " apparentPowerVA=";
  line += isnan(reading.apparentPowerVA) || isinf(reading.apparentPowerVA) ? "N/A" : String(reading.apparentPowerVA, 0);
  line += " gridPowerW=";
  line += isnan(reading.gridPowerW) || isinf(reading.gridPowerW) ? "N/A" : String(reading.gridPowerW, 0);
  line += " currentA=";
  line += isnan(reading.currentA) || isinf(reading.currentA) ? "N/A" : String(reading.currentA, 2);
  line += " energyWh=";
  line += String(static_cast<uint32_t>(reading.energyWh));
  line += " tariff=";
  line += reading.tariff.length() ? reading.tariff : "N/A";
  line += " period=";
  line += reading.period.length() ? reading.period : "N/A";
  line += " lastValidAgeMs=";
  line += reading.lastValidReadMs ? String(now - reading.lastValidReadMs) : "N/A";
  line += " errors=";
  line += String(reading.errorCount);

  Serial.println(line);
  state.logEvent(reading.available ? "INFO" : "WARNING", "TIC_VALUES", line, "LinkyTIC");
}
