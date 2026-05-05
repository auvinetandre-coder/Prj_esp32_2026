# RouteurSolaireESP32

Firmware Arduino IDE pour routeur solaire photovoltaique local, modulaire, distribue et redondant sur ESP32.

Le principe du projet est simple : tous les ESP32 utilisent exactement le meme firmware. Le role reel de chaque module est configure au moment de l'installation depuis l'interface Web locale, puis stocke dans LittleFS.

## Avertissement securite 230 V

Ce projet pilote des equipements pouvant etre raccordes au secteur 230 V, a un chauffe-eau, a des SSR et a un triac RobotDyn. Ces montages peuvent provoquer electrocution, incendie, destruction materielle ou declenchements dangereux si le cablage, l'isolation, la dissipation thermique ou les protections sont incorrects.

Ne jamais intervenir sous tension. Utiliser des protections adaptees : disjoncteur, differentiel, fusible, terre, boitier ferme, bornes isolees, dissipateur SSR, cables dimensionnes, separation basse tension/secteur et arret d'urgence. Le cablage 230 V doit etre realise ou verifie par une personne qualifiee. Tester d'abord sans charge secteur, puis avec une charge resistive de test adaptee.

Le firmware inclut des securites logicielles, mais elles ne remplacent jamais les securites materielles du chauffe-eau, du tableau electrique, du thermostat, du limiteur thermique ou du montage SSR/triac.

## Objectifs

- Piloter un chauffe-eau avec le surplus photovoltaique.
- Fonctionner entierement en local, sans dependance Internet.
- Utiliser un firmware unique pour tous les ESP32.
- Configurer dynamiquement les capteurs, actionneurs et regles.
- Supporter ESP-NOW pour un systeme distribue.
- Assurer une redondance MASTER/BACKUP avec reprise automatique.
- Continuer a fonctionner sans WiFi maison grace au fallback point d'acces.
- Prevoir une extension future, par exemple MQTT.

## Architecture generale

Le projet est organise en modules Arduino `.h/.cpp` :

```text
RouteurSolaireESP32/
+-- RouteurSolaireESP32.ino
+-- README.md
+-- WIRING.md
+-- data/
|   +-- README_LITTLEFS.txt
|   +-- www/
|       +-- index.html
|       +-- app.js
|       +-- style.css
+-- config_examples/
|   +-- device.example.json
|   +-- system.example.json
|   +-- sensors.example.json
|   +-- actuators.example.json
|   +-- rules.example.json
+-- tools/
|   +-- Compiler_RouteurSolaireESP32.bat
|   +-- Televerser_LittleFS_COM3.bat
|   +-- Effacer_Flash_ESP32.bat
+-- src/
    +-- actuators/
    +-- communication/
    +-- config/
    +-- logger/
    +-- logic/
    +-- network/
    +-- runtime/
    +-- safety/
    +-- sensors/
    +-- status/
    +-- web/
```

Sequence de demarrage :

1. Liaison serie a 115200 bauds.
2. LED d'etat sur GPIO2.
3. Coupure de toutes les sorties.
4. Initialisation LittleFS.
5. Chargement ou creation des fichiers JSON.
6. Initialisation actionneurs et securites.
7. Initialisation capteurs.
8. Connexion WiFi ou point d'acces fallback.
9. Initialisation ESP-NOW.
10. Initialisation redondance.
11. Demarrage interface Web.
12. Boucle principale non bloquante basee sur `millis()`.

## Roles des ESP32

Le role est stocke dans `/config/device.json`.

Roles supportes :

- `MASTER`
- `BACKUP`
- `NODE_SENSOR`
- `NODE_ACTUATOR`
- `NODE_MIXED`

### MASTER

Le MASTER est le controleur actif principal. Il lit les mesures disponibles, evalue les regles, calcule les commandes et pilote les actionneurs critiques locaux ou distants.

### BACKUP

Le BACKUP surveille le MASTER par heartbeat ESP-NOW. Si le MASTER disparait au-dela du timeout configure, le BACKUP devient MASTER actif et incremente l'`epoch` de redondance.

### NODE_SENSOR

Un NODE_SENSOR lit des capteurs locaux puis transmet les valeurs au MASTER via ESP-NOW. Il ne pilote pas de sortie critique.

