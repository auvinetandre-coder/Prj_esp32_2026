# RouteurSolaireESP32

Firmware Arduino IDE pour ESP32 permettant de realiser un routeur solaire photovoltaique local, modulaire, distribue et redondant.

Le principe du projet est de garder un seul firmware commun pour tous les ESP32. Le role de chaque module est choisi lors de l'installation depuis l'interface Web locale, puis stocke dans LittleFS.

## Avertissement securite 230 V

Ce projet peut piloter des equipements raccordes au secteur 230 V : chauffe-eau, SSR, triac RobotDyn, relais ou contacteur. Une erreur de cablage, d'isolation, de dissipation thermique ou de configuration peut provoquer electrocution, incendie, destruction de materiel ou declenchement dangereux.

Ne jamais intervenir sous tension. Utiliser des protections materielles adaptees : disjoncteur, differentiel, fusible, terre, boitier ferme, bornes isolees, dissipateur SSR, cables dimensionnes, separation basse tension/secteur et arret d'urgence. Le cablage 230 V doit etre realise ou verifie par une personne qualifiee.

Les securites logicielles du firmware ne remplacent jamais les securites materielles du chauffe-eau, du thermostat, du limiteur thermique, du tableau electrique ou du montage SSR/triac.

## Objectifs

- Router le surplus photovoltaique vers un chauffe-eau.
- Fonctionner entierement en local, sans Internet.
- Utiliser un firmware unique multi-role.
- Configurer dynamiquement les capteurs, actionneurs et regles.
- Supporter ESP-NOW pour des modules distribues.
- Gerer une redondance MASTER/BACKUP.
- Continuer a fonctionner sans WiFi maison grace au point d'acces local.
- Publier et recevoir des donnees MQTT vers Jeedom si l'option est activee.

## Architecture

```text
RouteurSolaireESP32/
|-- RouteurSolaireESP32.ino
|-- README.md
|-- WIRING.md
|-- data/
|   |-- README_LITTLEFS.txt
|   `-- www/
|       |-- index.html
|       |-- app.js
|       `-- style.css
|-- config_examples/
|   |-- device.example.json
|   |-- system.example.json
|   |-- sensors.example.json
|   |-- actuators.example.json
|   `-- rules.example.json
|-- tools/
|   |-- Compiler_RouteurSolaireESP32.bat
|   |-- Televerser_LittleFS_COM3.bat
|   `-- Effacer_Flash_ESP32.bat
`-- src/
    |-- actuators/
    |-- communication/
    |-- config/
    |-- logger/
    |-- logic/
    |-- network/
    |-- runtime/
    |-- safety/
    |-- sensors/
    |-- simulation/
    |-- status/
    `-- web/
```

Modules principaux :

- `ConfigManager` : fichiers JSON LittleFS.
- `SensorManager` : centralisation des capteurs.
- `JSYMK194TManager` : JSY-MK-194T Modbus RTU.
- `LinkyTICManager` : TIC Linky.
- `DS18B20Manager` : bus OneWire GPIO13 par defaut, configurable.
- `ActuatorManager` : SSR, RobotDyn, relais, PWM, sorties digitales.
- `RuleEngine` : moteur de regles SI / ALORS.
- `SafetyManager` : securites globales.
- `SimulationManager` : simulation sans capteurs ni charge 230 V.
- `EspNowManager` : communication ESP-NOW.
- `MqttManager` : publication et reception MQTT pour Jeedom.
- `RedundancyManager` : MASTER/BACKUP.
- `WebUi` : API REST et interface Web.
- `RuntimeState` : etat global.
- `Logger` : logs et evenements.
- `StatusLed` : LED GPIO2.

## Demarrage firmware

Au boot :

1. Initialisation serie 115200 bauds.
2. LED d'etat GPIO2.
3. Coupure de toutes les sorties.
4. Initialisation LittleFS.
5. Chargement ou creation des JSON de configuration.
6. Simulation forcee OFF au reboot.
7. Initialisation actionneurs.
8. Initialisation securites.
9. Initialisation capteurs.
10. Connexion WiFi ou AP fallback.
11. Initialisation ESP-NOW.
12. Initialisation redondance.
13. Demarrage interface Web.
14. Boucle principale non bloquante avec `millis()`.

