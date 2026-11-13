# RouteurSolaireESP32

Recovery sur carte neuve : `02_Installer_Recovery.bat`. Remplacement de la Recovery seule sur carte deja installee : `02b_Mettre_A_Jour_Recovery_Seule.bat` (port et image `.bin` demandes ; table controlee, ancienne factory sauvegardee). Voir [les deux parcours](doc/INSTALLATION_PAR_PHASES.md#remplacer-uniquement-la-recovery-sur-un-esp-deja-installe).

Installation d'un nouvel ESP32 : [procedure par phases 00 a 04](doc/INSTALLATION_PAR_PHASES.md), avec les lanceurs numerotes a la racine.

Firmware Arduino IDE pour ESP32 permettant de realiser un routeur solaire photovoltaique local, modulaire et distribue.

Le projet garde un firmware commun pour tous les ESP32. Chaque module fonctionne selon ses capteurs, actionneurs et regles configures dans LittleFS, sans selection de role ni redondance.

Voir le [bilan du 4 novembre 2026](doc/ETAT_DU_PROJET.md), l'[audit détaillé](doc/AUDIT_2026-11-04.md) et la [référence ESP-NOW](doc/ESP_NOW.md).

## Avertissement securite 230 V

Ce projet peut piloter des equipements raccordes au secteur 230 V : charge, SSR, triac RobotDyn, relais ou contacteur. Une erreur de cablage, d'isolation, de dissipation thermique ou de configuration peut provoquer electrocution, incendie, destruction de materiel ou declenchement dangereux.

Ne jamais intervenir sous tension. Utiliser des protections materielles adaptees : disjoncteur, differentiel, fusible, terre, boitier ferme, bornes isolees, dissipateur SSR, cables dimensionnes, separation basse tension/secteur et arret d'urgence. Le cablage 230 V doit etre realise ou verifie par une personne qualifiee.

Les securites logicielles du firmware ne remplacent jamais les securites materielles du charge, du thermostat, du limiteur thermique, du tableau electrique ou du montage SSR/triac.

## Objectifs

- Router le surplus photovoltaique vers un charge.
- Fonctionner entierement en local, sans Internet.
- Utiliser un firmware unique configurable.
- Configurer dynamiquement les capteurs, actionneurs et regles.
- Supporter ESP-NOW pour des modules distribues.
- Continuer a fonctionner sans WiFi maison grace au point d'acces local.
- Publier et recevoir des donnees MQTT vers Jeedom si l'option est activee.

Voir [les outils et les commandes de test](tools/README.md) pour le classement du dossier tools.

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
|   |-- Televerser_LittleFS.bat
|   |-- Installation_ESP32.ps1
|   |-- Recovery_Seule.ps1
|   `-- tests/
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
- `EspNowManager` : communication ESP-NOW.
- `EspNowNode` : brique ESP-NOW generique pour decouverte et trames capteurs multi-noeuds.
- `MqttManager` : publication et reception MQTT pour Jeedom.
- `WebUi` : API REST et interface Web.
- `RuntimeState` : etat global.
- `Logger` : messages du journal série.
- `StatusLed` : LED GPIO2.

## Demarrage firmware

Au boot :

1. Initialisation serie 115200 bauds.
2. LED d'etat GPIO2.
3. Coupure de toutes les sorties.
4. Initialisation LittleFS.
5. Chargement ou creation des JSON de configuration.
6. Initialisation des sorties en etat sur.
7. Initialisation actionneurs.
8. Initialisation securites.
9. Initialisation capteurs.
10. Connexion WiFi ou AP fallback.
11. Initialisation ESP-NOW.
12. Démarrage de la WebUI, de MQTT, de la logique et de l'affichage.
13. Boucle coopérative cadencée avec `millis()`.

## Configuration des ESP32

Chaque ESP utilise le meme firmware et execute ses capteurs, actionneurs et regles actives. Aucun role de module ni bascule automatique entre ESP. Les anciens parametres sont retires par une migration ciblee ; voir [la decision et les tests](doc/SUPPRESSION_ROLES.md).

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

La page par defaut `/` redirige vers `/app`.

Pages :

- Dashboard
- Capteurs
- Actionneurs
- Logique
- Diagnostic
- Systeme
- MQTT
- Parametres

Interface de secours :

```text
http://adresse-ip/lite
```

Lien cache de passage en recovery, embarque dans le firmware et independant de LittleFS :

```text
http://adresse-ip/recovery
```

Cette page ne sert pas a mettre a jour directement le firmware. Elle contient seulement un bouton **Passer en recovery**. Les uploads firmware et LittleFS se font ensuite depuis la factory recovery.

API utiles :

- `/api/status-lite`
- `/api/status`
- `/api/diagnostic`
- `/api/sensors`
- `/api/actuators`
- `/api/rules`
- `/api/system`
- `/api/device`
- `/fs`

### Login Web

L'interface Web est protegee par une authentification HTTP Basic.

Identifiants par defaut :

```text
Utilisateur : admin
Mot de passe : routeur1234
```

Le navigateur affiche une fenetre de connexion au premier acces a `/`, `/app`, `/lite`, `/recovery` ou aux API.

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

`routeurSolaire/state` publie un JSON global avec les mesures importantes : puissance reseau, injection, surplus, consommation, tension, courant, securite, temperatures DS18B20 et commandes actionneurs.

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
  "actuatorId": "ssr1_output",
  "command": "setActuatorPercent",
  "value": 25
}
```

Commande directe par topic :

```text
Topic: routeurSolaire/actuator/ssr1_output/set
Payload: 25
```

En etat Safety CRITICAL, les commandes MQTT actionneurs sont refusees sauf les commandes d'arret.

## Dashboard

Le dashboard affiche :

- nom du module
- WiFi, IP, RSSI
- etat securite
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

## Régulation PID

L'action `setPowerFromSurplus` avec le réglage PID pilote la sortie choisie dans une règle. Elle utilise le PID positionnel commun et la source de mesure sélectionnée pour cette action. Le calcul traite les nouveaux échantillons et le chemin de régulation coupe la demande si la mesure est invalide ou périmée. Safety reste prioritaire. Voir [réglages, migration et diagnostic PID](doc/PID_UNIQUE.md).

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

- `temperature_1`
- `temperature_2`
- `temperature_3`
- `temperature_1`
- `temperature_2`
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

- SSR1 charge principal : GPIO5.
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
- `MULTI_SINUS`
- `DEMI_SINUS`
- `LOW_FREQ_PWM`
- `PHASE_ANGLE`
- `MANUAL_SAFE`

L'interface filtre les modes selon le type choisi.

### Modes conseilles

SSR :

- `BURST_FIRE`
- `MULTI_SINUS`
- `DEMI_SINUS`
- `LOW_FREQ_PWM`
- `MANUAL_SAFE`

RobotDyn Triac :

- `PHASE_ANGLE`
- `BURST_FIRE`
- `MANUAL_SAFE`

Relais :

- `ON_OFF`
- `MANUAL_SAFE`

Le mode `ON_OFF` est du tout ou rien. Il est adapte a un relais, une sortie digitale ou un contacteur. Il n'est pas adapte au routage progressif d'un charge.

Le mode `PHASE_ANGLE` est reserve au RobotDyn/Triac avec detection zero-cross. Il permet un dosage fin mais peut generer davantage de parasites.

Pour un SSR AC zero-cross et une charge résistive, `MULTI_SINUS` répartit la commande en salves à trame variable. `BURST_FIRE` utilise la fenêtre configurée par `cycleMs`. Voir le [guide MULTI_SINUS](doc/MULTI_SINUS.md) pour les réglages, les binaires de la livraison et la validation sur banc.

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
- erreur configuration
- arret manuel urgence

En `CRITICAL`, SSR1, SSR2 et RobotDyn doivent etre coupes.

Modes Safety disponibles dans les parametres :

- `strict`
- `warning_only`
- `missing_sensors_off`
- `off`

Le mode `off` est reserve aux tests sans charge 230 V.

## Simulation retiree

La simulation locale est supprimee sur demande utilisateur le 11 novembre 2026. Elle sera reconcue dans un chantier separe. Voir [Systeme et Diagnostic](doc/SYSTEME_DIAGNOSTIC.md).

## ESP-NOW

Le projet contient un gestionnaire integre au main et une brique autonome d'exemple :

- `EspNowManager` : gestionnaire du main, traitant le protocole generique v4 et le format historique des commandes et de la télémétrie.
- `EspNowNode` : brique autonome pour producteurs/consommateurs, utilisee par l'exemple ; elle n'est pas instanciee dans le main.

La reference du fonctionnement actuel, des limites et des tests est [doc/ESP_NOW.md](doc/ESP_NOW.md).

La brique generique se trouve dans :

```text
src/communication/espnow_protocol.h
src/communication/espnow_node.h
src/communication/espnow_node.cpp
examples/EspNowSensorSender/EspNowSensorSender.ino
```

### Noeud ESP-NOW generique

Les services radio sont deduits de la configuration active : acquisition, reception et commandes actionneurs. Aucun role de module a selectionner.

Les capacites declarent ce qu'un noeud sait produire ou traiter :

- `ESPNOW_CAP_LINKY`
- `ESPNOW_CAP_JSY`
- `ESPNOW_CAP_TEMP`
- `ESPNOW_CAP_ROUTER`
- `ESPNOW_CAP_ACTUATOR`

Au demarrage, chaque noeud :

1. passe le Wi-Fi en `WIFI_STA` ;
2. initialise ESP-NOW ;
3. ajoute le peer broadcast `FF:FF:FF:FF:FF:FF` ;
4. annonce periodiquement son identite ;
5. ecoute les annonces des autres noeuds ;
6. maintient une table locale des ESP detectes.

La decouverte ne donne pas a elle seule le droit d'injecter des donnees critiques. Elle sert a afficher les ESP visibles et leurs capacites. Les trames capteurs ne sont appliquees que si le peer source est autorise et si le capteur distant est configure dans la page `Capteurs`.

### Trame de decouverte

`EspNowDiscoveryPacket` contient :

- version de protocole ;
- type de trame `ESPNOW_PACKET_DISCOVERY` ;
- `nodeId` ;
- `nodeName` ;
- services actifs ;
- capacites ;
- type capteur principal ;
- MAC source ;
- sequence ;
- uptime ;
- checksum.

### Protocole capteurs v4

Les capteurs ESP-NOW utilisent un protocole separe en trois familles. ESP-NOW reste uniquement le transport ; `SensorManager` normalise ensuite les valeurs et met a jour le `RuntimeState`.

```text
ESP-NOW transporte
SensorManager normalise
State global expose
PID / dashboard / regles consomment sans connaitre ESP-NOW
```

Types de trames :

- `ESPNOW_PACKET_FAST_DATA` : valeurs compactes frequentes, sans noms ni unites.
- `ESPNOW_PACKET_SENSOR_DISCOVERY` : metadonnees capteur, noms, unites, types de valeurs.
- `ESPNOW_PACKET_DIAGNOSTIC` : qualite radio et diagnostic noeud.
- `ESPNOW_PACKET_DISCOVERY` : presence generale du noeud, services et capacites.

La trame rapide compacte contient uniquement :

```cpp
typedef struct __attribute__((packed)) {
  uint8_t valueType;
  float value;
} EspNowCompactSensorValue;

typedef struct __attribute__((packed)) {
  uint8_t version;
  uint8_t packetType;
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t sensorType;
  uint32_t sequence;
  uint32_t timestampMs;
  bool sensorOk;
  uint8_t valueCount;
  EspNowCompactSensorValue values[6];
  uint16_t checksum;
} EspNowFastSensorPacket;
```

Elle ne contient pas `nodeName`, `sensorName`, `key`, `unit` ni description. Ces informations sont envoyees par `EspNowSensorDiscoveryPacket`, au demarrage puis periodiquement.

La trame de decouverte capteur contient :

- `nodeId`, `nodeName` ;
- `sensorId`, `sensorName`, `sensorRole`, `sensorType` ;
- version firmware ;
- liste des `valueType`, `key`, `unit` disponibles ;
- checksum.

`sensorRole` transporte le role metier du capteur source. Pour une sonde temperature, par exemple, la destination peut reprendre automatiquement `temperature_1`, `temperature_1`, `ambiance` ou tout autre role configure cote source.

La trame diagnostic contient :

- uptime ;
- heap libre ;
- RSSI ;
- compteurs d'envoi/reception ;
- paquets perdus ;
- derniere erreur ;
- checksum.

La taille de chaque structure est verifiee a la compilation pour rester sous la limite ESP-NOW de 250 octets.

### Types de capteurs

```text
SENSOR_UNKNOWN = 0
SENSOR_LINKY = 1
SENSOR_JSY = 2
SENSOR_DS18B20 = 3
SENSOR_TEMP_HUM = 4
SENSOR_RELAY_STATUS = 7
SENSOR_ROUTER = 8
SENSOR_CUSTOM = 255
```

### Types de valeurs

```text
VALUE_POWER_W = 1
VALUE_GRID_POWER_W = 2
VALUE_VOLTAGE_V = 3
VALUE_CURRENT_A = 4
VALUE_APPARENT_POWER_VA = 5
VALUE_POWER_FACTOR = 6
VALUE_FREQUENCY_HZ = 7
VALUE_TEMPERATURE_C = 8
VALUE_HUMIDITY_PERCENT = 9
VALUE_ENERGY_KWH = 10
VALUE_STATE_BOOL = 14
VALUE_RSSI_DBM = 15
```

### Cles de valeurs reservees

Linky :

- `PAPP` en `VA`
- `IINST` en `A`
- `SINSTS` en `VA`
- `GRID` en `W`
- `BASE` en `Wh`

JSY-MK-194T :

- `VOLT` en `V`
- `CURR` en `A`
- `POWER` en `W`
- `PF` sans unite
- `FREQ` en `Hz`
- `ENERGY` en `kWh`

Temperature :

- `TEMP` en `C`
- `HUM` en `%`

Convention obligatoire pour le routeur solaire :

```text
GRID > 0 : achat reseau
GRID < 0 : injection reseau / surplus
GRID = 0 : equilibre
```

### Exemple autonome

Le sketch `examples/EspNowSensorSender/EspNowSensorSender.ino` montre un noeud Linky simule :

- annonce broadcast toutes les 3 s ;
- reception et affichage des noeuds detectes ;
- envoi d'une trame Linky simulee toutes les 500 ms vers `receiverMac` ;
- logs serie complets sur sequence, valeurs, taille de trame, checksum et resultat d'envoi.

Pour transformer l'exemple Linky en JSY ou DS18B20 :

1. changer `NODE_ID` ;
2. changer `NODE_NAME` ;
3. changer `SENSOR_TYPE` ;
4. changer `capabilityFlags` ;
5. appeler `buildJsyPacket()` ou `buildTemperaturePacket()` dans `loop()`.

### Validation terrain minimale

1. Flasher la source et la destination avec le meme protocole ESP-NOW, actuellement `ESPNOW_PROTOCOL_VERSION = 4`.
2. Ouvrir les deux moniteurs serie a 115200 bauds.
3. Verifier que chaque ESP affiche sa MAC source.
4. Verifier que chaque ESP affiche au moins un `ESP-NOW discovery RX`.
5. Verifier que l'emetteur affiche des trames `FAST_DATA`.
6. Verifier que le callback d'envoi affiche `resultat=OK`.
7. Eloigner progressivement les ESP pour valider la robustesse radio.
8. Couper un ESP et verifier que l'autre continue sans blocage.

### Frequences et priorites

L'export ESP-NOW cote source est volontaire : un capteur local n'est envoye que si sa fiche capteur contient `espNowExportEnabled = true`. Dans la page `Capteurs`, cette option apparait dans le bloc `Export ESP-NOW`, sous forme de case a cocher. Cela evite qu'un ESP source publie automatiquement tous ses capteurs locaux.

Exemple dans `/config/sensors.json` pour un capteur local :

```json
{
  "id": "tic_linky",
  "name": "TIC Linky",
  "type": "TIC Linky",
  "source": "local",
  "espNowExportEnabled": true,
  "espNowExportIntervalMs": 1000,
  "espNowExportPriority": 1,
  "espNowSendOnChange": true,
  "espNowMinDelta": 10.0
}
```

Pour une sonde DS18B20, les memes champs sont portes directement par l'objet de la sonde dans `ds18b20`.

Le bloc historique `system.espnow.exports` n'est plus utilise pour activer un export capteur. Il reste tolere dans le JSON systeme pour ne pas casser les anciennes configurations, mais il ne doit plus annoncer de capteur fantome. La source de verite est maintenant uniquement la configuration du capteur dans `/config/sensors.json`.

Champs importants :

- `sensorId` identifie le capteur local exporte. Convention actuelle : `1` Linky, `2` JSY, `20..22` DS18B20.
- `sensorName` est envoye dans la trame `SENSOR_DISCOVERY`.
- `sensorType` suit l'enum ESP-NOW : Linky `1`, JSY `2`, DS18B20 `3`.
- `exportEnabled = false` garde le capteur local uniquement.
- `exportIntervalMs` fixe la periode maximale entre deux trames rapides.
- `sendOnChange` + `minDelta` permettent d'envoyer plus vite si la valeur principale varie assez.
- `priority` reste une information de politique d'envoi : `0` bas, `1` normal, `2` haut, `3` critique.

Les valeurs ne sont donc plus toutes envoyees a la meme frequence :

- JSY / puissance reseau : trame rapide, typiquement 200 a 500 ms.
- Linky : trame rapide, typiquement 500 a 1000 ms.
- DS18B20 : trame rapide seulement quand disponible ; la periode peut etre augmentee ensuite vers 5 a 30 s.
- Diagnostic : 10 s par defaut.
- Decouverte capteur : 30 s par defaut.

Les pertes de paquets sont calculees par `nodeId + sensorId` a partir du compteur `sequence`. Le premier paquet n'est pas considere comme une perte. Les timeouts sont geres par type dans `SensorManager` :

- JSY : 1000 ms ;
- Linky : 3000 ms ;
- temperature : 30000 ms ;
- defaut : 2000 ms.

Si un capteur distant expire, il est marque `ok = false`. Les modules metier continuent a lire uniquement `SensorManager` ou `RuntimeState`.

### API Web ESP-NOW

L'interface Web principale expose une page dediee `ESP-NOW`, placee dans le menu de gauche sous `MQTT`.

Endpoints disponibles :

- `GET /api/espnow` : etat local ESP-NOW, MAC locale, services, capacites, peers autorises et noeuds detectes.
- `GET /api/espnow/discovery` : liste brute des noeuds ESP-NOW detectes.
- `POST /api/espnow/discovery/announce` : force une annonce de presence immediate.
- `POST /api/espnow/sensors/announce` : force l'annonce immediate des capteurs locaux exportables par `SENSOR_DISCOVERY`.
- `POST /api/espnow/peer` avec `mac=AA:BB:CC:DD:EE:FF` : ajoute un peer autorise.
- `POST /api/espnow/peer/remove` avec `mac=AA:BB:CC:DD:EE:FF` : retire un peer autorise.

La page Web affiche :

- l'etat local ESP-NOW ;
- la MAC locale ;
- les services et capacites declares ;
- les ESP detectes par broadcast ;
- les peers autorises ;
- les options de debug transport, separees entre transmission et reception.

Dans la page `Capteurs`, les trames `SENSOR_DISCOVERY` recues font apparaitre une section `Capteurs ESP-NOW disponibles`. Seule l'action utilisateur `Ajouter`, suivie de la sauvegarde, cree la configuration locale du capteur distant avec :

- `source = espnow` ;
- la MAC source ;
- le `sensorId` distant ;
- le type metier deduit de `sensorType` ;
- le role metier repris depuis `sensorRole` ;
- `remoteKey = ALL`.

La liste est regroupee par ESP source. Un capteur deja ajoute est grise et ne peut pas etre ajoute une seconde fois. Pour les sondes temperature, le role envoye par la source est conserve afin qu'une sonde `temperature_1` ou `temperature_2` reste exploitable comme une sonde locale equivalente sur la destination.

Le capteur distant est ensuite vu par `SensorManager` comme un capteur local virtuel. Pour un Linky ESP-NOW, `remoteKey = ALL` importe :

- `GRID` vers la puissance reseau TIC ;
- `PAPP` ou `SINSTS` vers la puissance apparente ;
- `IINST` vers le courant ;
- `BASE` vers l'index energie.

Les autres profils suivent la meme logique :

- `JSY-MK-194T` : `VOLT`, `CURR`, `POWER`, `GRID`, `PF`, `FREQ`, `ENERGY` si disponible ;
- `DS18B20` : `TEMP` ; le role envoye par la source indique comment `TEMP` est utilisee ;

Il reste possible de choisir une seule valeur distante en remplacant `ALL` par une cle precise comme `GRID`, `TEMP`, `VOLT` ou `POWER`. L'option `Debug trames ESP-NOW` est commune a tous les profils. Elle ajoute au journal série une ligne decodee avec la sequence, le type et toutes les valeurs de chaque trame recue depuis la MAC configuree.

Important : seules les trames provenant d'un peer autorise sont appliquees. Cette separation evite qu'un ESP inconnu ou mal configure puisse modifier directement `state.gridPowerW` ou les variables utilisees par la regulation.

### Etat implementation ESP-NOW relu le 18 octobre 2026

- Les trames rapides `FAST_DATA` sont compactes et ne transportent plus les noms, roles ni unites.
- Les trames `SENSOR_DISCOVERY` transportent les metadonnees : `nodeName`, `sensorName`, `sensorRole`, `sensorType`, firmware et valeurs disponibles.
- Les trames `DIAGNOSTIC` transportent les informations radio et systeme.
- `SensorManager` conserve les capteurs distants dans une table runtime et les expose dans `/api/status`.
- La page `Capteurs` permet l'ajout manuel d'un capteur distant decouvert ; aucune discovery seule ne cree sa configuration.
- Les options de debug ESP-NOW sont separees entre transmission et reception dans la page `ESP-NOW`.
- Le JSON capteurs accepte maintenant une liste `sensors` vide, sans recreer automatiquement un capteur distant supprime.

Les TTL, le filtrage des heartbeats et l'arbitrage des commandes distantes restent a corriger ; voir [les limites ESP-NOW](doc/ESP_NOW.md#défauts-prioritaires-ouverts).

### Messages historiques EspNowManager

Messages supportes par la couche historique :

- `SENSOR_VALUE`
- `ACTUATOR_COMMAND`
- `HEARTBEAT`
- `CONFIG_SYNC`
- `STATUS`
- `ERROR`

Les messages incluent notamment :

- `sequenceNumber`
- `senderId`
- `timestamp`
- checksum
- TTL pour les commandes actionneurs

Un actionneur refuse les commandes d'un pair non autorise, au checksum invalide, expirees ou interdites par SafetyManager. Aucun MASTER ni epoch d'autorite. Les emplacements retires dans les trames sont reserves pour conserver le format radio.

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
RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)
```

Cette partition permet :

- une partition `factory` pour la recovery
- une partition `ota_0` pour le firmware principal
- 1 Mo pour LittleFS
- l'interface Web complete dans `/www`
- les fichiers JSON de configuration dans `/config`

Si le menu de partition n'apparait pas dans Arduino IDE :

1. Fermer Arduino IDE.
2. Relancer Arduino IDE.
3. Ouvrir le gestionnaire de cartes.
4. Laisser l'IDE recharger les donnees des cartes.
5. Revenir dans `Outils > Partition Scheme`.
6. Selectionner `RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)`.

Il peut y avoir un cache cote Arduino IDE. Si l'option n'apparait pas tout de suite, un redemarrage complet de l'IDE ou un rechargement des donnees de cartes peut etre necessaire.

Le fichier de partition du projet est :

```text
partitions_recovery_main_4MB.csv
```

Il contient :

```text
factory 0x10000   0x110000
ota_0   0x120000  0x1E0000
spiffs  0x300000  0x100000
```

Dans le core ESP32 Arduino, la partition est declaree sous le nom :

```text
partitions_recovery_main_4MB
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
RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)
```

5. Compiler et récupérer le binaire principal pour la factory recovery.

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

Cette signature permet a la page **Systeme** de relire la version presente dans le firmware principal `ota_0`.

Important : une partition qui contient un ancien firmware construit avant cette signature peut afficher `N/A`. Elle affichera sa version apres avoir recu un firmware genere avec ce systeme.

Dans la page **Systeme > Boot / Recovery**, on peut voir :

- la partition lancee actuellement
- la partition configuree au boot
- les informations de la factory recovery
- les informations de `ota_0`
- la taille du slot firmware
- la taille du firmware utilise
- le reste disponible

La page propose aussi un bouton **Passer en recovery**. Ce bouton synchronise les identifiants WiFi maison vers NVS, configure le prochain boot sur `factory`, puis redemarre l'ESP32.

## Televersement firmware

### Depuis la factory recovery

Pour un ESP déjà installé, suivre le passage explicite en factory et envoyer le firmware principal vers `ota_0`. La [procédure par phases](doc/INSTALLATION_PAR_PHASES.md) décrit l'installation initiale et le parcours de maintenance.

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
2. cree le fichier LittleFS dans `build/ota/RouteurSolaireESP32_littlefs.bin`
3. ecrit la version LittleFS dans `data/www/littlefs_version.txt`
4. cree aussi une copie versionnee `build/ota/RouteurSolaireESP32_littlefs_YYYYMMDD-NN.bin`
5. demande si l'on veut televerser maintenant sur le port COM
6. si la reponse est oui, l'ecrit dans la partition LittleFS a l'adresse `0x300000`
7. utilise une taille LittleFS de `0x100000`
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
http://192.168.4.1/recovery
http://192.168.4.1/lite
```

