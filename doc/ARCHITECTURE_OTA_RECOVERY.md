# Architecture OTA / Recovery ESP32

Relecture du 20 octobre 2026 : schéma retenu, pas une validation des mises à jour sur matériel. L'[audit courant](AUDIT_2026-10-20.md) détaille les défauts de conservation de configuration et d'authentification/validation des uploads. Firmware principal unique en `ota_0`, recovery minimale en `factory`, mise à jour explicite seulement.

## Capacités des partitions

| Partition | Capacité exacte |
|---|---:|
| `factory` | 1 114 112 octets |
| `ota_0` | 1 966 080 octets |
| `spiffs` utilisée par LittleFS | 1 048 576 octets |

Ces capacités ne sont pas les tailles des binaires. Aucun build récent ni taille actuelle d'image n'est certifié par cette relecture. Les anciens résultats de compilation sont datés dans l'[audit du 18 octobre](AUDIT_2026-10-18.md).

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

## Anciennes estimations de taille (non remesurées)
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
- Upload LittleFS séparé ; la sauvegarde/restauration de `/config` doit être fiabilisée avant d'en garantir la conservation

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