## Roles ESP32

Le role est stocke dans `/config/device.json`.

- `MASTER` : controleur actif principal.
- `BACKUP` : surveille le MASTER et reprend si besoin.
- `NODE_SENSOR` : module capteurs distant.
- `NODE_ACTUATOR` : module actionneurs distant.
- `NODE_MIXED` : capteurs et actionneurs.

Tous les roles utilisent le meme firmware.

## Redondance MASTER/BACKUP

Le MASTER actif envoie des heartbeats via ESP-NOW. Le BACKUP surveille ces messages.

Valeurs par defaut :

- Heartbeat : 300 ms.
- Takeover : 1000 ms.
- `epoch` augmente a chaque reprise.

Etats possibles :

- `PASSIVE`
- `ACTIVE_MASTER`
- `BACKUP_READY`
- `TAKEOVER_PENDING`
- `TAKEOVER_ACTIVE`
- `SPLIT_BRAIN_DETECTED`

En cas de double MASTER ou split brain, le SafetyManager doit passer en etat critique.

## WiFi

WiFi maison :

```text
SSID: a renseigner dans l'interface Web
Mot de passe: a renseigner dans l'interface Web
```

Point d'acces fallback :

```text
SSID: configurable dans system.json ou l'interface Web
Mot de passe: configurable dans system.json ou l'interface Web
IP: 192.168.4.1
```

Le firmware tente le WiFi maison. En cas d'echec, il demarre un point d'acces local. L'interface Web reste disponible dans les deux cas.

Option disponible : garder l'AP local actif en meme temps que le WiFi maison.

## Interface Web

Interface principale :

```text
http://adresse-ip/app
```

Pages :

- Dashboard
- Capteurs
- Actionneurs
- Logique
- Diagnostic & Simulation
- Systeme
- MQTT
- Parametres

Interface de secours :

```text
http://adresse-ip/lite
```

API utiles :

- `/api/status-lite`
- `/api/status`
- `/api/diagnostic`
- `/api/sensors`
- `/api/actuators`
- `/api/rules`
- `/api/system`
- `/api/device`
- `/api/simulation`
- `/fs`

### Login Web

L'interface Web est protegee par une authentification HTTP Basic.

Identifiants par defaut :

```text
Utilisateur : admin
Mot de passe : routeur1234
```

Le navigateur affiche une fenetre de connexion au premier acces a `/`, `/app`, `/lite`, aux API ou aux pages OTA.

La configuration est stockee dans `/config/system.json` :

```json
{
  "webAuth": {
    "enabled": true,
    "username": "admin",
    "password": "routeur1234"
  }
}
```

Depuis la page **Parametres > Acces Web**, il est possible de :

- activer ou desactiver le login
- modifier l'utilisateur
- modifier le mot de passe

Si le champ mot de passe est laisse vide dans l'interface, le mot de passe actuel est conserve.

Attention : HTTP Basic protege l'acces local, mais ce n'est pas du HTTPS. Ne pas exposer l'ESP32 directement a Internet.

## MQTT / Jeedom

Le firmware integre un runtime MQTT base sur `PubSubClient`. Il reste inactif tant que MQTT n'est pas active dans la page **MQTT** ou dans `system.json`.

Configuration par defaut :

```text
Broker Jeedom: 192.168.0.48
Port: 1883
Topic de base: routeurSolaire
Publication: toutes les 5000 ms
```

Topics principaux par defaut :

```text
routeurSolaire/state
routeurSolaire/availability
routeurSolaire/command
routeurSolaire/actuator/+/set
```

`routeurSolaire/state` publie un JSON global avec les mesures importantes : puissance reseau, injection, surplus, consommation, tension, courant, securite, simulation, temperatures DS18B20 et commandes actionneurs.

Si les topics individuels sont actives, le firmware publie aussi des valeurs simples utiles pour Jeedom :

```text
routeurSolaire/gridPowerW
routeurSolaire/injectionW
routeurSolaire/consumptionW
routeurSolaire/surplusW
routeurSolaire/ssr1PowerPct
routeurSolaire/ssr2PowerPct
routeurSolaire/robotDynPowerPct
routeurSolaire/safetyLevel
routeurSolaire/ds18b20/sonde1/temperatureC
```

