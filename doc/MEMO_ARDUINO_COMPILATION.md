# Memo Arduino, partitions et fichiers

Ce fichier est le point de reference rapide pour eviter de rechercher a chaque fois la configuration Arduino, le schema de partitions, les scripts, les noms de fichiers et les emplacements.

## Dossier projet

Racine du projet :

```text
C:\ARDUINO\V2_PCB\RouteurSolaireESP32
```

Sketch principal :

```text
RouteurSolaireESP32.ino
```

Interface Web LittleFS :

```text
data\www\index.html
data\www\app.js
data\www\style.css
data\www\littlefs_version.txt
```

Documentation projet :

```text
doc\
```

Scripts Windows :

```text
tools\
```

## Architecture memoire ESP32 4 Mo

Le projet utilise une architecture Recovery/Main, pas un double OTA classique.

```text
factory = recovery minimal
ota_0   = firmware principal
spiffs  = partition utilisee par LittleFS
```

Fichier de partitions du projet :

```text
partitions_recovery_main_4MB.csv
```

Contenu important :

```csv
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
factory,    app,  factory, 0x10000,  0x110000,
ota_0,      app,  ota_0,   0x120000, 0x1E0000,
spiffs,     data, spiffs,  0x300000, 0x100000,
```

Tailles :

```text
factory recovery : 0x110000 = 1.06 Mo environ
main ota_0       : 0x1E0000 = 1.88 Mo environ
LittleFS spiffs  : 0x100000 = 1 Mo
```

Regles :

- Le recovery reste dans `factory`.
- Le firmware principal va dans `ota_0`.
- LittleFS va dans `spiffs` a l'adresse `0x300000`.
- Ne pas renommer `spiffs` sans analyse : il est utilise comme partition LittleFS.
- Ne pas ecraser `/config` volontairement sans sauvegarde.

## Configuration Arduino IDE

Carte :

```text
ESP32 Dev Module
```

FQBN utilise par les scripts :

```text
esp32:esp32:esp32
```

Partition Scheme attendu dans Arduino IDE :

```text
RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)
```

Si l'option n'apparait pas dans Arduino IDE :

1. Fermer Arduino IDE.
2. Lancer :

```bat
tools\Installer_Partition_Arduino.bat
```

3. Rouvrir Arduino IDE.
4. Aller dans `Outils > Partition Scheme`.
5. Choisir `RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)`.

Le script de compilation copie aussi automatiquement `partitions_recovery_main_4MB.csv` dans le dossier partitions du core ESP32 Arduino.

## Compiler le firmware principal

Script recommande :

```bat
tools\Compiler_RouteurSolaireESP32.bat
```

Ce script :

- detecte Arduino CLI et les outils ESP32 ;
- copie la partition custom dans le core ESP32 ;
- genere le build info ;
- compile avec `PartitionScheme=custom` ;
- limite le firmware principal a `1966080` octets ;
- copie le `.bin` dans `build\ota`.

Sorties :

```text
build\ota\RouteurSolaireESP32_firmware.bin
build\ota\RouteurSolaireESP32_firmware_YYYYMMDD-NN.bin
```

Le firmware principal doit tenir dans :

```text
ota_0 : 1966080 octets
```

## Televerser le firmware principal

Scripts directs selon le port :

```bat
tools\Televerser_Firmware_COM3.bat
tools\Televerser_Firmware_COM4.bat
```

Ces scripts televersent le firmware principal vers `ota_0`.

Alternative recommandee si le recovery est deja en place :

1. Compiler avec `tools\Compiler_RouteurSolaireESP32.bat`.
2. Ouvrir la page recovery de l'ESP.
3. Envoyer :

```text
build\ota\RouteurSolaireESP32_firmware.bin
```

4. Verifier que l'upload cible `ota_0`.
5. Demarrer `ota_0` depuis la recovery.

## Compiler ou generer LittleFS

LittleFS contient la Web UI et les fichiers embarques de `data`.

Quand un fichier suivant change, il faut regenerer et televerser LittleFS :

```text
data\www\app.js
data\www\index.html
data\www\style.css
data\www\littlefs_version.txt
data\README_LITTLEFS.txt
```

Scripts selon le port :

```bat
tools\Televerser_LittleFS_COM3.bat
tools\Televerser_LittleFS_COM4.bat
```

Ces scripts :

- ecrivent la version dans `data\www\littlefs_version.txt` ;
- creent l'image LittleFS depuis `data` ;
- creent `build\ota\RouteurSolaireESP32_littlefs.bin` ;
- creent une copie horodatee ;
- demandent si on veut televerser immediatement.

Sorties :