### NODE_ACTUATOR

Un NODE_ACTUATOR recoit des commandes d'actionneurs via ESP-NOW. Il n'accepte les commandes critiques que du MASTER actif reconnu, avec `masterId`, `epoch`, sequence et TTL valides.

### NODE_MIXED

Un NODE_MIXED peut combiner mesures locales et actionneurs locaux. Il reste soumis aux memes securites que les autres roles.

## Redondance MASTER/BACKUP

Le module `RedundancyManager` gere les etats :

- `PASSIVE`
- `ACTIVE_MASTER`
- `BACKUP_READY`
- `TAKEOVER_PENDING`
- `TAKEOVER_ACTIVE`
- `SPLIT_BRAIN_DETECTED`

Parametres par defaut :

- Heartbeat : 300 ms
- Takeover : 1000 ms

En cas de split brain ou double MASTER detecte, le `SafetyManager` passe en defaut critique et les actionneurs critiques sont coupes.

## Capteurs supportes

Capteurs prevus par defaut :

- JSY-MK-194T sur Serial2, RX GPIO16, TX GPIO17.
- TIC Linky sur Serial1, RX GPIO32.
- Trois DS18B20 sur bus OneWire GPIO4.
- Entree analogique.
- Entree digitale.
- Capteur virtuel.
- Capteur distant ESP-NOW.

### JSY-MK-194T

Lecture Modbus RTU manuelle :

- Adresse Modbus : 1
- Vitesse : 4800 bauds
- Format : 8N1
- Registre de depart : `0x0048`
- Verification CRC16

Mesures exposees :

- `voltageV`
- `currentA`
- `activePowerW`
- `gridPowerW`
- `injectionW`
- `consumptionW`
- `surplusW`
- `powerFactor`
- `frequencyHz`
- `energyDirection`
- `available`
- `lastValidReadMs`

Logique de signe :

- Si le JSY indique une injection, `gridPowerW` devient negatif.
- Si `gridPowerW < 0`, alors `injectionW = -gridPowerW`.
- Si `gridPowerW > 0`, alors `consumptionW = gridPowerW`.
- `surplusW = injectionW`.

### TIC Linky

La TIC sert au diagnostic, a la coherence et aux informations energie. Elle ne doit pas etre la mesure principale pour le pilotage rapide.

Mesures minimales :

- puissance apparente
- intensite
- index energie
- option tarifaire
- periode tarifaire
- etat TIC
- dernier age de lecture valide

### DS18B20

Les sondes sont volontairement generiques dans le code :

- `sonde1`
- `sonde2`
- `sonde3`

Le role reel vient de `sensors.json` ou de l'interface Web :

- `ballon_haut`
- `ballon_milieu`
- `ballon_bas`
- `depart_eau_chaude`
- `retour_eau_froide`
- `ambiance`
- `autre`

Chaque sonde peut etre critique ou non critique pour le `SafetyManager`. Les adresses OneWire peuvent etre scannees et associees depuis la page Capteurs.

## Actionneurs supportes

Actionneurs par defaut :

- SSR1 chauffe-eau principal : GPIO26.
- SSR2 auxiliaire : GPIO25.
- RobotDyn Triac : zero-cross GPIO27, gate/control GPIO33.

Types supportes :

- SSR
- RobotDyn Triac
- Relais
- PWM
- Sortie digitale
- Actionneur virtuel
- Actionneur distant ESP-NOW

Modes supportes :

- `OFF`
- `ON_OFF`
- `BURST_FIRE`
- `TRAIN_ONDES_ENTIERES`
- `ZERO_CROSS_BURST`
- `LOW_FREQ_PWM`
- `PHASE_ANGLE`
- `MANUAL_SAFE`

Le mode `PHASE_ANGLE` est reserve au RobotDyn Triac et refuse sur un SSR classique.

## Fichiers de configuration LittleFS

Fichiers utilises :

- `/config/device.json`
- `/config/system.json`
- `/config/sensors.json`
- `/config/actuators.json`
- `/config/rules.json`

