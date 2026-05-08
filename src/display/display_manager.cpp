#include "display_manager.h"

void DisplayManager::begin() {
  loadConfig();
  if (!enabled) {
    Serial.println(F("Display SSD1309 desactive"));
    return;
  }

  pinMode(pinCs, OUTPUT);
  pinMode(pinDc, OUTPUT);
  pinMode(pinReset, OUTPUT);
  digitalWrite(pinCs, HIGH);
  digitalWrite(pinDc, LOW);
  digitalWrite(pinReset, HIGH);

  SPI.begin(pinSclk, -1, pinMosi, pinCs);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  oledInit();
  clear();
  ready = true;
  Serial.print(F("Display SSD1309 SPI pret: SCLK"));
  Serial.print(pinSclk);
  Serial.print(F(" MOSI"));
  Serial.print(pinMosi);
  Serial.print(F(" RES"));
  Serial.print(pinReset);
  Serial.print(F(" DC"));
  Serial.print(pinDc);
  Serial.print(F(" CS"));
  Serial.println(pinCs);
}

void DisplayManager::loop(uint32_t now) {
  if (!ready || now - lastRefreshMs < refreshIntervalMs) return;
  lastRefreshMs = now;
  render();
}

void DisplayManager::loadConfig() {
  JsonObject display = config.system()["display"];
  if (display.isNull()) return;
  enabled = display["enabled"] | true;
  pinSclk = display["sclk"] | 18;
  pinMosi = display["mosi"] | 19;
  pinReset = display["reset"] | 16;
  pinDc = display["dc"] | 4;
  pinCs = display["cs"] | 15;
  refreshIntervalMs = display["refreshMs"] | 1000;
  refreshIntervalMs = constrain(refreshIntervalMs, 500UL, 10000UL);
}

void DisplayManager::oledReset() {
  digitalWrite(pinReset, HIGH);
  delay(20);
  digitalWrite(pinReset, LOW);
  delay(50);
  digitalWrite(pinReset, HIGH);
  delay(100);
}

void DisplayManager::oledInit() {
  oledReset();
  command(0xAE);
  command(0xD5); command(0x80);
  command(0xA8); command(0x3F);
  command(0xD3); command(0x00);
  command(0x40);
  command(0xA1);
  command(0xC8);
  command(0xDA); command(0x12);
  command(0x81); command(0xCF);
  command(0xD9); command(0xF1);
  command(0xDB); command(0x40);
  command(0xA4);
  command(0xA6);
  command(0x20); command(0x00);
  command(0x2E);
  command(0xAF);
}

void DisplayManager::command(uint8_t value) {
  digitalWrite(pinDc, LOW);
  digitalWrite(pinCs, LOW);
  SPI.transfer(value);
  digitalWrite(pinCs, HIGH);
}

void DisplayManager::data(uint8_t value) {
  digitalWrite(pinDc, HIGH);
  digitalWrite(pinCs, LOW);
  SPI.transfer(value);
  digitalWrite(pinCs, HIGH);
}

void DisplayManager::setWindow(uint8_t colStart, uint8_t colEnd, uint8_t pageStart, uint8_t pageEnd) {
  command(0x21);
  command(colStart);
  command(colEnd);
  command(0x22);
  command(pageStart);
  command(pageEnd);
}

void DisplayManager::clear() {
  setWindow(0, 127, 0, 7);
  digitalWrite(pinDc, HIGH);
  digitalWrite(pinCs, LOW);
  for (uint16_t i = 0; i < 1024; i++) SPI.transfer(0x00);
  digitalWrite(pinCs, HIGH);
}

void DisplayManager::drawPixel(uint8_t x, uint8_t y, bool on) {
  if (x >= 128 || y >= 64) return;
  uint8_t pageIndex = y / 8;
  uint8_t bit = y % 8;
  setWindow(x, x, pageIndex, pageIndex);
  data(on ? (1 << bit) : 0);
}

void DisplayManager::drawChar(uint8_t x, uint8_t y, char c) {
  if (x > 122 || y > 56) return;
  uint8_t glyph[5];
  glyphFor(c, glyph);
  uint8_t pageIndex = y / 8;
  setWindow(x, x + 5, pageIndex, pageIndex);
  digitalWrite(pinDc, HIGH);
  digitalWrite(pinCs, LOW);
  for (uint8_t i = 0; i < 5; i++) SPI.transfer(glyph[i]);
  SPI.transfer(0x00);
  digitalWrite(pinCs, HIGH);
}