```text
build\ota\RouteurSolaireESP32_littlefs.bin
build\ota\RouteurSolaireESP32_littlefs_YYYYMMDD-NN.bin
build\ota\latest_littlefs_version.txt
```

Adresse et taille LittleFS :

```text
offset : 0x300000
size   : 0x100000
```

## Televerser LittleFS

Direct USB :

```bat
tools\Televerser_LittleFS_COM3.bat
tools\Televerser_LittleFS_COM4.bat
```

Le script pose la question :

```text
Veux-tu aussi televerser maintenant sur COMx ? [O/N]
```

Repondre `O` pour envoyer sur l'ESP branche.

Via recovery :

1. Generer l'image avec un des scripts LittleFS.
2. Ouvrir la page recovery.
3. Choisir :

```text
build\ota\RouteurSolaireESP32_littlefs.bin
```

4. Uploader l'image LittleFS.
5. Redemarrer vers `ota_0`.

Attention : ecrire LittleFS remplace la partition `spiffs`. Les fichiers `/config/*.json` sont aussi dans LittleFS sur l'ESP. Si l'image ne contient pas la configuration voulue, il faudra reconfigurer ou restaurer.

## Compiler le recovery

Script :

```bat
tools\Compiler_Recovery.bat
```

Le recovery est minimal et doit rester dans `factory`.

Il ne doit pas contenir :

- ESP-NOW ;
- MQTT ;
- dashboard complet ;
- PID ;
- logique metier ;
- GitHub OTA.

## Televerser le recovery

Scripts selon le port :

```bat
tools\Televerser_Recovery_COM3.bat
tools\Televerser_Recovery_COM4.bat
```

Ces scripts concernent la partition `factory`.

Ne pas utiliser ces scripts pour mettre a jour le firmware principal.

## Ports COM habituels

Scripts disponibles :

```text
COM3 :
tools\Televerser_Firmware_COM3.bat
tools\Televerser_LittleFS_COM3.bat
tools\Televerser_Recovery_COM3.bat

COM4 :
tools\Televerser_Firmware_COM4.bat
tools\Televerser_LittleFS_COM4.bat
tools\Televerser_Recovery_COM4.bat
```

Pour voir les cartes branchees depuis Arduino CLI :

```bat
arduino-cli board list
```

Dans ce projet, preferer les scripts deja prepares plutot que de retaper les commandes longues.

## Nommage des fichiers generes

Firmware principal :

```text
RouteurSolaireESP32_firmware.bin
RouteurSolaireESP32_firmware_YYYYMMDD-NN.bin
```

LittleFS :

```text
RouteurSolaireESP32_littlefs.bin
RouteurSolaireESP32_littlefs_YYYYMMDD-NN.bin
```

Release locale :

```text
build\release\firmware.bin
build\release\littlefs.bin
build\release\version.json
```

Version courante :

```text
build\ota\latest_version.txt
build\ota\latest_littlefs_version.txt
data\www\littlefs_version.txt
src\build_info.h
```

## Dossiers de build

Build Arduino :

```text
build\esp32.esp32.esp32
```

Fichiers prets pour upload local ou recovery :

```text
build\ota
```

Fichiers de release locale :

```text
build\release
```

## Quel fichier televerser selon le changement

Code C++ ou `.ino` modifie :

```text
compiler firmware
televerser firmware principal
```

Fichier `data\www\*` modifie :

```text
generer LittleFS
televerser LittleFS
```

Code C++ et Web UI modifies :

```text
1. compiler et televerser firmware
2. generer et televerser LittleFS
```

Recovery modifie :

```text
compiler recovery
televerser recovery vers factory
```

Partition CSV modifiee :

```text
reinstaller partition Arduino
recompiler firmware
recompiler recovery si necessaire
verifier les tailles
```

## Verification rapide apres flash

Dans la Web UI :

```text
/app
Page Systeme
```

Verifier :

- nom ESP ;
- role redondance ;
- role fonctionnel ;
- version firmware ;
- version LittleFS ;
- partition active ;
- LittleFS OK.

Endpoints utiles :

```text
/api/status-lite
/api/state
/api/system-info
/api/espnow
/api/fs
```

Si la Web UI ne change pas apres modification de `app.js`, c'est presque toujours que LittleFS n'a pas ete reteleverse ou que le navigateur garde un cache. Regenerer puis televerser LittleFS, puis faire un refresh force de `/app`.

## Regle courte a retenir

```text
Firmware = build\ota\RouteurSolaireESP32_firmware.bin -> ota_0
LittleFS = build\ota\RouteurSolaireESP32_littlefs.bin -> spiffs @ 0x300000
Recovery = factory
Partition = partitions_recovery_main_4MB.csv
Web UI = data\www\
Config ESP = /config/*.json dans LittleFS
```
