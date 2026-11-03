# Historique des decisions

Relecture du 20 octobre 2026 : les décisions ci-dessous sont conservées. Leur présence n'est pas une preuve que chaque exigence est garantie dans le code. L'[audit courant](AUDIT_2026-10-20.md) et le [bilan](ETAT_DU_PROJET.md) suivent la réalisation et les défauts. Les propositions de commandes typées, rôles séparés et bilans persistants restent des évolutions à valider, sans nouvelle décision implicite dans cette relecture.

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

## 21 octobre 2026 — Suppression des roles et de la redondance

Demande utilisateur explicite : retirer tous les roles de module, MASTER/BACKUP, les anciens champs de configuration/API et l onglet Secours. Les capacites configurees determinent seules le fonctionnement. Migration ciblee, protections locales et recovery explicite conserves. Voir [la decision complete](SUPPRESSION_ROLES.md).

## 2026-10-21 — Routage generique

A la demande utilisateur, suppression des hypotheses liees a une charge particuliere. Champs API et WebUI generiques, migration des configurations, cible principale configurable et validation des equipements presents. Les identifiants existants restent stables pour conserver les liens des regles, MQTT et ESP-NOW. Voir [Routage generique](ROUTAGE_GENERIQUE.md).

## 2026-10-21 — Suppression du forcage horaire

A la demande utilisateur, retrait du panneau quotidien, de son execution dans AutomateManager et de sa configuration persistante. Voir [Suppression du forcage horaire](SUPPRESSION_FORCAGE_HORAIRE.md).

## 2026-10-21 — Catalogue capteurs reduit

Sur demande utilisateur, retrait des types de capteurs inutilises. Voir [Catalogue capteurs](CATALOGUE_CAPTEURS.md). Les mesures internes du routeur, MQTT et les actionneurs restent conserves.

## 2026-11-03 — Modulation SSR à trame variable

Ajout du mode optionnel `MULTI_SINUS` aux SSR locaux. La durée de trame dépend de la consigne et les fronts sont pilotés par un timer matériel au pas nominal de 10 ms. Les configurations historiques conservent leur sémantique. Voir le [guide et le bilan de validation](MULTI_SINUS.md).