Commande JSON depuis Jeedom :

```json
{
  "actuatorId": "ssr1_water_heater",
  "command": "setActuatorPercent",
  "value": 25
}
```

Commande directe par topic :

```text
Topic: routeurSolaire/actuator/ssr1_water_heater/set
Payload: 25
```

En etat Safety CRITICAL, les commandes MQTT actionneurs sont refusees sauf les commandes d'arret.

## Dashboard

Le dashboard affiche :

- nom du module
- role ESP32
- WiFi, IP, RSSI
- etat securite
- etat simulation
- source puissance reseau
- puissance reseau
- injection
- surplus
- consommation
- SSR1, SSR2, RobotDyn
- temperatures sonde1/2/3
- informations ESP : firmware, core Arduino, IDF, CPU, flash, heap, LittleFS

Graphes :

- historique 30 minutes
- 1 point toutes les 5 secondes
- echelle visible
- tooltip au survol
- personnalisation des blocs et courbes

Graphe temps reel rapide :

- fenetre affichee : 10 dernieres secondes
- pas configurable : 100 a 500 ms
- valeur par defaut : 300 ms
- duree maximale active : 2 minutes
- desactive automatiquement apres redemarrage
- polling rapide arrete quand le graphe est desactive

Les donnees rapides viennent de l'API legere :

```text
GET /api/realtime
```

L'historique rapide reste cote navigateur. Il n'est pas stocke dans LittleFS et ne provoque pas d'ecriture flash.

## Regulation PID

Le pilotage chauffe-eau utilise un controleur PID separe du gestionnaire de capteurs.

Principe :

- `SensorManager` selectionne la source reseau JSY/TIC/AUTO.
- la puissance brute est stockee dans `gridPowerRawW`.
- un filtre alpha produit `gridPowerFilteredW`.
- `PIDController` lit uniquement cette puissance filtree.
- la sortie PID devient une commande 0 a 100 % pour SSR1.
- `ActuatorManager` applique la commande aux sorties physiques.
- `SafetyManager` reste prioritaire et force la commande a 0 % en cas de defaut critique.

Parametres principaux dans `system.json` :

```json
{
  "router": {
    "pidEnabled": true,
    "gridSetpointW": 0,
    "deadbandW": 30,
    "alphaFilter": 0.25,
    "maxOutputRampPercentPerSecond": 15,
    "heaterMaxPowerW": 1500,
    "jsyReadIntervalMs": 100,
    "kp": 0.08,
    "ki": 0.01,
    "kd": 0.0
  }
}
```

Cadences principales :

- JSY : 80 a 100 ms si configure
- SensorManager : 100 ms
- PIDController : 100 ms
- SafetyManager : 100 ms
- ActuatorManager : appele a chaque boucle, pilotage non bloquant

## Source puissance reseau

Le parametre `router.gridPowerSource` choisit la source officielle de `gridPowerW`.

Valeurs :

- `JSY` : puissance reseau issue du JSY-MK-194T.
- `TIC` : puissance reseau issue du Linky si exploitable.
- `AUTO` : TIC si disponible, sinon JSY.

Important : le JSY reste la source conseillee pour le pilotage rapide. La TIC historique donne souvent `PAPP`, qui est une puissance apparente positive. La TIC standard peut donner `SINSTS` et `SINSTI`, plus utiles pour distinguer soutirage et injection.

## Capteurs

Capteurs supportes :

- JSY-MK-194T
- TIC Linky
- DS18B20
- Entree analogique
- Entree digitale
- Capteur virtuel

### JSY-MK-194T

Configuration par defaut :

```text
Serial2
RX GPIO26
TX GPIO27
Adresse Modbus 1
4800 bauds
8N1
Registre 0x0048
```

Mesures :

- `voltageV`
- `currentA`
- `activePowerW`
- `gridPowerW`
- `injectionW`
- `consumptionW`
- `surplusW`
- `powerFactor`
- `frequencyHz`
- `available`

Deux pinces sont gerees :

- `activePowerW1`
- `voltageV1`
- `currentA1`
- `powerFactor1`
- `activePowerW2`
- `voltageV2`
- `currentA2`
- `powerFactor2`

Dans la page Capteurs, le JSY est un seul capteur avec deux voies :