void DisplayManager::drawText(uint8_t x, uint8_t y, const String &text) {
  uint8_t cursor = x;
  for (uint16_t i = 0; i < text.length() && cursor <= 122; i++) {
    char c = text.charAt(i);
    if (c >= 'a' && c <= 'z') c -= 32;
    drawChar(cursor, y, c);
    cursor += 6;
  }
}

void DisplayManager::drawHeader(const String &title) {
  drawText(0, 0, title);
  for (uint8_t x = 0; x < 128; x++) drawPixel(x, 10, true);
}

void DisplayManager::render() {
  clear();
  switch (page) {
    case 0: renderOverview(); break;
    case 1: renderPower(); break;
    case 2: renderTemperatures(); break;
    default: renderOutputs(); break;
  }
  page = (page + 1) % 4;
}

void DisplayManager::renderOverview() {
  drawHeader("ROUTEUR SOLAIRE");
  drawText(0, 16, state.moduleName.substring(0, 20));
  drawText(0, 28, String("IP ") + (state.stationIp != "0.0.0.0" ? state.stationIp : state.apIp));
  drawText(0, 40, String("WIFI ") + state.networkMode);
  drawText(0, 52, String("SEC ") + state.safetyLevel);
}

void DisplayManager::renderPower() {
  drawHeader(String("SOURCE ") + state.gridPowerSource);
  drawText(0, 16, String("RESEAU ") + fmtFloat(state.gridPowerW) + " W");
  drawText(0, 28, String("INJ ") + fmtFloat(state.injectionW) + " W");
  drawText(0, 40, String("CONSO ") + fmtFloat(state.consumptionW) + " W");
  drawText(0, 52, state.gridPowerSource == "TIC" ? state.ticStatus : (state.jsyOnline ? "JSY OK" : "JSY ABS"));
}

void DisplayManager::renderTemperatures() {
  drawHeader("TEMPERATURES");
  drawText(0, 16, String("S1 ") + fmtFloat(state.ds18b20Temps[0], 1) + " C " + (state.ds18b20Available[0] ? "OK" : "ABS"));
  drawText(0, 28, String("S2 ") + fmtFloat(state.ds18b20Temps[1], 1) + " C " + (state.ds18b20Available[1] ? "OK" : "ABS"));
  drawText(0, 40, String("S3 ") + fmtFloat(state.ds18b20Temps[2], 1) + " C " + (state.ds18b20Available[2] ? "OK" : "ABS"));
  drawText(0, 52, state.ds18b20CriticalMissing ? "SONDE CRIT ABS" : "SONDES OK");
}

void DisplayManager::renderOutputs() {
  drawHeader("SORTIES");
  drawText(0, 16, String("SSR1 ") + fmtFloat(state.ssr1PowerPct) + " %");
  drawText(0, 28, String("SSR2 ") + fmtFloat(state.ssr2PowerPct) + " %");
  drawText(0, 40, String("TRIAC ") + fmtFloat(state.robotDynPowerPct) + " %");
  drawText(0, 52, state.simulationMode ? "SIMULATION" : state.systemMode);
}

String DisplayManager::fmtFloat(float value, uint8_t decimals) {
  if (isnan(value)) return "--";
  return String(value, static_cast<unsigned int>(decimals));
}

