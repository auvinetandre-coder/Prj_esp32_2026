# RouteurSolaireESP32 — Partitionnement Recovery/Main ESP32

## 1. Objectif

Mettre en place un schéma de partitions ESP32 4 MB adapté au projet RouteurSolaireESP32.

Le firmware principal est devenu trop gros pour le schéma OTA classique `ota_0` / `ota_1`.
L’objectif est donc de remplacer le double slot OTA par une architecture :

```text
factory = firmware recovery minimal
ota_0   = firmware principal routeur solaire
spiffs  = partition utilisée par LittleFS
```

Le recovery sert uniquement à dépanner et mettre à jour le firmware principal en local.

---

## 2. État initial

Partitionnement précédent :

| Partition | Type | Offset | Taille | Usage |
|---|---|---:|---:|---|
| nvs | data/nvs | 0x9000 | 0x5000 = 20 Ko | configuration NVS |
| otadata | data/ota | 0xE000 | 0x2000 = 8 Ko | choix du slot OTA |
| app0 | app/ota_0 | 0x10000 | 0x180000 = 1 572 864 o | firmware slot 1 |
| app1 | app/ota_1 | 0x190000 | 0x180000 = 1 572 864 o | firmware slot 2 |
| spiffs | data/spiffs | 0x310000 | 0xF0000 = 983 040 o | utilisé comme LittleFS |

État firmware constaté :

```text
firmware principal : 1 565 280 o
slot firmware max  : 1 572 864 o
marge restante     : 7 584 o
```

Conclusion : le firmware principal est quasiment à la limite. Le schéma OTA classique n’est plus viable.

---

## 3. Nouvelle architecture validée

Décision retenue :

```text
factory recovery : firmware de secours minimal
ota_0 main       : firmware principal complet
LittleFS         : interface Web + configuration
```

Le recovery ne doit jamais être écrasé par une mise à jour du firmware principal.

Fonctionnement cible :

```text
Boot ESP32
→ factory recovery
→ attente courte / accès maintenance
→ bascule vers ota_0
→ firmware principal
→ le firmware principal reste sur ota_0 aux reboots normaux
→ le retour factory se fait seulement par demande recovery explicite
```

---

## 4. Nouveau schéma de partitions

Schéma actuellement retenu :

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x5000
otadata,    data, ota,     0xe000,   0x2000
factory,    app,  factory, 0x10000,  0x110000
ota_0,      app,  ota_0,   0x120000, 0x1E0000
spiffs,     data, spiffs,  0x300000, 0x100000
```

Tailles :

| Partition | Taille hex | Taille décimale | Rôle |
|---|---:|---:|---|
| factory | 0x110000 | 1 114 112 o | recovery |
| ota_0 | 0x1E0000 | 1 966 080 o | firmware principal |
| spiffs / LittleFS | 0x100000 | 1 048 576 o | fichiers Web/config |

Vérification fin de flash :

```text
0x300000 + 0x100000 = 0x400000
```

Donc le schéma remplit bien une flash ESP32 de 4 MB.

---

## 5. État après modification

Recovery allégé :

```text
Recovery factory : 948 884 / 1 310 720 o = 72 %
Binaire généré   : 949 040 o
Marge factory    : environ 361 Ko avant dernier redécoupage
```

Avec le nouveau redécoupage `factory = 0x110000` :

```text
factory max      : 1 114 112 o
recovery actuel  : environ 949 040 o
marge recovery   : environ 165 Ko
```

Firmware principal :

```text
ota_0 max        : 1 966 080 o
main actuel      : environ 1 566 463 o
marge main       : environ 399 Ko
```

LittleFS :

```text
partition LittleFS : 1 048 576 o
dossier data réel  : environ 397 968 o
occupation réelle  : environ 38 %
```

Conclusion : LittleFS n’est pas le point bloquant actuellement.

---

## 6. Recovery minimal retenu

Le recovery doit rester très simple.

À conserver :

```text
- WiFi local si possible
- AP de secours RouteurSolaire_Recovery
- page Web locale très simple
- upload local d’un firmware.bin
- écriture uniquement dans ota_0
- bouton démarrer firmware principal
- logs simples
```

À supprimer / ne pas remettre :

```text
- GitHub
- HTTPS
- HTTPClient si inutile
- WiFiClientSecure
- SHA256 / mbedtls si uniquement pour GitHub
- version.json
- certificats
- dashboard complet
- CSS/JS lourd
- logique métier routeur solaire
- ESP-NOW
- capteurs
- PID
- MQTT
- AutomateManager
- logique métier principale
```

Règle :

```text
Recovery = coffre-fort local.
Main = application complète.
```

---

## 7. WiFi du recovery

Objectif : pouvoir accéder au recovery depuis le réseau local, mais ne jamais se bloquer dehors.

Fonctionnement recommandé :

```text
Mode WiFi : AP_STA

