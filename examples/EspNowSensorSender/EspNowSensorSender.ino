#include <Arduino.h>

// Exemple autonome Arduino IDE.
// Le .cpp est inclus ici uniquement pour que ce sketch exemple compile seul.
// Dans le firmware RouteurSolaireESP32, espnow_node.cpp est compile normalement depuis src/.
#include "../../src/communication/espnow_node.h"
#include "../../src/communication/espnow_node.cpp"

// ---------------------------------------------------------------------------
// Configuration du noeud capteur
// ---------------------------------------------------------------------------
// Remplacer par la MAC WiFi STA de l'ESP32 destination.
uint8_t receiverMac[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

const uint8_t NODE_ID = 1;
const char NODE_NAME[] = "ESP_LINKY";
const uint8_t SENSOR_TYPE = SENSOR_LINKY;
const uint32_t SEND_INTERVAL_MS = 500;

EspNowNodeConfig nodeConfig{
  NODE_ID,
  NODE_NAME,
  static_cast<uint8_t>(ESPNOW_ROLE_PRODUCER | ESPNOW_ROLE_CONSUMER),
  ESPNOW_CAP_LINKY,
  SENSOR_TYPE,
  3000
};

EspNowNode espNowNode(nodeConfig);
static uint32_t lastSendMs = 0;
static uint32_t lastPrintNodesMs = 0;

void onDiscovery(void *context, const EspNowDiscoveredNode &node) {
  (void)context;
  Serial.print(F("Noeud ESP-NOW detecte: "));
  Serial.print(node.nodeName);
  Serial.print(F(" mac="));
  EspNowNode::printMac(Serial, node.mac);
  Serial.print(F(" roles=0x"));
  Serial.print(node.roleFlags, HEX);
  Serial.print(F(" capabilities=0x"));
  Serial.println(node.capabilityFlags, HEX);
}

void onSensorPacket(void *context, const uint8_t *mac, const EspNowSensorPacket &packet) {
  (void)context;
  Serial.print(F("Trame capteur recue depuis "));
  EspNowNode::printMac(Serial, mac);
  Serial.print(F(" nodeName="));
  Serial.print(packet.nodeName);
  Serial.print(F(" valueCount="));
  Serial.println(packet.valueCount);
}

void buildLinkyPacket() {
  espNowNode.clearPacket(SENSOR_TYPE, true);

  // Remplacer ces valeurs simulees par les mesures TIC Linky reelles.
  const float simulatedPappVa = 850.0f;
  const float simulatedIinstA = 3.7f;
  const float simulatedSinstsVa = 820.0f;
  const float simulatedGridW = 420.0f;
  const float simulatedBaseWh = 12345678.0f;

  espNowNode.addValue("PAPP", simulatedPappVa, "VA");
  espNowNode.addValue("IINST", simulatedIinstA, "A");
  espNowNode.addValue("SINSTS", simulatedSinstsVa, "VA");

  // Convention routeur solaire: GRID > 0 achat reseau, GRID < 0 injection.
  espNowNode.addValue("GRID", simulatedGridW, "W");
  espNowNode.addValue("BASE", simulatedBaseWh, "Wh");
}

void buildJsyPacket() {
  espNowNode.clearPacket(SENSOR_TYPE, true);

  // Remplacer ces valeurs simulees par les registres JSY-MK-194T reels.
  espNowNode.addValue("VOLT", 231.4f, "V");
  espNowNode.addValue("CURR", 4.21f, "A");
  espNowNode.addValue("POWER", 760.0f, "W");
  espNowNode.addValue("PF", 0.96f, "");
  espNowNode.addValue("FREQ", 50.0f, "Hz");
  espNowNode.addValue("ENERGY", 12.345f, "kWh");
}

void buildTemperaturePacket() {
  espNowNode.clearPacket(SENSOR_TYPE, true);

  // Remplacer ces valeurs simulees par les mesures DS18B20, SHT, DHT, etc.
  espNowNode.addValue("TEMP", 21.8f, "C");
  espNowNode.addValue("HUM", 48.0f, "%");
}

void buildBatteryPacket() {
  espNowNode.clearPacket(SENSOR_TYPE, true);

  // Remplacer ces valeurs simulees par les mesures batterie reelles.
  espNowNode.addValue("BATV", 52.1f, "V");
  espNowNode.addValue("BATA", -8.4f, "A");
  espNowNode.addValue("SOC", 76.0f, "%");
  espNowNode.addValue("BATP", -438.0f, "W");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== EspNowSensorSender boot ==="));
  espNowNode.setDiscoveryHandler(onDiscovery, nullptr);
  espNowNode.setSensorPacketHandler(onSensorPacket, nullptr);
  espNowNode.begin();
}

void loop() {
  const uint32_t now = millis();
  espNowNode.loop(now);

  if (now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;

    // Demo par defaut: trame Linky simulee toutes les 500 ms.
    // Pour transformer ce noeud en JSY, temperature ou batterie, changer les
    // constantes en haut du fichier puis appeler la fonction buildXXXPacket().
    buildLinkyPacket();
    espNowNode.sendSensorPacket(receiverMac);
  }

  if (now - lastPrintNodesMs >= 10000) {
    lastPrintNodesMs = now;
    espNowNode.printDiscoveredNodes();
  }
}

