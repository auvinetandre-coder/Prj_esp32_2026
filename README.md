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
- Prevoir des extensions futures comme MQTT.

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
- `DS18B20Manager` : bus OneWire GPIO4.
- `ActuatorManager` : SSR, RobotDyn, relais, PWM, sorties digitales.
- `RuleEngine` : moteur de regles SI / ALORS.
- `SafetyManager` : securites globales.
- `SimulationManager` : simulation sans capteurs ni charge 230 V.
- `EspNowManager` : communication ESP-NOW.
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

WiFi maison par defaut :

```text
SSID: WIFI_SSID_A_CONFIGURER
Mot de passe: WIFI_PASSWORD_A_CONFIGURER
```

Point d'acces fallback :

```text
SSID: AP_SSID_A_CONFIGURER
Mot de passe: AP_PASSWORD_A_CONFIGURER
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
RX GPIO16
TX GPIO17
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
- `currentA1`
- `activePowerW2`
- `currentA2`

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
RX GPIO32
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
GPIO4
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

- SSR1 chauffe-eau principal : GPIO26.
- SSR2 auxiliaire : GPIO25.
- RobotDyn Triac : zero-cross GPIO27, controle GPIO33.

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

Sorties LED de simulation prevues :

- SSR1 : GPIO18
- SSR2 : GPIO19
- RobotDyn : GPIO21

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
- SSR1 GPIO26
- SSR2 GPIO25
- RobotDyn GPIO27/GPIO33
- LED etat GPIO2

Bibliotheques Arduino :

- ArduinoJson
- OneWire
- DallasTemperature
- LittleFS ESP32
- WiFi ESP32
- WebServer ESP32
- ESP-NOW ESP32

Carte :

```text
ESP32 Dev Module
```

Partition conseillee : une partition avec LittleFS disponible.

## Compilation

Depuis Arduino IDE :

1. Ouvrir `RouteurSolaireESP32.ino`.
2. Choisir la carte ESP32.
3. Choisir le port COM.
4. Compiler.

Script fourni :

```text
Compiler_RouteurSolaireESP32.bat
```

## Televersement firmware

Depuis Arduino IDE :

1. Selectionner le port COM.
2. Cliquer Televerser.

Le firmware seul ne met pas a jour l'interface Web LittleFS.

## Televersement LittleFS

Quand `data/www/app.js`, `data/www/style.css` ou `data/www/index.html` change, il faut televerser LittleFS.

Script fourni pour COM3 :

```text
Televerser_LittleFS_COM3.bat
```

Apres televersement, ouvrir :

```text
http://192.168.4.1/app
```

ou l'IP donnee par le WiFi maison.

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
  RX ESP32 GPIO16 <- TX JSY
  TX ESP32 GPIO17 -> RX JSY
  GND commun

TIC Linky:
  RX ESP32 GPIO32 <- interface TIC adaptee
  GND commun selon interface

DS18B20:
  DATA GPIO4
  Pull-up 4.7 kOhm vers 3.3 V
  VCC 3.3 V
  GND

SSR1:
  commande GPIO26

SSR2:
  commande GPIO25

RobotDyn Triac:
  zero-cross GPIO27
  gate/control GPIO33

LED simulation:
  SSR1 GPIO18
  SSR2 GPIO19
  Triac GPIO21
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
