# ARCHITECTURE.md - Architecture RouteurSolaireESP32

## Vue generale

RouteurSolaireESP32 est un systeme distribue base sur ESP32.

Il permet :

- mesure reseau ;
- mesure production / consommation ;
- routage du surplus solaire ;
- pilotage chauffe-eau ;
- supervision de capteurs locaux et ESP-NOW ;
- redondance MASTER/BACKUP ;
- WebUI embarquee ;
- maintenance locale par architecture Recovery/Main.

---

## Principe central

Le firmware principal reste unique pour tous les ESP32.

Chaque ESP se specialise par configuration :

```text
Firmware principal unique
   ->
/config/device.json
   ->
Role + capteurs + actionneurs + regles
```

L'architecture de boot est separee :

```text
factory = Recovery minimal
ota_0   = Main applicatif
spiffs  = LittleFS
```

---

## Architecture Recovery/Main

Le projet n'utilise plus le double slot OTA classique `ota_0` / `ota_1` comme strategie cible.

Schema valide :

```csv
nvs,        data, nvs,     0x9000,   0x5000
otadata,    data, ota,     0xe000,   0x2000
factory,    app,  factory, 0x10000,  0x110000
ota_0,      app,  ota_0,   0x120000, 0x1E0000
spiffs,     data, spiffs,  0x300000, 0x100000
```

Roles :

- `factory` : recovery minimal, local, robuste ;
- `ota_0` : firmware principal complet ;
- `spiffs` : partition utilisee par LittleFS.

Le recovery ne contient pas la logique metier du routeur solaire.

---

## Modules principaux du main

Modules a respecter :

- ConfigManager
- RuntimeState
- SensorManager
- ActuatorManager
- AutomateManager
- RuleEngine
- SolarRouter
- PIDController
- SafetyManager
- SimulationManager
- EspNowManager
- EspNowNode
- RedundancyManager
- MqttManager
- WebUi
- DisplayManager
- Logger
- StatusLed
- WiFiManager
- BootManager

---

## BootManager

`BootManager` gere le choix de partition et les informations de boot.

Fonctions attendues ou presentes :

- `setNextBootFactory()`
- `setNextBootMain()`
- `bootToFactory()`
- `bootToMain()`
- `getRunningPartitionInfo()`
- `getBootPartitionInfo()`
- `getMainPartitionInfo()`

Cycle cible valide :

```text
Boot ESP32
 -> factory recovery
 -> fenetre de maintenance
 -> boot ota_0
 -> main
 -> reboots normaux sur ota_0
 -> retour factory seulement sur demande recovery explicite
```

Contraintes :

- le recovery peut demarrer le main ;
- le main peut demander le retour recovery ;
- le main ne remet pas automatiquement le prochain boot sur factory ;
- l'update firmware ecrit uniquement dans `ota_0` ;
- le recovery ne doit jamais etre ecrase.

---

## AutomateManager

`AutomateManager` appartient au firmware principal.

Il sert a centraliser les automatismes applicatifs non bloquants :

- orchestration des sequences ;
- decisions automatiques a partir de RuntimeState ;
- coordination avec RuleEngine, SolarRouter et PIDController ;
- journalisation des actions automatiques ;
- respect strict des interdictions SafetyManager.

Il ne doit jamais etre charge dans le recovery.

---

## Demarrage logique du main

1. securite actionneurs ;
2. BootManager et informations partition ;
3. chargement configuration ;
4. logs ;
5. capteurs ;
6. actionneurs ;
7. Wi-Fi ;
8. ESP-NOW ;
9. redondance ;
10. WebUI ;
11. AutomateManager / regles / routage ;
12. supervision ;
13. loop non bloquante.

---

## Configuration

Fichiers de configuration :

```text
/config/device.json
/config/system.json
/config/sensors.json
/config/actuators.json
/config/rules.json
```

Ces fichiers doivent survivre aux mises a jour.

---

## Capteurs

Capteurs connus :

- JSY-MK-194T ;
- Linky TIC ;
- DS18B20 ;
- ESP-NOW virtuels ;
- futurs capteurs.

Tous doivent passer par SensorManager.

---

## Actionneurs

Actionneurs connus ou prevus :

- SSR ;
- relais ;
- RobotDyn triac ;
- PWM ;
- actionneurs distants ESP-NOW.

Modes :

- AUTO ;
- MANUEL ;
- FORCE ;
- OFF ;
- SECURITE.

SafetyManager garde toujours la priorite.

---

## ESP-NOW

Trames fonctionnelles :

- DISCOVERY ;
- HEARTBEAT ;
- SENSOR_DATA ;
- FAST_DATA ;
- DIAGNOSTIC ;
- SENSOR_DISCOVERY ;
- ACTUATOR_DISCOVERY.

Le protocole doit rester leger et robuste.

ESP-NOW ne transporte jamais de firmware `.bin`.

---

## Redondance

Roles :

- MASTER ;
- BACKUP ;
- STANDALONE.

Le BACKUP surveille le MASTER.

Cas critique :

```text
DOUBLE_MASTER_RISK
```

---

## WebUI

Servie depuis LittleFS.

Technos :

- HTML ;
- CSS ;
- JavaScript natif ;
- SVG ;
- Canvas.

Le dashboard lit l'etat global via API.

---

## Maintenance et mises a jour

Mises a jour validees :

- firmware main par upload local depuis recovery vers `ota_0` ;
- LittleFS separe, avec preservation de `/config`.

GitHub OTA cote ESP32 n'est pas dans l'architecture validee actuelle.

---

## Vision cible

Le MASTER devient superviseur du parc ESP32 :

- inventaire ;
- versions ;
- etat des capteurs ;
- etat des actionneurs ;
- diagnostic ;
- ordre de maintenance si valide ;
- logs.
