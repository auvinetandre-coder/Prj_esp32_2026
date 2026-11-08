# OTA.md - Recovery/Main local

## Statut de maintenance — 4 novembre 2026

Ce document décrit l'architecture retenue et les exigences. La préservation de `/config` reste un **objectif non garanti** par le code actuel : effacement complet de LittleFS lors de l'upload, backup NVS conditionnel/non actualisé après chaque fichier, restauration non transactionnelle et formatage implicite au montage dans le main. Les écritures factory n'ont pas d'authentification et l'image LittleFS tronquée n'est pas suffisamment validée. Voir l'[audit courant, constats A1 et A2](AUDIT_2026-11-04.md#corrections-proposées).

La page `/recovery` du main est embarquée et ne dépend pas de LittleFS. `POST /api/system/recovery` demande explicitement le prochain boot factory ; le reboot normal sélectionne `ota_0`. En factory, les routes d'upload sont `/upload` pour le firmware et `/upload-fs` pour LittleFS. La présence de ces routes ne valide pas leur robustesse aux interruptions.

Ne pas présenter un remplacement d'image LittleFS comme une simple copie de WebUI : la partition contient aussi les cinq JSON. Prévoir leur export indépendant, vérifier leur intégrité après restauration et traiter un échec de backup avant toute opération d'effacement. Aucun upload ni test sur appareil n'a été effectué pendant cette relecture ; la checklist ci-dessous reste à réaliser.

Les scripts USB COM3/COM4 sont des outils d'installation/maintenance manuelle qui écrivent directement les partitions. Ils ne passent pas par les contrôles de la factory et ne garantissent pas davantage la conservation des fichiers. Le parcours de mise à jour utilisateur retenu reste le passage explicite en recovery.

## Objectif

La strategie OTA validee n'est plus le double slot classique `ota_0` / `ota_1`.

Le projet utilise maintenant une architecture Recovery/Main pour ESP32 4 MB :

```text
factory = recovery minimal
ota_0   = firmware principal
spiffs  = partition utilisee par LittleFS
```

Le recovery est le point de maintenance local. Le firmware principal reste l'application complete du routeur solaire.

---

## Regles absolues

- Pas d'auto-update silencieux.
- Action utilisateur obligatoire.
- Le recovery ne doit jamais etre ecrase par une mise a jour du main.
- L'update firmware ecrit uniquement dans `ota_0`.
- LittleFS reste separe du firmware.
- Ne jamais ecraser `/config`.
- Ne jamais effacer NVS.
- Erreurs explicites et logs exploitables.
- Ne pas remettre GitHub, HTTPS, `WiFiClientSecure` ou logique metier dans le recovery.

---