Chaque fichier contient un champ `version`. Si un fichier est absent, il est recree avec les valeurs par defaut. Si un JSON est corrompu, il est sauvegarde en `.bak`, puis remplace par une configuration saine.

Important : les vrais fichiers `/config/*.json` vivent dans le LittleFS de l'ESP32 et sont modifies par l'interface Web. Ils ne doivent pas etre places dans `data/`, sinon un televersement LittleFS pourrait ecraser les reglages. Les exemples PC sont dans `config_examples/`.

### device.json

Contient notamment :

- `deviceId`
- `deviceName`
- `role`
- `isConfigured`
- `firmwareVersion`

### system.json

Contient notamment :

- WiFi maison
- point d'acces fallback
- heartbeat redondance
- timeout takeover
- seuils injection
- temperatures limites
- PID
- mode simulation
- options de securite

### sensors.json

Decrit les capteurs locaux, distants ESP-NOW et les sondes DS18B20 generiques.

### actuators.json

Decrit SSR1, SSR2, RobotDyn et les actionneurs ajoutes par l'utilisateur.

### rules.json

Decrit les regles SI / ET / OU / ALORS.

## Interface Web

L'interface est locale, legere et sans CDN obligatoire. Elle reste disponible en WiFi station et en point d'acces fallback.

Pages :

- Dashboard
- Capteurs
- Actionneurs
- Logique
- Diagnostic
- Parametres
- Installation

Le menu lateral gauche est fixe sur desktop et responsive sur mobile.

API principales :

- `GET /api/status`
- `GET /api/sensors`
- `GET /api/actuators`
- `GET /api/rules`
- `GET /api/config`
- `POST /api/config/device`
- `POST /api/config/system`
- `POST /api/config/sensors`
- `POST /api/config/actuators`
- `POST /api/config/rules`
- `POST /api/actuator/command`
- `POST /api/safety/manual-stop`
- `POST /api/system/reboot`
- `POST /api/simulation/start`
- `POST /api/simulation/stop`
- `POST /api/simulation/set-values`
- `GET /api/logs/export`
- `POST /api/logs/clear`

## Moteur de regles

Les regles sont stockees dans `/config/rules.json`.

Structure :

- `id`
- `name`
- `enabled`
- `priority`
- `logic` : `AND` ou `OR`
- `conditions`
- `actions`

Les regles sont triees par priorite. Une regle desactivee ou invalide n'est pas executee. En securite critique, aucune regle ne peut forcer une sortie.

Dans l'interface Logique, les conditions utilisent des listes deroulantes :

- Source
- Mesure
- Operateur
- Valeur
- Unite

Cela evite les erreurs de frappe et les regles invalides.

## Securite logicielle

Le `SafetyManager` centralise les niveaux :

- `OK`
- `WARNING`
- `DEGRADED`
- `CRITICAL`

Causes suivies :

- temperature haute
- sonde DS18B20 critique absente
- JSY absent
- TIC absente
- JSY et TIC absents
- incoherence capteur
- perte MASTER
- risque double MASTER
- erreur configuration
- arret d'urgence manuel

En `CRITICAL` :

- SSR1 est coupe.
- SSR2 est coupe.
- RobotDyn est coupe.
- Les tests manuels sont refuses.
- Une banniere rouge apparait dans l'interface Web.

Certaines securites sur absence de capteurs peuvent etre configurees dans la page Parametres. Cela permet d'adapter le comportement si le chauffe-eau ou le chauffage dispose deja de protections materielles independantes. Cette option doit etre utilisee avec prudence.

## Mode simulation

Le mode simulation est global et stocke dans `system.json`.

Comportement :

- Les capteurs peuvent etre lus normalement.
- Les regles sont evaluees normalement.
- Les commandes actionneurs sont calculees normalement.
- Les GPIO SSR1, SSR2 et RobotDyn ne sont jamais actives.
- L'interface affiche clairement `SIMULATION ACTIVE`.
- Les logs indiquent les commandes qui auraient ete appliquees.

API :

- `POST /api/simulation/start`
- `POST /api/simulation/stop`
- `POST /api/simulation/set-values`

Valeurs simulables :

- `gridPowerW`
- `injectionW`
- `temperatureTop`
- `temperatureMiddle`
- `temperatureBottom`
- `jsyAvailable`
- `ticAvailable`