- Pince 1
- Pince 2

Roles normalises des pinces :

- `grid`
- `production`
- `load`
- `custom`

Valeurs par defaut :

- Pince 1 : `grid`
- Pince 2 : `production`

### TIC Linky

Configuration par defaut :

```text
Serial1
RX GPIO26
TX GPIO27 renseigne dans la configuration, non utilise par la TIC
mode historique
1200 bauds
```

Mesures :

- `apparentPowerVA`
- `gridPowerW` si exploitable
- `currentA`
- `tariff`
- `period`
- `available`
- `lastValidReadMs`

La TIC sert surtout au diagnostic, a la coherence et aux informations energie. Elle n'est pas la meilleure source pour un pilotage rapide.

### DS18B20

Bus OneWire :

```text
GPIO13
Pull-up 4.7 kOhm vers 3.3 V
```

Les sondes sont generiques :

- `sonde1`
- `sonde2`
- `sonde3`

Le role reel vient de `sensors.json` ou de l'interface :

- `ballon_haut`
- `ballon_milieu`
- `ballon_bas`
- `depart_eau_chaude`
- `retour_eau_froide`
- `ambiance`
- `autre`

Chaque sonde peut etre critique ou non critique.

Le scan OneWire affiche les adresses detectees. Exemple :

```json
{
  "busGpio": 4,
  "count": 3,
  "addresses": [
    "28-FF-64-1E-85-16-03-5C",
    "28-3C-01-D6-45-12-04-A9",
    "28-A1-7B-91-20-19-02-6E"
  ]
}
```

Le scan ne sait pas quelle sonde est en haut/milieu/bas. Il faut associer chaque adresse a `sonde1`, `sonde2` ou `sonde3`.

## Actionneurs

Actionneurs par defaut :

- SSR1 chauffe-eau principal : GPIO5.
- SSR2 auxiliaire : GPIO17.
- RobotDyn Triac : zero-cross a renseigner selon PCB, controle GPIO33.

Types supportes :

- SSR
- RobotDyn Triac
- Relay
- PWM
- Digital Output
- Virtual

Modes supportes :

- `OFF`
- `ON_OFF`
- `BURST_FIRE`
- `TRAIN_ONDES_ENTIERES`
- `ZERO_CROSS_BURST`
- `LOW_FREQ_PWM`
- `PHASE_ANGLE`
- `MANUAL_SAFE`

L'interface filtre les modes selon le type choisi.

### Modes conseilles

SSR :

- `BURST_FIRE`
- `TRAIN_ONDES_ENTIERES`
- `ZERO_CROSS_BURST`
- `LOW_FREQ_PWM`
- `MANUAL_SAFE`

RobotDyn Triac :

- `PHASE_ANGLE`
- `ZERO_CROSS_BURST`
- `BURST_FIRE`
- `MANUAL_SAFE`

Relais :

- `ON_OFF`
- `MANUAL_SAFE`

Le mode `ON_OFF` est du tout ou rien. Il est adapte a un relais, une sortie digitale ou un contacteur. Il n'est pas adapte au routage progressif d'un chauffe-eau.

Le mode `PHASE_ANGLE` est reserve au RobotDyn/Triac avec detection zero-cross. Il permet un dosage fin mais peut generer davantage de parasites.

## Moteur de regles

Les regles sont stockees dans `/config/rules.json`.

Structure :

- `id`
- `name`
- `enabled`
- `priority`
- `logic`
- `conditions`
- `actions`

Logique :

- `AND` : toutes les conditions doivent etre vraies.
- `OR` : une seule condition vraie suffit.

La page Logique utilise des listes deroulantes pour eviter les erreurs :

- Source
- Mesure
- Operateur
- Valeur
- Unite
- Actionneur
- Commande

Les operateurs dependent du type :

- nombre : `>`, `>=`, `<`, `<=`, `==`, `!=`
- booleen : `==`, `!=`
- enum/texte : `==`, `!=`

Il n'y a pas encore de groupes complexes du type :

```text
(A ET B) OU (C ET D)
```

## Securites

Niveaux :

- `OK`
- `WARNING`
- `DEGRADED`
- `CRITICAL`

Causes possibles :