1. AP toujours actif : RouteurSolaire_Recovery
2. tentative de connexion au WiFi local si identifiants disponibles
3. si WiFi local OK : afficher l’IP locale
4. si WiFi local KO : rester accessible via l’AP recovery
```

Pour garder le recovery léger :

```text
- préférer Preferences / NVS pour stocker SSID et mot de passe recovery
- éviter de charger ConfigManager complet
- éviter ArduinoJson uniquement pour lire /config/system.json
```

Page recovery minimale :

```text
- état WiFi local
- IP locale si connecté
- IP AP
- formulaire SSID / mot de passe
- formulaire upload firmware.bin
- bouton démarrer firmware principal
```

---

## 8. Modifications Arduino IDE / boards.local.txt

L’installateur Arduino doit déclarer le nouveau menu de partitions.

Nom affiché souhaité :

```text
RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)
```

Identifiant utilisé :

```text
routeursolairerecovery
```

Points à vérifier :

```text
build.partitions=partitions_recovery_main_4MB
upload.maximum_size=1966080
```

Le fichier CSV doit être copié au bon endroit dans le core Arduino ESP32, par exemple :

```text
.../packages/esp32/hardware/esp32/<version>/tools/partitions/partitions_recovery_main_4MB.csv
```

Le fichier généré dans le dossier build doit correspondre exactement au CSV source :

```text
build/esp32.esp32.esp32/partitions.csv
```

---

## 9. Fichiers à maintenir

Fichiers importants côté projet :

```text
partitions_recovery_main_4MB.csv
boards.local.txt ou script d’installation Arduino
tools/Compiler_Recovery.bat
tools/Compiler_Main.bat
recovery/recovery.ino ou équivalent
src/boot_manager.* si présent
src/boot/boot_manager.* si présent
src/logic/automate_manager.* si présent
```

À vérifier après chaque modification :

```text
- le CSV source
- le CSV réellement utilisé dans build
- upload.maximum_size
- taille du firmware main
- taille du recovery
- taille réelle du dossier data
```

---

## 10. Boot manager

Fonctions attendues :

```cpp
bootToFactory();
bootToMain();
setNextBootFactory();
setNextBootMain();
getRunningPartitionInfo();
```

Règles :

```text
- le recovery peut basculer vers ota_0
- le main doit pouvoir selectionner factory uniquement pour un passage recovery explicite
- le recovery ne doit jamais écrire dans factory
- l’update firmware écrit uniquement dans ota_0
```

Cycle cible :

```text
1. boot sur factory
2. accès recovery possible
3. bascule vers ota_0
4. main démarre
5. les reboots normaux du main restent sur ota_0
6. le retour factory se fait seulement sur demande recovery explicite
```

---

## 11. Update firmware depuis recovery

L’update depuis recovery se fait uniquement en local.

Workflow :

```text
1. ouvrir la page recovery
2. choisir firmware.bin
3. vérifier taille firmware <= taille ota_0
4. écrire dans ota_0
5. Update.end()
6. sélectionner ota_0 comme prochain boot
7. redémarrer
```

Sécurités obligatoires :

```text
- ne jamais écrire dans factory
- ne jamais effacer LittleFS
- ne jamais effacer NVS
- refuser un firmware trop gros
- conserver le recovery accessible en cas d’échec
```

---

## 12. LittleFS

Même si la partition s’appelle `spiffs`, le projet peut continuer à utiliser LittleFS.

Partition actuelle :

```text
spiffs / LittleFS : 0x100000 = 1 Mo
```

Taille réelle constatée :

```text
dossier data : environ 388 Ko
```

Conclusion :

```text
LittleFS a de la marge.
Ne pas réduire LittleFS maintenant, sauf besoin futur.
```

À surveiller :

```text
- index.html
- app.js
- style.css
- fichiers inutilisés
- anciennes versions
- assets inutiles
```

---

## 13. Points de vigilance firmware principal

Même avec le nouveau partitionnement, le firmware principal doit être surveillé.

Candidats à audit :

```text
- GitHub / HTTPS / WiFiClientSecure
- MQTT si non utilisé
- simulation
- logs très verbeux
- anciennes API en doublon
- compatibilité anciens formats JSON
- pages Web encore embarquées en PROGMEM
- modules capteurs/actionneurs non utilisés
- debug ESP-NOW
```

Ne rien supprimer directement sans rapport préalable.

Demander d’abord un audit :

```text
Lister les modules supprimables ou rendables optionnels.
Pour chaque élément : fichier, fonction, gain estimé, risque, proposition.
```

---

## 14. Décisions figées

Décisions validées à ce stade :

```text
- abandon du double OTA classique ota_0 / ota_1
- conservation de otadata
- conservation de NVS
- conservation de LittleFS
- recovery local uniquement
- suppression GitHub/HTTPS du recovery
- update local par fichier .bin
- recovery en factory
- firmware principal en ota_0
```

À ne pas refaire sans raison :

```text
- réintroduire GitHub dans le recovery
- grossir la page recovery
- mettre de la logique métier dans recovery
- réduire LittleFS sans mesurer data/
- modifier encore les partitions avant validation complète du cycle boot/update
```

---

## 15. Checklist de validation

Après modification, valider :

```text
[ ] le projet compile avec le nouveau menu Arduino
[ ] build.partitions pointe vers le bon CSV
[ ] build/esp32.esp32.esp32/partitions.csv est correct
[ ] upload.maximum_size main = 1 966 080
[ ] recovery rentre dans factory 0x110000
[ ] main rentre dans ota_0 0x1E0000
[ ] LittleFS monte correctement
[ ] recovery démarre
[ ] AP RouteurSolaire_Recovery visible
[ ] recovery peut se connecter au WiFi local si configuré
[ ] page recovery accessible
[ ] bouton boot main fonctionne
[ ] main démarre
[ ] reboot normal du main reste sur ota_0
[ ] passage recovery explicite depuis le main selectionne factory
[ ] upload local firmware.bin fonctionne
[ ] update écrit uniquement ota_0
[ ] factory reste intact après update
```

---

## 16. Cadrage court pour Codex

```text
Ne modifie plus l’architecture globale.
Stabilise le partitionnement Recovery/Main ESP32 4MB.

Schéma retenu :
- factory 0x110000
- ota_0 0x1E0000
- LittleFS/spiffs 0x100000

Recovery minimal uniquement :
- AP secours
- WiFi local si possible
- page Web simple
- upload local .bin vers ota_0
- bouton boot main

Ne pas remettre GitHub, HTTPS, HTTPClient, WiFiClientSecure, SHA256, dashboard, ESP-NOW, PID ou logique métier dans recovery.

Vérifier les tailles, le CSV Arduino, upload.maximum_size, et le cycle complet boot factory → main ota_0 → reboot normal ota_0, avec passage recovery explicite vers factory.
```