`/recovery` est une page embarquee dans le firmware principal et independante de LittleFS. Elle contient seulement le bouton **Passer en recovery**.

## Recovery factory

Les mises a jour firmware et LittleFS sont centralisees dans la factory recovery.

Depuis le firmware principal :

1. Ouvrir l'interface principale `/app`.
2. Aller dans **Systeme > Actions systeme**.
3. Cliquer **Passer en recovery**.

Lien cache direct, independant de LittleFS :

```text
http://adresse-ip/recovery
```

Le bouton :

1. synchronise le SSID et le mot de passe WiFi maison vers NVS `recovery`
2. configure le prochain boot sur `factory`
3. redemarre l'ESP32

Dans la recovery :

- AP toujours actif : `RouteurSolaire_Recovery`
- URL AP : `http://192.168.4.1`
- tentative automatique de connexion au WiFi maison si les identifiants sont disponibles
- formulaire WiFi local disponible pour changer SSID/mot de passe
- upload firmware local vers `ota_0`
- upload image LittleFS locale vers `spiffs`
- redemarrage vers `ota_0` seulement via le bouton dedie

Procedure firmware via recovery :

1. Compiler le firmware avec `Compiler_RouteurSolaireESP32.bat`.
2. Recuperer le fichier `.bin` genere dans `build/ota`.
3. Passer en recovery.
4. Ouvrir `http://192.168.4.1` ou l'IP locale affichee par la recovery.
5. Choisir le fichier firmware.
6. Envoyer vers `ota_0`.
7. Cliquer **Demarrer ota_0** quand les mises a jour souhaitees sont terminees.