- temperature trop haute
- sonde critique absente
- JSY absent
- TIC absente
- incoherence capteur
- perte MASTER
- risque double MASTER
- erreur configuration
- arret manuel urgence

En `CRITICAL`, SSR1, SSR2 et RobotDyn doivent etre coupes.

Modes Safety disponibles dans les parametres :

- `strict`
- `warning_only`
- `missing_sensors_off`
- `off`

Le mode `off` est reserve aux tests sans charge 230 V.

## Simulation

Le mode simulation permet de tester le firmware sans capteurs physiques et sans activer les sorties 230 V.

Simule :

- JSY-MK-194T
- TIC Linky
- DS18B20
- commandes SSR1, SSR2, RobotDyn

Modes :

- `manual`
- `random`
- `scenario`

Scenarios :

- `normal`
- `production_low`
- `injection_medium`
- `injection_high`
- `tank_almost_hot`
- `tank_overheat`
- `critical_sensor_lost`
- `jsy_lost`

Securites simulation :

- aucune sortie 230 V reelle n'est activee en simulation
- les commandes sont calculees et visibles dans l'interface
- la simulation est forcee OFF au reboot
- la simulation s'arrete automatiquement apres 5 minutes
- le dashboard affiche le temps restant
- en sortie de simulation, les sorties sont forcees OFF pendant au moins 2 secondes

Les anciennes sorties LED de simulation ont ete supprimees. La simulation reste visible dans l'interface Web et sur l'ecran OLED.

## ESP-NOW

Messages supportes :

- `SENSOR_VALUE`
- `ACTUATOR_COMMAND`
- `HEARTBEAT`
- `CONFIG_SYNC`
- `STATUS`
- `ERROR`

Les messages incluent notamment :

- `sequenceNumber`
- `senderId`
- `role`
- `timestamp`
- checksum
- `masterId`
- `epoch`
- TTL pour les commandes actionneurs

Un `NODE_ACTUATOR` doit refuser une commande si :

- le MASTER n'est pas reconnu
- l'epoch est trop ancien
- le TTL est expire
- le SafetyManager est en `CRITICAL`

## Fichiers LittleFS

Fichiers de configuration sur l'ESP :

```text
/config/device.json
/config/system.json
/config/sensors.json
/config/actuators.json
/config/rules.json
```

Interface Web LittleFS :

```text
/www/index.html
/www/app.js
/www/style.css
```

Important : les fichiers `config_examples/*.example.json` sont des exemples PC. Les vraies configurations sont creees et modifiees dans LittleFS sur l'ESP.

## Installation Arduino IDE

Materiel par defaut :

- ESP32
- JSY-MK-194T
- TIC Linky
- 3 DS18B20
- SSR1 GPIO5
- SSR2 GPIO17
- RobotDyn zero-cross a renseigner / GPIO33
- LED etat GPIO2
- OLED SSD1309 SPI GPIO18/19/16/4/15

Bibliotheques Arduino :

- ArduinoJson
- OneWire
- DallasTemperature
- LittleFS ESP32
- WiFi ESP32
- WebServer ESP32
- ESP-NOW ESP32
- PubSubClient

Carte :

```text
ESP32 Dev Module
```

Partition conseillee :

```text
RouteurSolaire OTA (1.5MB APP x2 / 960KB LittleFS)
```

Cette partition permet :

- deux emplacements firmware OTA d'environ 1.5 Mo chacun
- environ 960 Ko pour LittleFS
- l'interface Web complete dans `/www`
- les fichiers JSON de configuration dans `/config`

Si le menu de partition n'apparait pas dans Arduino IDE :

1. Fermer Arduino IDE.
2. Relancer Arduino IDE.
3. Ouvrir le gestionnaire de cartes.
4. Laisser l'IDE recharger les donnees des cartes.
5. Revenir dans `Outils > Partition Scheme`.
6. Selectionner `RouteurSolaire OTA (1.5MB APP x2 / 960KB LittleFS)`.

Il peut y avoir un cache cote Arduino IDE. Si l'option n'apparait pas tout de suite, un redemarrage complet de l'IDE ou un rechargement des donnees de cartes peut etre necessaire.

Le fichier de partition du projet est :

```text
partitions_ota_1m5app_960k_littlefs.csv
```

Il contient :