Lors du passage de simulation vers reel, toutes les sorties sont forcees OFF pendant au moins 2 secondes.

## Cablage texte par defaut

Materiel :

- ESP32
- JSY-MK-194T
- TIC Linky
- 3 sondes DS18B20
- SSR1 sur GPIO26
- SSR2 sur GPIO25
- RobotDyn zero-cross GPIO27, control GPIO33
- LED etat GPIO2

Table de cablage :

```text
ESP32 GPIO16  -> JSY-MK-194T TX
ESP32 GPIO17  -> JSY-MK-194T RX
ESP32 GND     -> JSY-MK-194T GND basse tension

ESP32 GPIO32  -> Sortie TIC Linky via adaptation de niveau adaptee
ESP32 GND     -> Reference basse tension TIC si necessaire selon interface

ESP32 GPIO4   -> Bus OneWire DS18B20 DATA
3V3           -> DS18B20 VCC
GND           -> DS18B20 GND
Resistance 4,7 kOhm entre 3V3 et DATA OneWire

ESP32 GPIO26  -> Entree commande SSR1
ESP32 GPIO25  -> Entree commande SSR2
ESP32 GPIO27  -> Entree zero-cross RobotDyn
ESP32 GPIO33  -> Entree gate/control RobotDyn
ESP32 GPIO2   -> LED etat
```

Ne jamais melanger directement la basse tension ESP32 avec le secteur. Respecter l'isolation des modules et la documentation constructeur.

## Installation Arduino IDE

1. Installer Arduino IDE.
2. Installer le support cartes ESP32 depuis le gestionnaire de cartes.
3. Selectionner une carte ESP32 compatible, par exemple `ESP32 Dev Module`.
4. Ouvrir `RouteurSolaireESP32.ino`.
5. Installer les bibliotheques necessaires.
6. Compiler.
7. Televerser sur l'ESP32.
8. Televerser les fichiers LittleFS si votre environnement le demande.

## Dependances necessaires

A installer depuis le gestionnaire de bibliotheques Arduino :

- ArduinoJson
- OneWire
- DallasTemperature

Fournies par le core ESP32 Arduino :

- WiFi
- WebServer
- LittleFS
- ESP-NOW
- HardwareSerial

## Compilation

Dans Arduino IDE :

1. Ouvrir `RouteurSolaireESP32.ino`.
2. Choisir la carte ESP32.
3. Choisir le port serie.
4. Cliquer sur `Verifier`.

Le projet contient aussi un script local :

```text
Compiler_RouteurSolaireESP32.bat
```

Ce fichier a la racine est un raccourci. Le script reel est dans :

```text
tools/Compiler_RouteurSolaireESP32.bat
```

## Televersement

Dans Arduino IDE :

1. Brancher l'ESP32 en USB.
2. Selectionner le bon port.
3. Cliquer sur `Televerser`.
4. Ouvrir le moniteur serie a 115200 bauds.

Au boot, les sorties sont forcees OFF avant l'initialisation complete.

Pour televerser uniquement l'interface Web LittleFS :

```text
Televerser_LittleFS_COM3.bat
```

Ce raccourci appelle `tools/Televerser_LittleFS_COM3.bat`, cree l'image LittleFS depuis `data/`, l'envoie sur `COM3`, puis demande le redemarrage automatique de l'ESP32.

## Premiere configuration

Au premier demarrage, le firmware tente le WiFi maison par defaut :

```text
SSID : WIFI_SSID_A_CONFIGURER
Mot de passe : WIFI_PASSWORD_A_CONFIGURER
```

Si la connexion echoue apres 20 secondes, l'ESP32 demarre en point d'acces :

```text
SSID : AP_SSID_A_CONFIGURER
Mot de passe : AP_PASSWORD_A_CONFIGURER
IP : 192.168.4.1
```

Procedure :

1. Se connecter au WiFi `AP_SSID_A_CONFIGURER` si le WiFi maison n'est pas disponible.
2. Ouvrir `http://192.168.4.1`.
3. Aller dans `Installation`.
4. Renseigner le nom du module.
5. Choisir le role.
6. Configurer le WiFi maison.
7. Activer ou non ESP-NOW.
8. Scanner les DS18B20.
9. Sauvegarder.
10. Redemarrer l'ESP32.