Procedure LittleFS via recovery :

1. Générer l'image avec `03_Generer_Firmware_et_LittleFS.bat` et exporter séparément les cinq JSON de `/config`.
2. Passer en recovery.
3. Ouvrir `http://192.168.4.1` ou l'IP locale affichee par la recovery.
4. Choisir le fichier LittleFS `.bin`.
5. Envoyer vers `spiffs`.
6. Cliquer **Demarrer ota_0** quand les mises a jour souhaitees sont terminees.

### Configuration et LittleFS

L'ecriture LittleFS remplace toute la partition `spiffs`. Les fichiers `/config/*.json` sont dans LittleFS : si l'image LittleFS ne contient pas la configuration voulue, il faudra reconfigurer ou restaurer les fichiers.

Ne pas couper l'alimentation pendant une mise a jour firmware ou LittleFS.

## Résumé des méthodes de mise à jour

Pour une carte déjà configurée, exporter les cinq JSON, générer les images avec la phase 03 et installer depuis la factory recovery avec la phase 04. Le remplacement de LittleFS touche toute la partition. Comparer les configurations après restauration. Voir la [procédure par phases](doc/INSTALLATION_PAR_PHASES.md) et les [limites de préservation](doc/OTA.md).

Les écritures USB directes servent à l'installation et à la maintenance sur banc. Vérifier la table de partitions de la carte avant de les utiliser.