```text
app0   0x10000   0x180000
app1   0x190000  0x180000
spiffs 0x310000  0x0F0000
```

Dans le core ESP32 Arduino, la partition est declaree sous le nom :

```text
partitions_ota_1m5app_960k_littlefs
```

## Compilation

Deux methodes sont possibles.

### Methode 1 : Arduino IDE

1. Ouvrir `RouteurSolaireESP32.ino`.
2. Choisir la carte :

```text
ESP32 Dev Module
```

3. Choisir le port COM.
4. Choisir le partitionnement :

```text
RouteurSolaire OTA (1.5MB APP x2 / 960KB LittleFS)
```

5. Compiler.
6. Televerser.

Cette methode met a jour le firmware, mais pas les fichiers Web LittleFS.

### Methode 2 : script de compilation

Script fourni :

```text
Compiler_RouteurSolaireESP32.bat
```

Ce script compile avec la bonne partition RouteurSolaire et genere le firmware dans le dossier de build Arduino.

Il est utile si Arduino IDE ne propose pas encore la partition dans le menu, ou si l'on veut eviter une erreur de selection de partition.

Apres compilation, le script copie automatiquement le firmware OTA ici :

```text
build/ota/RouteurSolaireESP32_firmware.bin
build/ota/RouteurSolaireESP32_firmware_YYYYMMDD-NN.bin
```

Le fichier sans date est toujours le dernier firmware compile. Le fichier avec version permet de conserver un historique.

Le format de version est :

```text
YYYYMMDD-NN
```

Exemple :

```text
RouteurSolaireESP32_firmware_20260509-01.bin
```

La version est aussi visible dans la page **Systeme**.

Le script genere aussi `src/build_info.h`. Ce fichier injecte dans le firmware :

- la version `YYYYMMDD-NN`
- la date de build
- le numero de build du jour
- un timestamp complet
- une signature embarquee dans le binaire

Signature embarquee :

```text
RS32_VERSION:YYYYMMDD-NN;
```

Cette signature permet a la page **Systeme** de relire la version presente dans APP1 et APP2, meme pour la partition OTA inactive.

Important : une partition qui contient un ancien firmware construit avant cette signature peut afficher `N/A`. Elle affichera sa version apres avoir recu un firmware genere avec ce systeme.

Dans la page **Systeme > OTA firmware**, on peut voir :

- la partition lancee actuellement : `app0` ou `app1`
- la partition configuree au boot
- la prochaine partition utilisee par l'OTA
- la version APP1 / OTA_0
- la version APP2 / OTA_1
- la taille du slot firmware
- la taille du firmware utilise
- le reste disponible

La page propose aussi un bouton **Rollback firmware**.

Ce bouton :

1. verifie l'autre partition OTA
2. refuse l'action si elle ne contient pas une image valide
3. configure l'autre partition comme partition de boot
4. redemarre l'ESP32

Exemple :

```text
ESP32 lance depuis app0
Rollback firmware -> prochain boot sur app1
```

Le rollback ne modifie pas LittleFS. L'interface et les fichiers `/config` restent donc ceux de la partition LittleFS actuelle.

## Televersement firmware

### Depuis Arduino IDE

1. Selectionner le port COM.
2. Cliquer Televerser.

Le firmware seul ne met pas a jour l'interface Web LittleFS.

### Depuis les scripts

Scripts fournis :

```text
Televerser_Firmware_COM3.bat
Televerser_Firmware_COM4.bat
```

Utiliser le script correspondant au port serie de l'ESP32.

Exemple si l'ESP32 est sur COM4 :

```text
Televerser_Firmware_COM4.bat
```

Ces scripts utilisent le firmware compile precedemment.

## Televersement LittleFS

Quand `data/www/app.js`, `data/www/style.css` ou `data/www/index.html` change, il faut televerser LittleFS.

Scripts fournis :

```text
Televerser_LittleFS_COM3.bat
Televerser_LittleFS_COM4.bat
```

Utiliser le script correspondant au port serie de l'ESP32.

Exemple si l'ESP32 est sur COM4 :

```text
Televerser_LittleFS_COM4.bat
```

Le script :

