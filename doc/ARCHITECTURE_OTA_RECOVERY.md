# Architecture OTA / Recovery ESP32

## Objectif
Architecture basée sur :
- Factory Recovery
- Firmware principal
- LittleFS

## Partition Scheme Officiel

```csv
factory, app,  factory, 0x10000,  0x110000
ota_0,   app,  ota_0,   0x120000, 0x1E0000
spiffs,  data, spiffs,  0x300000, 0x100000
```

## Arduino IDE
RouteurSolaire Recovery/Main
(factory 1.06MB / main 1.88MB / LittleFS 1MB)

upload.maximum_size=1966080

## État actuel
- Firmware principal : ~1 566 Ko
- Marge main : ~400 Ko
- Recovery : ~949 Ko
- Marge recovery : ~165 Ko
- LittleFS : ~398 Ko utilisés sur 1 Mo

## Recovery autorisé
- WiFi local
- AP secours
- Upload firmware.bin
- Flash ota_0
- Démarrage firmware principal

## Recovery interdit
- ESP-NOW
- MQTT
- Dashboard complet
- GitHub OTA
- HTTPS
- WiFiClientSecure
- AutomateManager
- logique metier du firmware principal

## Main autorise
- BootManager
- AutomateManager
- SensorManager
- ActuatorManager
- RuleEngine
- SafetyManager
- WebUI complete