## Partitionnement valide

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x5000
otadata,    data, ota,     0xe000,   0x2000
factory,    app,  factory, 0x10000,  0x110000
ota_0,      app,  ota_0,   0x120000, 0x1E0000
spiffs,     data, spiffs,  0x300000, 0x100000
```

Tailles :

- `factory` : 1 114 112 octets pour le recovery.
- `ota_0` : 1 966 080 octets pour le main.
- `spiffs` / LittleFS : 1 048 576 octets pour WebUI et configuration.

---

## Recovery

Le recovery doit rester minimal et local-first.

Autorise :

- AP de secours `RouteurSolaire_Recovery`.
- Wi-Fi local si les identifiants sont disponibles.
- Page Web locale simple.
- Upload local de `firmware.bin`.
- Ecriture du firmware uniquement dans `ota_0`.
- Upload local de l'image LittleFS si la fonction est conservee.
- Bouton pour demarrer le firmware principal.
- Logs simples.

Interdit dans le recovery :

- ESP-NOW.
- MQTT.
- Dashboard complet.
- PID.
- capteurs et actionneurs.
- AutomateManager.
- GitHub OTA.
- HTTPS.
- `HTTPClient`, `WiFiClientSecure`, certificats et SHA256 si uniquement utiles a GitHub.
- logique metier routeur solaire.

Regle courte :

```text
Recovery = coffre-fort local.
Main = application complete.
```

---

## BootManager

`BootManager` porte la logique de selection de partition.

Fonctions attendues ou presentes :

```cpp
bootToFactory();
bootToMain();
setNextBootFactory();
setNextBootMain();
getRunningPartitionInfo();
getBootPartitionInfo();
getMainPartitionInfo();
```

Cycle cible valide :

```text
1. l'ESP32 boote sur factory si la partition factory est selectionnee
2. le recovery laisse une fenetre de maintenance
3. le recovery bascule vers ota_0 sur action utilisateur ou expiration de la fenetre
4. le firmware principal demarre
5. les reboots normaux restent sur ota_0
6. le retour recovery se fait uniquement par demande explicite du main
```

Contraintes :

- le recovery peut selectionner `ota_0` comme prochain boot ;
- le main doit pouvoir selectionner `factory` comme prochain boot uniquement pour une demande recovery explicite ;
- le recovery ne doit jamais ecrire dans `factory` ;
- l'update firmware ecrit uniquement dans `ota_0`.

---

## AutomateManager

`AutomateManager` appartient au firmware principal.

Role cible :

- orchestrer les automatismes applicatifs ;
- centraliser les sequences non bloquantes ;
- declencher les actions a partir de l'etat runtime, des regles et des securites ;
- rester separe du recovery.

Regles :

- aucune dependance du recovery vers `AutomateManager` ;
- pas d'automatisme bloquant ;
- SafetyManager garde la priorite sur toute action automatique ;
- les automatismes doivent rester observables via logs, etat runtime ou API.

---

## Firmware principal

Le main contient l'application complete :

- ConfigManager ;
- RuntimeState ;
- SensorManager ;
- ActuatorManager ;
- AutomateManager ;
- RuleEngine ;
- PIDController ;
- SafetyManager ;
- SimulationManager ;
- EspNowManager ;
- RedundancyManager ;
- MqttManager si active ;
- WebUi ;
- BootManager ;
- logs et diagnostics.

Le main doit surveiller sa taille et rester sous `0x1E0000`.

---

## LittleFS

LittleFS contient la WebUI et les fichiers de configuration.

A preserver :

```text
/config/device.json
/config/system.json
/config/sensors.json
/config/actuators.json
/config/rules.json
```

Si la partition s'appelle `spiffs` dans le CSV, ne pas la renommer sans analyse : elle est utilisee comme partition LittleFS.

---

## Livraison MULTI_SINUS du 3 novembre 2026

Le firmware `20261103-05` et LittleFS `20261103-05` sont disponibles séparément dans `build/ota`. Installer les deux via le Recovery pour disposer du moteur SSR et de son sélecteur WebUI. Les noms des images et les vérifications de livraison figurent dans le [guide MULTI_SINUS](MULTI_SINUS.md#livraison-du-3-novembre-2026).

## GitHub Releases

Decision actuelle : GitHub OTA est abandonne pour l'architecture validee.

GitHub peut rester utile pour publier des fichiers `.bin` comme artefacts de release, mais l'ESP32 ne doit pas telecharger ni appliquer automatiquement une mise a jour depuis GitHub dans le recovery valide.

Toute reintroduction de GitHub OTA doit faire l'objet d'une nouvelle decision documentee, avec impact taille firmware, securite, rollback et UX.

---

## Checklist de validation

```text
[ ] le menu Arduino Recovery/Main est selectionne
[ ] le CSV source correspond au CSV de build
[ ] upload.maximum_size main = 1 966 080
[ ] recovery rentre dans factory 0x110000
[ ] main rentre dans ota_0 0x1E0000
[ ] LittleFS monte correctement
[ ] AP RouteurSolaire_Recovery visible
[ ] recovery accessible en local
[ ] upload firmware.bin fonctionne depuis recovery
[ ] update firmware ecrit uniquement ota_0
[ ] main demarre apres update
[ ] reboot normal du main reste sur ota_0
[ ] passage recovery explicite depuis le main selectionne factory
[ ] factory reste intact
[ ] /config reste intact
```