1. construit l'image LittleFS a partir du dossier `data`
2. cree le fichier OTA LittleFS dans `build/ota/RouteurSolaireESP32_littlefs.bin`
3. ecrit la version LittleFS dans `data/www/littlefs_version.txt`
4. cree aussi une copie versionnee `build/ota/RouteurSolaireESP32_littlefs_YYYYMMDD-NN.bin`
5. demande si l'on veut televerser maintenant sur le port COM
6. si la reponse est oui, l'ecrit dans la partition LittleFS a l'adresse `0x310000`
7. utilise une taille LittleFS de `0xF0000`
8. redemarre l'ESP32 en fin de televersement si possible

Apres televersement, ouvrir :

```text
http://192.168.4.1/app
```

ou l'IP donnee par le WiFi maison.

Si `/app` affiche une ancienne interface, une page blanche, ou `Not found: /www/index.html`, verifier :

1. que le bon schema de partition est selectionne
2. que LittleFS a bien ete televerse
3. que le port COM est le bon
4. que l'ESP32 a bien redemarre apres le televersement
5. que l'URL utilisee est `/app`

Page de secours :

```text
http://192.168.4.1/
http://192.168.4.1/lite
```

La page de secours permet aussi d'envoyer un firmware ou une image LittleFS par OTA quand le reseau fonctionne.

## OTA Web

La page de secours integre deux zones de mise a jour OTA :

- firmware
- LittleFS

Adresse :

```text
http://adresse-ip/
```

Routes OTA :

```text
POST /ota/firmware
POST /ota/littlefs
```

Procedure OTA firmware :

1. Compiler le firmware avec `Compiler_RouteurSolaireESP32.bat`.
2. Recuperer le fichier `.bin` genere dans `build/ota`.
3. Ouvrir la page de secours.
4. S'identifier si le navigateur demande le login.
5. Choisir le fichier firmware.
6. Envoyer.
7. Attendre le redemarrage.

Procedure OTA LittleFS :

1. Generer l'image LittleFS avec `Televerser_LittleFS_COM3.bat` ou `Televerser_LittleFS_COM4.bat`.
2. Ouvrir la page de secours.
3. S'identifier si le navigateur demande le login.
4. Choisir le fichier LittleFS `.bin`.
5. Envoyer.
6. Attendre le redemarrage.

### Conservation de la configuration pendant OTA LittleFS

L'OTA LittleFS remplace toute la partition LittleFS. Sans protection, les fichiers suivants pourraient etre perdus :

```text
/config/device.json
/config/system.json
/config/sensors.json
/config/actuators.json
/config/rules.json
```

Le firmware sauvegarde donc automatiquement ces fichiers dans la partition NVS avant de commencer l'ecriture OTA LittleFS.

Au redemarrage suivant, si une sauvegarde NVS est en attente, le `ConfigManager` restaure les fichiers `/config/*.json` avant de charger la configuration.

Cela permet de conserver :

- SSID WiFi maison
- mot de passe WiFi maison
- nom du module
- role du module
- login Web
- capteurs
- actionneurs
- regles
- parametres routeur solaire

Si la sauvegarde NVS echoue, l'OTA LittleFS est refusee pour eviter de perdre la configuration.

Ne pas couper l'alimentation pendant une mise a jour OTA.

## Resume des methodes de mise a jour

Firmware uniquement :

```text
Arduino IDE > Televerser
ou
Televerser_Firmware_COM3.bat / Televerser_Firmware_COM4.bat
ou
OTA firmware depuis la page de secours avec build/ota/RouteurSolaireESP32_firmware.bin
```

Interface Web uniquement :

```text
Televerser_LittleFS_COM3.bat / Televerser_LittleFS_COM4.bat
ou
OTA LittleFS depuis la page de secours avec build/ota/RouteurSolaireESP32_littlefs.bin
```

Firmware + interface Web :

```text
1. Televerser le firmware
2. Televerser LittleFS
3. Redemarrer l'ESP32
4. Ouvrir /app
```

Regle pratique :

- modification `.ino`, `.cpp`, `.h` : televerser le firmware
- modification `data/www/*` : televerser LittleFS
- modification des deux : televerser firmware puis LittleFS

## Premiere configuration