## Premiere configuration

1. Flasher le firmware.
2. Televerser LittleFS.
3. Demarrer l'ESP32.
4. Se connecter au WiFi maison ou a l'AP fallback.
5. Ouvrir `/app`.
6. Aller dans Parametres.
7. Regler :
   - nom module
   - WiFi
   - source puissance reseau JSY/TIC/AUTO
   - Safety
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

Sur banc basse tension, sorties de puissance deconnectees :

1. Demarrer et verifier les sorties OFF, le port serie et WiFi/AP.
2. Ouvrir Systeme : versions, partitions et acces reseau.
3. Ouvrir Diagnostic : securite, fraicheur des capteurs et console.
4. Verifier perte/retour de mesure, arret systeme OFF et refus des anciennes regles de simulation.

Avec capteurs :

1. Scanner DS18B20.
2. Lire JSY.
3. Lire TIC.
4. Verifier source reseau.
5. Verifier Safety.

Avec sorties :

1. Verifier le niveau OFF au demarrage et les protections, sur banc adapte.
2. Tester SSR sans charge 230 V.
3. Tester SSR avec charge resistive adaptee.
4. Tester RobotDyn en dernier.

## Diagnostic

Page Diagnostic :

- securite
- raison dernier defaut
- uptime
- heap libre
- LittleFS
- WiFi
- ESP-NOW
- JSY
- TIC
- DS18B20
- actionneurs
- console série Web

Messages typiques du journal série :

- `BOOT`
- `CONFIG_LOADED`
- `WIFI_CONNECTED`
- `WIFI_FAIL_AP_MODE`
- `JSY_TIMEOUT`
- `TIC_TIMEOUT`
- `DS18B20_MISSING`
- `SAFETY_TRIGGERED`
- `ACTUATOR_FORCED_OFF`
- `CONFIG_CHANGED`

## En cas de defaut

1. Ne pas forcer une sortie 230 V.
2. Lire la bannierre rouge/orange.
3. Aller dans Diagnostic.
4. Consulter la console série.
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

## État actuel

Le PID positionnel est commun aux sources de mesure. Les mises à jour passent par la factory recovery. Consulter le [bilan courant](doc/ETAT_DU_PROJET.md) et l'[audit](doc/AUDIT_2026-11-04.md) pour les limites et les résultats des tests.
