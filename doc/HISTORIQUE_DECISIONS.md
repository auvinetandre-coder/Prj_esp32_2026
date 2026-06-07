# Historique des decisions

## Architecture Recovery/Main

- Le double slot OTA classique `ota_0` / `ota_1` est abandonne pour l'ESP32 4 MB.
- L'architecture validee est :
  - `factory` : recovery minimal ;
  - `ota_0` : firmware principal ;
  - `spiffs` : partition utilisee par LittleFS.
- Le recovery est le point de maintenance local.
- Le firmware principal reste l'application complete.
- Le recovery ne doit jamais etre ecrase par une mise a jour du main.

## Partitions

- `factory` : `0x110000`.
- `ota_0` : `0x1E0000`.
- LittleFS / `spiffs` : `0x100000`.
- `otadata` et NVS sont conserves.

## Boot

- `BootManager` porte la selection de partition.
- Le recovery peut basculer vers `ota_0`.
- Le main peut basculer vers `factory` uniquement sur demande recovery explicite.
- Le reboot normal du main reste sur `ota_0`.
- Le cycle operationnel courant est `factory -> ota_0`, puis `ota_0` aux reboots normaux.

## Recovery

- Recovery minimal.
- AP secours local.
- Wi-Fi local si possible.
- Update locale par fichier `.bin`.
- Ecriture firmware uniquement dans `ota_0`.
- GitHub OTA abandonne cote ESP32.
- HTTPS, GitHub, ESP-NOW, MQTT, dashboard complet, PID et logique metier interdits dans le recovery.

## Firmware principal

- Firmware principal unique.
- Specialisation par configuration locale.
- `AutomateManager` est ajoute au cadrage du main pour centraliser les automatismes applicatifs non bloquants.
- `BootManager` est ajoute au cadrage du main pour gerer le retour recovery et les infos partition.
- SafetyManager reste prioritaire sur toute action automatique.

## LittleFS

- LittleFS reste separe du firmware.
- La partition peut rester nommee `spiffs` dans le CSV.
- `/config` doit survivre aux mises a jour.

## ESP-NOW

- Import manuel uniquement.
- Discovery et Heartbeat conserves.
- Les capteurs ESP-NOW sont vus comme des capteurs locaux virtuels.
- ESP-NOW ne transporte jamais de binaire OTA.

## Dashboard

- Utilise SensorManager et l'etat global.
- Pas de logique specifique ESP-NOW.
- Reste en HTML, CSS, JavaScript natif, SVG et Canvas.

## Evolutions futures

- Stabiliser completement Recovery/Main.
- Optimiser WebUI et `webUi.loop`.
- Auditer la taille firmware.
- Etudier ESP32-S3 apres validation ESP32 4 MB.
