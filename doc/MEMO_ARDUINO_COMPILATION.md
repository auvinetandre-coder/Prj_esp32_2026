# Memo Arduino, partitions et fichiers

Procedure numerotee du 26 octobre 2026 : voir [INSTALLATION_PAR_PHASES.md](INSTALLATION_PAR_PHASES.md).
Les lanceurs `00_` a `04_` sont a la racine du projet. La phase 02 inclut maintenant
le bootloader et la selection factory, pour une carte neuve ou effacee.

## Relecture et portée — 20 octobre 2026

Marqueurs de sources Main et LittleFS au 4 novembre 2026 · `20261103-05`. La version installée reste à relever sur la carte. Les mesures d'anciens builds sont historiques. Voir les contrôles du [nouvel audit](AUDIT_2026-11-04.md#vérifications).

Pour une mise à jour utilisateur, conserver le passage explicite en factory recovery. Les scripts `Televerser_*_COM3/COM4.bat` écrivent directement par USB : ils relèvent de l'installation/maintenance et contournent les contrôles factory. Un transfert d'image LittleFS remplace la partition entière, y compris `/config`, pas seulement les fichiers Web ; sa sauvegarde/restauration actuelle reste conditionnelle. Voir [OTA.md](OTA.md) et l'[audit courant](AUDIT_2026-11-04.md).

Points d'outillage ouverts : `Detect_Arduino_Tools.bat` garde une variable `ARDUINO_CLI` déjà définie à un chemin invalide, empêchant son repli PATH. Les scripts habituels de compilation génèrent `build_info.h` ; les scripts de packaging/LittleFS peuvent modifier les marqueurs de version. Ne pas les lancer pour une simple vérification documentaire. `build_release.bat` et `publish_release.bat` existent ; leur vocabulaire/URLs GitHub historiques ne réintroduisent pas de téléchargement automatique par l'ESP.

### Vérifications locales sans appareil

Depuis la racine, avec Node disponible :

```powershell
node --check data/www/app.js
Get-ChildItem tools/test_*.js | ForEach-Object {
    & node $_.FullName
    if ($LASTEXITCODE -ne 0) { throw ('Echec du test : ' + $_.Name) }
}
```

Les 24 scripts `tools/test_*.js` passent au 4 novembre. Ils couvrent des fonctions WebUI et des blocs C++ extraits ou simulés. Le test de `/lite` vérifie le script embarqué. Les contrôles ne remplacent pas une compilation ni un essai ESP32.

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

### Menu toujours absent : correction du 18 octobre 2026

Sur ce poste, avec le core ESP32 `3.3.12`, l'option ajoutee dans `boards.local.txt` etait reconnue par Arduino CLI, mais restait absente du menu Arduino IDE, meme apres redemarrage et rechargement des donnees de la carte.

La correction appliquee a ete de sauvegarder `boards.txt`, puis d'ajouter directement ces lignes dans ce fichier :

```ini
esp32.menu.PartitionScheme.routeursolairerecovery=RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)
esp32.menu.PartitionScheme.routeursolairerecovery.build.partitions=partitions_recovery_main_4MB
esp32.menu.PartitionScheme.routeursolairerecovery.upload.maximum_size=1966080
```

Fichiers concernes sur ce poste :

```text
C:\Users\andre\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.12\boards.txt
C:\Users\andre\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.3.12\tools\partitions\partitions_recovery_main_4MB.csv
```

Sauvegarde creee avant modification : `boards.txt.routeursolaire-20261018-112602.bak`, dans le meme dossier que `boards.txt`.

Apres fermeture de toutes les fenetres Arduino IDE et reouverture, l'utilisateur a confirme que le choix etait visible. Selectionner `ESP32 Dev Module`, puis cette partition dans `Outils > Partition Scheme`, en fin de liste apres `Custom`.

Le script `Installer_Partition_Arduino.bat` ajoute actuellement les entrees dans `boards.local.txt` uniquement : il ne reproduit pas cette correction directe dans `boards.txt`. Une mise a jour ou reinstallation du core peut necessiter de refaire l'installation dans le dossier de la nouvelle version. Sauvegarder le fichier avant toute modification et ne pas dupliquer les entrees si elles existent deja.

Le choix du menu Arduino IDE ne change pas les commandes des `.bat` : les scripts imposent eux-memes leur schema de partitions. Installer le menu ne modifie pas la table de partitions presente sur l'ESP32.

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

Ils recompilent le code actuel du dossier projet (`RouteurSolaireESP32.ino` et les sources), puis envoient `build\esp32.esp32.esp32\RouteurSolaireESP32.ino.bin` a l'adresse `0x120000`. Ils ne televersent ni LittleFS ni la recovery.

Arduino IDE peut rester ferme : les scripts utilisent Arduino CLI. Fermer le moniteur serie pour liberer le port COM. La compilation peut rester silencieuse plusieurs minutes ; attendre le bilan de taille puis le message `Ecriture firmware principal dans ota_0 uniquement...`.

### Depannage USB COM4 : vitesse et boutons BOOT / EN

Le 18 octobre 2026, la compilation a reussi mais la connexion sur COM4 a echoue avec :

```text
Invalid head of packet (0x65): Possible serial noise or corruption.
```

La vitesse dans `tools\Televerser_Firmware_COM4.bat` a ete reduite de `921600` a `115200` bauds pour tenter de fiabiliser le transfert. Le succes d'un televersement apres cette modification n'a pas ete confirme dans cet echange. Cette modification concerne seulement ce script, pas les scripts COM3, LittleFS ou recovery.

En cas de `Wrong boot mode`, ou si la connexion n'aboutit pas :

1. Relancer `tools\Televerser_Firmware_COM4.bat`.
2. A l'apparition de `Connecting...`, maintenir `BOOT`.
3. Appuyer brievement sur `EN` / `RESET` en gardant `BOOT` appuye.
4. Relacher `BOOT` lorsque l'ecriture commence (`Writing...` ou progression).
5. Attendre `Firmware principal ota_0 televerse avec succes.`

Si l'erreur de communication persiste, essayer un autre cable USB de donnees et un port USB direct du PC. Les avertissements `Deprecated` concernant `default_reset`, `hard_reset` et `write_flash` ne sont pas l'erreur fatale de cet essai. Ne pas effacer la flash pour ce depannage.

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

## Images de la livraison MULTI_SINUS

Dans `build/ota`, le firmware `RouteurSolaireESP32_firmware_20261103-05.bin` contient le moteur SSR et `RouteurSolaireESP32_littlefs_20261103-05.bin` contient la WebUI associée. Les copies courantes conservent les noms `RouteurSolaireESP32_firmware.bin` et `RouteurSolaireESP32_littlefs.bin`. Voir le [bilan de livraison](MULTI_SINUS.md#livraison-du-3-novembre-2026).