void DisplayManager::glyphFor(char c, uint8_t out[5]) {
  memset(out, 0, 5);
  switch (c) {
    case ' ': break;
    case '-': out[1]=0x08; out[2]=0x08; out[3]=0x08; break;
    case '.': out[2]=0x40; break;
    case '%': out[0]=0x63; out[1]=0x13; out[2]=0x08; out[3]=0x64; out[4]=0x63; break;
    case '/': out[0]=0x40; out[1]=0x30; out[2]=0x08; out[3]=0x06; out[4]=0x01; break;
    case ':': out[2]=0x36; break;
    case '0': out[0]=0x3E; out[1]=0x51; out[2]=0x49; out[3]=0x45; out[4]=0x3E; break;
    case '1': out[0]=0x00; out[1]=0x42; out[2]=0x7F; out[3]=0x40; break;
    case '2': out[0]=0x42; out[1]=0x61; out[2]=0x51; out[3]=0x49; out[4]=0x46; break;
    case '3': out[0]=0x21; out[1]=0x41; out[2]=0x45; out[3]=0x4B; out[4]=0x31; break;
    case '4': out[0]=0x18; out[1]=0x14; out[2]=0x12; out[3]=0x7F; out[4]=0x10; break;
    case '5': out[0]=0x27; out[1]=0x45; out[2]=0x45; out[3]=0x45; out[4]=0x39; break;
    case '6': out[0]=0x3C; out[1]=0x4A; out[2]=0x49; out[3]=0x49; out[4]=0x30; break;
    case '7': out[0]=0x01; out[1]=0x71; out[2]=0x09; out[3]=0x05; out[4]=0x03; break;
    case '8': out[0]=0x36; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x36; break;
    case '9': out[0]=0x06; out[1]=0x49; out[2]=0x49; out[3]=0x29; out[4]=0x1E; break;
    case 'A': out[0]=0x7E; out[1]=0x11; out[2]=0x11; out[3]=0x11; out[4]=0x7E; break;
    case 'B': out[0]=0x7F; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x36; break;
    case 'C': out[0]=0x3E; out[1]=0x41; out[2]=0x41; out[3]=0x41; out[4]=0x22; break;
    case 'D': out[0]=0x7F; out[1]=0x41; out[2]=0x41; out[3]=0x22; out[4]=0x1C; break;
    case 'E': out[0]=0x7F; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x41; break;
    case 'F': out[0]=0x7F; out[1]=0x09; out[2]=0x09; out[3]=0x09; out[4]=0x01; break;
    case 'G': out[0]=0x3E; out[1]=0x41; out[2]=0x49; out[3]=0x49; out[4]=0x7A; break;
    case 'H': out[0]=0x7F; out[1]=0x08; out[2]=0x08; out[3]=0x08; out[4]=0x7F; break;
    case 'I': out[0]=0x41; out[1]=0x41; out[2]=0x7F; out[3]=0x41; out[4]=0x41; break;
    case 'J': out[0]=0x20; out[1]=0x40; out[2]=0x41; out[3]=0x3F; out[4]=0x01; break;
    case 'K': out[0]=0x7F; out[1]=0x08; out[2]=0x14; out[3]=0x22; out[4]=0x41; break;
    case 'L': out[0]=0x7F; out[1]=0x40; out[2]=0x40; out[3]=0x40; out[4]=0x40; break;
    case 'M': out[0]=0x7F; out[1]=0x02; out[2]=0x0C; out[3]=0x02; out[4]=0x7F; break;
    case 'N': out[0]=0x7F; out[1]=0x04; out[2]=0x08; out[3]=0x10; out[4]=0x7F; break;
    case 'O': out[0]=0x3E; out[1]=0x41; out[2]=0x41; out[3]=0x41; out[4]=0x3E; break;
    case 'P': out[0]=0x7F; out[1]=0x09; out[2]=0x09; out[3]=0x09; out[4]=0x06; break;
    case 'Q': out[0]=0x3E; out[1]=0x41; out[2]=0x51; out[3]=0x21; out[4]=0x5E; break;
    case 'R': out[0]=0x7F; out[1]=0x09; out[2]=0x19; out[3]=0x29; out[4]=0x46; break;
    case 'S': out[0]=0x46; out[1]=0x49; out[2]=0x49; out[3]=0x49; out[4]=0x31; break;
    case 'T': out[0]=0x01; out[1]=0x01; out[2]=0x7F; out[3]=0x01; out[4]=0x01; break;
    case 'U': out[0]=0x3F; out[1]=0x40; out[2]=0x40; out[3]=0x40; out[4]=0x3F; break;
    case 'V': out[0]=0x1F; out[1]=0x20; out[2]=0x40; out[3]=0x20; out[4]=0x1F; break;
    case 'W': out[0]=0x7F; out[1]=0x20; out[2]=0x18; out[3]=0x20; out[4]=0x7F; break;
    case 'X': out[0]=0x63; out[1]=0x14; out[2]=0x08; out[3]=0x14; out[4]=0x63; break;
    case 'Y': out[0]=0x07; out[1]=0x08; out[2]=0x70; out[3]=0x08; out[4]=0x07; break;
    case 'Z': out[0]=0x61; out[1]=0x51; out[2]=0x49; out[3]=0x45; out[4]=0x43; break;
    default: out[0]=0x7F; out[4]=0x7F; break;
  }
}