1. Flasher le firmware.
2. Televerser LittleFS.
3. Demarrer l'ESP32.
4. Se connecter au WiFi maison ou a l'AP fallback.
5. Ouvrir `/app`.
6. Aller dans Parametres.
7. Regler :
   - nom module
   - role
   - WiFi
   - source puissance reseau JSY/TIC/AUTO
   - Safety
   - simulation
8. Aller dans Capteurs.
9. Scanner DS18B20.
10. Associer les adresses aux sondes.
11. Configurer JSY et TIC.
12. Aller dans Actionneurs.
13. Verifier SSR1, SSR2, RobotDyn.
14. Aller dans Logique.
15. Verifier les regles.
16. Sauvegarder.
17. Redemarrer.

## Procedure de test recommandee

Sans 230 V :

1. Boot ESP32.
2. Verifier le port serie 115200.
3. Verifier WiFi/AP.
4. Ouvrir `/app`.
5. Activer simulation.
6. Tester `gridPowerW = -800`.
7. Verifier injection/surplus.
8. Verifier commande SSR calculee.
9. Verifier arret automatique simulation apres 5 minutes.
10. Verifier simulation OFF apres reboot.

Avec capteurs :

1. Scanner DS18B20.
2. Lire JSY.
3. Lire TIC.
4. Verifier source reseau.
5. Verifier Safety.

Avec sorties :

1. Tester LED de simulation.
2. Tester SSR sans charge 230 V.
3. Tester SSR avec charge resistive adaptee.
4. Tester RobotDyn en dernier.

## Diagnostic

Page Diagnostic & Simulation :

- securite
- raison dernier defaut
- uptime
- heap libre
- LittleFS
- WiFi
- ESP-NOW
- redondance
- JSY
- TIC
- DS18B20
- actionneurs
- evenements systeme

Evenements typiques :

- `BOOT`
- `CONFIG_LOADED`
- `WIFI_CONNECTED`
- `WIFI_FAIL_AP_MODE`
- `JSY_TIMEOUT`
- `TIC_TIMEOUT`
- `DS18B20_MISSING`
- `SAFETY_TRIGGERED`
- `SIMULATION_TIMEOUT`
- `MASTER_TAKEOVER`
- `SPLIT_BRAIN_DETECTED`
- `ACTUATOR_FORCED_OFF`
- `CONFIG_CHANGED`

## En cas de defaut

1. Ne pas forcer une sortie 230 V.
2. Lire la bannierre rouge/orange.
3. Aller dans Diagnostic & Simulation.
4. Verifier les evenements.
5. Verifier JSY/TIC/DS18B20.
6. Verifier la configuration Safety.
7. Corriger la cause.
8. Redemarrer si necessaire.

Si l'interface `/app` ne charge pas :

1. Tester `/lite`.
2. Tester `/api/status-lite`.
3. Tester `/fs`.
4. Televerser LittleFS a nouveau.
5. Ouvrir le moniteur serie.

## Cablage texte

```text
ESP32 GPIO2   -> LED etat

JSY-MK-194T:
  RX ESP32 GPIO26 <- TX JSY
  TX ESP32 GPIO27 -> RX JSY
  GND commun

TIC Linky:
  RX ESP32 GPIO26 <- interface TIC adaptee
  TX ESP32 GPIO27 reserve au meme connecteur, non utilise par la TIC
  GND commun selon interface

DS18B20:
  DATA GPIO13
  Pull-up 4.7 kOhm vers 3.3 V
  VCC 3.3 V
  GND

SSR1:
  commande GPIO5

SSR2:
  commande GPIO17

RobotDyn Triac:
  zero-cross a renseigner selon PCB
  gate/control GPIO33

OLED SSD1309 SPI:
  SCLK GPIO18
  SDA/MOSI GPIO19
  RES GPIO16
  DC GPIO4
  CS GPIO15
```

Respecter strictement la separation basse tension / secteur.

## Etat actuel

Dernieres compilations realisees avec succes. Le firmware occupe environ 90% de l'espace programme d'un ESP32 avec partition standard 1.3 Mo application.

Points restant a tester sur materiel reel :

- JSY deux pinces
- TIC historique/standard
- DS18B20 avec adresses reelles
- SSR reels
- RobotDyn reel
- ESP-NOW multi-modules
- redondance MASTER/BACKUP
- comportement Safety sur defaut reel