## Procedure de test

Faire les tests progressivement.

1. Tester sans aucune charge 230 V raccordee.
2. Verifier dans le moniteur serie que LittleFS, WiFi, WebUI et capteurs demarrent.
3. Ouvrir le Dashboard.
4. Activer le mode simulation.
5. Injecter des valeurs de simulation.
6. Verifier que les regles calculent une commande.
7. Verifier que les GPIO restent OFF en simulation.
8. Scanner les DS18B20.
9. Associer les adresses aux sondes generiques.
10. Tester SSR1/SSR2 avec une charge de test adaptee.
11. Tester RobotDyn uniquement avec protections et charge resistive adaptee.
12. Desactiver la simulation.
13. Verifier que les sorties restent OFF pendant la temporisation de securite.

## Procedure de diagnostic

La page `Diagnostic` affiche :

- etat general securite
- raison du dernier defaut
- uptime
- heap libre
- etat LittleFS
- etat WiFi
- etat ESP-NOW
- etat redondance
- role actif
- masterId actif
- epoch
- dernier heartbeat recu
- etat JSY
- etat TIC
- etat DS18B20
- etat actionneurs
- derniers evenements systeme

Actions disponibles :

- filtrer les evenements par niveau
- exporter les logs JSON
- effacer les logs recents
- redemarrer l'ESP32 avec confirmation

Codes evenements suivis :

- `BOOT`
- `CONFIG_LOADED`
- `CONFIG_ERROR`
- `WIFI_CONNECTED`
- `WIFI_FAIL_AP_MODE`
- `JSY_OK`
- `JSY_TIMEOUT`
- `TIC_TIMEOUT`
- `DS18B20_MISSING`
- `SAFETY_TRIGGERED`
- `SAFETY_CLEARED`
- `MASTER_HEARTBEAT`
- `MASTER_TAKEOVER`
- `SPLIT_BRAIN_DETECTED`
- `ACTUATOR_COMMAND`
- `ACTUATOR_FORCED_OFF`
- `CONFIG_CHANGED`

## Procedure en cas de defaut

1. Ne pas rearmer mecaniquement une charge sans comprendre la cause.
2. Lire la banniere rouge ou orange dans l'interface Web.
3. Aller dans `Diagnostic`.
4. Lire `safetyLevel` et `safetyReason`.
5. Verifier les evenements recents.
6. Verifier les sondes DS18B20 critiques.
7. Verifier que le JSY-MK-194T repond.
8. Verifier la TIC Linky si elle est utilisee.
9. Verifier la redondance MASTER/BACKUP.
10. Corriger le cablage ou la configuration.
11. Redemarrer si necessaire.
12. Tester en mode simulation avant de repasser en reel.

Defauts typiques :

- DS18B20 absent ou adresse non associee.
- JSY non alimente, RX/TX inverses ou mauvaise adresse Modbus.
- TIC sans adaptation de niveau correcte.
- WiFi maison indisponible, passage en AP fallback.
- Split brain MASTER/BACKUP.
- Sortie verrouillee par arret d'urgence manuel.

## LED d'etat GPIO2

La LED d'etat utilise GPIO2.

Indications prevues :

- clignotement rapide : WiFi en cours.
- fixe : WiFi connecte.
- clignotement lent : point d'acces fallback.
- double flash : securite active.

Selon la carte ESP32, la LED bleue integree peut etre absente, inversee, ou raccordee a un autre GPIO. Dans ce cas, l'absence d'allumage n'indique pas forcement un defaut firmware.

## Notes importantes

- Le routeur solaire doit continuer a fonctionner meme sans WiFi.
- L'interface Web est disponible en WiFi maison et en AP fallback.
- Les mots de passe WiFi ne doivent pas etre affiches en clair dans les pages de configuration.
- Les tests manuels sont limites et bloques en securite critique.
- Les actionneurs critiques ne doivent pas etre pilotes par deux maitres simultanement.
- Le projet est prevu pour evoluer, notamment vers MQTT ou d'autres capteurs/actionneurs.


