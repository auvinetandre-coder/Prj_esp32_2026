# ESP_NOW.md - Architecture ESP-NOW

Mise a jour du 21 octobre 2026 : roles et redondance supprimes du firmware, des configurations actives, des API et de la WebUI ; onglet Secours retire. Voir [le contrat actuel et la migration](SUPPRESSION_ROLES.md). Les observations datees du 20 octobre ci-dessous sont historiques pour ces points.

Relecture du 20 octobre 2026. Les sections d'état décrivent le code et ses limites ; principes/workflows restent des exigences ou cibles. Voir le [bilan](ETAT_DU_PROJET.md) et l'[audit courant](AUDIT_2026-10-20.md). Le test de présence de nœud passe localement ; il ne valide ni le TTL, ni les heartbeats MASTER/BACKUP, ni la radio réelle.

## Objectif

### Commande partagee MANUEL/OFF/AUTO — firmware et LittleFS 20261026-03

Les commandes Web explicites sont maintenant distribuees a l'ESP physique.
Un AUTO depuis l'un des ESP annule la session manuelle precedente du meme
actionneur ; le retour periodique annule ensuite son renouvellement sur le
pilote. Un nouveau MANUEL ou OFF explicite peut reprendre la main depuis
l'une ou l'autre interface, sous reserve de Safety et du mode systeme FORCE.
Ce comportement remplace la priorite inconditionnelle de l'ancien manuel
local sur un nouveau geste manuel distant. L'ESP physique arbitre selon
l'ordre de reception des nouvelles intentions utilisateur.

Chaque intention manuelle utilise un jeton aleatoire non nul lie a la MAC
du pilote. Les commandes periodiques renouvellent cette session sans pouvoir
la recreer. AUTO, un autre geste explicite, une coupure globale ou l'expiration
du watchdog invalident l'ancien jeton : un paquet repete a 49 % est refuse
apres AUTO au lieu de reactiver le forcage. Les commandes automatiques sans
jeton restent admises en AUTO. Safety reste prioritaire ; simulation n'emet
ni intention de pilotage reel ni commande de puissance reelle.

Format historique conserve : `controlManual` et `controlAuto` sont des commandes
`MSG_ACTUATOR_COMMAND`, leur jeton est dans `reservedCounter`. Le callback interne
recoit maintenant la MAC et ce jeton. `safetyShutdown` assure la coupure globale
sans etre refuse comme une ancienne consigne de session. Pour `actuatorState`,
`mode` contient AUTO/MANUAL/OFF et `ttlMs` contient le jeton actif ; ces champs
etaient inutilises pour l'etat. Les API exposent `controlMode` et les panneaux
affichent Mode partage. Le format des capteurs et les configurations sont preserves.

Les retours restent autorises par peer et import MAC/remoteActuatorId. Le pilote
attend au moins 2 s apres sa demande avant d'annuler un forçage dont le jeton
n'est pas confirme, pour tolerer un ancien retour deja en transit. Une demande
radio perdue n'est pas reconstituee par les renouvellements de puissance : elle
echoue sans activer la sortie et le mode du proprietaire physique fait foi.
Le jeton distingue les sessions, sans constituer une authentification
cryptographique ou une protection contre le rejeu d'intentions explicites.
Le mode systeme FORCE reste un reglage independant ; AUTO du panneau ne change
pas automatiquement la configuration systeme. Des regles AUTO peuvent appliquer
une puissance non nulle apres liberation du manuel.

Les deux ESP doivent recevoir firmware et LittleFS 20261026-03 pour obtenir
la coordination de mode. Les versions precedentes ne connaissent pas les
intentions partagees, meme si elles affichent deja un retour de puissance.
L'installation se fait par recovery explicite, avec sauvegarde verifiee des
cinq configurations de chaque ESP avant LittleFS et restauration si necessaire.

Validation logicielle : `tools/test_shared_actuator_control.js` execute les corps
C++ de demande, reception et observation dans deux contextes ESP simules, ainsi
que le controleur de session. Cas testes : MANUEL/OFF/AUTO dans les deux sens,
ancien renouvellement refuse, changement de pilote, autre sortie preservee,
MAC du controleur, Safety, simulation, expiration et rebouclage de millis().
Voir `VERIFICATION_COMMANDES_PARTAGEES.md` pour les preuves et essais materiels.

### Panneaux manuels synchronises — LittleFS 20261026-02

Les deux interfaces suivent maintenant la commande appliquee par l'ESP physique,
y compris dans le curseur, le champ numerique et le grand pourcentage d'un
panneau deja ouvert. Le panneau s'ouvre sur `commandPercent`, pas sur une ancienne
consigne manuelle du pilote. Le libelle ambigu Derniere consigne locale est
remplace par Commande appliquee, Sortie annoncee et Source.

La synchronisation utilise le retour existant toutes les secondes. Elle appelle
`setManualPowerValue` sans envoi : aucun echo de commande entre ESP. Une saisie
active, un glissement, une consigne en attente, une requete en cours et un delai
de 1,5 s apres demande protegent le curseur contre une valeur encore ancienne.
Une erreur de rafraichissement ou une commande inconnue ne simule pas un zero.

OFF et AUTO remettent toujours immediatement la saisie a zero. Ensuite, AUTO suit
la commande reellement appliquee, qui peut etre non nulle si ESP-NOW ou une regle
pilote encore la sortie. Les priorites du firmware, dont le manuel local du SSR
et Safety, restent identiques. Ce changement synchronise l'affichage et ne
fusionne pas des ordres contradictoires ni ne commande les sorties par polling.

Correction dans `data/www/app.js`, tests dans `tools/test_espnow_actuator_state.js`
et marqueur LittleFS. Les 15 tests doivent passer. Firmware 20261025-08 compatible,
pas de reconstruction firmware necessaire. Installer LittleFS 20261026-02 sur
les deux ESP par recovery apres sauvegarde verifiee de leurs configurations.
Sur les deux panneaux ouverts, tester 83 %, OFF, changement local 40 %, puis
AUTO ; les curseurs et les etats doivent converger apres actualisation. Verifier
separement la protection d'une saisie active et l'indisponibilite du retour.

### Curseur manuel OFF/AUTO — LittleFS 20261026-01

Sur les deux ESP, OFF et AUTO remettent immediatement le curseur, la saisie et
le pourcentage manuel a zero. AUTO ne mettait pas le curseur a jour ; OFF
attendait la fin des requetes HTTP et du rafraichissement avant de le faire.
Une reponse tardive pouvait aussi effacer une nouvelle saisie apres OFF.
Le retour annonce du SSR reste separe ; un etat UNKNOWN avec un ESP eteint
ne bloque pas la remise a zero de la consigne affichee.

Correction WebUI uniquement dans `data/www/app.js`. Le firmware 20261025-08
reste utilisable. `tools/test_manual_actuator.js` reproduit 47 % puis OFF/AUTO,
avec reponse HTTP retardee et nouvelle saisie pendant l'attente. Les commandes
en attente restent annulees avant OFF/AUTO. Aucun ordre supplementaire n'est
genere pour remettre le curseur a zero. Validation sur cartes encore necessaire.

### Retour d'etat actionneur — firmware et LittleFS 20261025-08

L'ESP physiquement raccorde au SSR annonce maintenant son etat une fois par
seconde aux peers configures lorsque l'export de cet actionneur est active.
Cela inclut une modification manuelle locale, une commande ESP-NOW appliquee,
une sortie desactivee et la coupure Safety. L'annonce est une trame historique
`MSG_STATUS` avec `variable = actuatorState` ; le format binaire existant reste
identique. `actuatorId` identifie la cible locale ; `commandPercent` contient sa
commande, `value` sa sortie en pourcentage et `command` la source de pilotage.
Les bits 0..3 de `reservedCounter` indiquent ON, enabled, safetyLocked, simulation.
Le decodeur distingue le marqueur historique `0xff` des paquets generiques v4 :
`MSG_STATUS` vaut aussi 4 et etait sinon pris pour une version de protocole.

Le recepteur exige un peer autorise, un checksum valide et un import actionneur
correspondant a la paire MAC/remoteActuatorId. Aucun import ou changement de
configuration n'est realise. Les valeurs finies restent bornees a 0..100.
La table est bornee a 16 retours ; les entrees expirees peuvent etre reutilisees.
Le retour expire apres 3,5 s sans nouvelle annonce, independamment de la discovery.

Les API d'etat et runtime exposent les valeurs annoncees dans `commandPercent`,
`outputPercent`, `outputOn`, `source` et `estimatedPowerW`. `requestedPercent`
conserve la derniere consigne du pilote. `remoteStateAvailable` et `stateAgeMs`
indiquent la disponibilite et l'age du retour. Sans retour recent, les valeurs
annoncees sont nulles et l'actionneur est degrade ; une consigne envoyee seule
ne prouve plus l'etat applique. Ce retour periodique n'est pas un ACK correle
a une commande (`commandAcked` reste false), ni une mesure electrique de puissance.

La WebUI utilise le snapshot d'etat actuel dans Diagnostic/Dashboard, meme apres
une visite de la page Actionneurs. Le panneau manuel affiche la sortie annoncee,
sa source et la derniere consigne locale sans deplacer le curseur ni renvoyer
de commande. Un forcage manuel sur l'ESP du SSR reste prioritaire ; les deux
consignes peuvent donc differer volontairement, avec une sortie annoncee commune.

Fichiers : `src/communication/espnow_manager.h/.cpp`,
`src/actuators/actuator_manager.h/.cpp`, `data/www/app.js` et marqueurs de version.
Tests : `tools/test_espnow_actuator_state.js`, adaptation du banc
`tools/test_ssr_nan.js`, puis ensemble des tests existants et compilation ESP32.

Sur banc : installer le firmware sur les deux ESP et LittleFS avec sauvegarde
prealable de `/config`, via passage explicite en recovery. Verifier export et
peers dans les deux sens. Depuis le pilote, appliquer 25/60/OFF et comparer les
deux pages Actionneurs et Diagnostic. Puis rendre la main avec AUTO sur le pilote
et appliquer 40 % manuellement sur l'ESP du SSR : le pilote doit voir ce retour
environ une seconde plus tard. Tester un OFF local prioritaire, Safety et une
perte radio : les valeurs doivent devenir indisponibles apres 3,5 s.

### Correction commande distante — firmware 20261025-07

La commande manuelle d'un SSR importe pouvait etre rejetee parce que le TTL
comparait les `millis()` de deux ESP demarres a des instants differents. Une
commande acceptee pouvait aussi etre remise a zero au cycle IDLE/AUTO suivant.

`ttlMs` definit maintenant un maintien depuis la reception locale, borne par
`router.commandTimeoutMs` du recepteur (5 s par defaut). Un TTL nul utilise ce
watchdog ; il ne desactive pas la coupure. `timestamp` reste dans la trame mais
n'est plus utilise pour comparer deux horloges. Cela ne mesure pas l'age en
transit et ne fournit pas de protection contre le rejeu des trames.

Chaque canal local conserve sa consigne ESP-NOW jusqu'a expiration ou nouvelle
commande. Les commandes renouvelees par le pilote prolongent le maintien.
Sans renouvellement, la boucle actionneurs coupe la sortie ; les automatismes
locaux peuvent ensuite reprendre. Safety, recovery, allOff et le manuel local
restent prioritaires. Le recepteur refuse les commandes pendant un forcage local
ou sa simulation. L'API actionneur indique `source = ESP-NOW` pendant le maintien.

Le callback interne `ActuatorCommandHandler` recoit desormais `ttlMs` en dernier
argument pour armer ce watchdog. Son unique consommateur est adapte. Le format
radio et les routes Web restent identiques. Fichiers concernes :
`src/communication/espnow_manager.h/.cpp`, `src/actuators/actuator_manager.h/.cpp`
et `src/logic/automate_manager.cpp`.

Le SSR doit etre importe comme **actionneur ESP-NOW** sur l'ESP pilote ; un import
comme capteur ne constitue pas une cible de commande. Les deux ESP doivent
autoriser mutuellement leurs MAC STA dans leurs peers. La discovery seule ne
garantit pas cette autorisation.

Tests locaux : `tools/test_espnow_actuator_command.js` execute les blocs de
production d'acceptation, maintien, expiration et arbitrage (boots decales, peers,
Safety, manuel, autres canaux, rebouclage d'horloge).
`tools/test_manual_actuator.js` conserve la verification du manuel.

Sur banc : commander 25 puis 60 puis 0 %, verifier la sortie sur l'ESP du SSR,
redemarrer les ESP separement, remettre 60 % puis interrompre le pilote pour verifier la coupure
dans le delai configure, sans regle locale reprenant la sortie. Verifier aussi
OFF manuel local, Safety et recovery. Mise a jour firmware par passage explicite
en recovery ; aucune modification de `/config` ou de LittleFS n'est necessaire.

Les constats TTL et arbitrage dates du 20 octobre plus bas sont historiques a
partir de cette correction. La validation radio et electrique reste a faire.

ESP-NOW permet aux ESP32 du projet de communiquer localement sans dependre d'un serveur externe.

Il sert a :

- decouverte ;
- heartbeat ;
- remontee de mesures ;
- annonce de capteurs ;
- annonce d'actionneurs ;
- diagnostics ;
- supervision de presence des modules.

---

## Correction récente de présence

### Correction des fausses pertes de noeud - firmware 20261020-04

Observation sur le recepteur SSR : les mesures Linky arrivaient environ chaque
seconde et restaient a 0 W, mais la commande retombait periodiquement a zero
avec `LINKY_UNAVAILABLE`. Trace locale : `build/linky_routing_trace_20261020.jsonl`.
Les journaux ont montre `ESPNOW NODE LOST` a 430066 ms puis
`ESPNOW NODE RESTORED` a 430082 ms, suivis du defaut PID et d'une nouvelle mesure.

La comparaison de perte utilisait l'heure capturee au debut de la boucle.
Le callback RX Wi-Fi pouvait entre-temps actualiser `lastSeenMs` a une heure
plus recente : `now >= lastSeenMs` devenait faux et declarait le noeud perdu.
`updateDiscoveredNodeLoss` lit maintenant d'abord l'horodatage de reception,
puis l'heure courante pour calculer l'age. Le delai reel de perte de noeud
reste 30 s ; la soustraction non signee fonctionne au rebouclage de `millis()`.
Les delais des capteurs et les imports manuels sont preserves.

Fichiers : `src/communication/espnow_manager.cpp`,
`tools/test_espnow_node_liveness.js`. Le test execute le corps du controleur
C++ traduit mecaniquement et verifie une reception plus recente que la boucle,
une vraie expiration, le retour du noeud, les notifications uniques et le
rebouclage d'horloge. Sur ESP, verifier la disparition des couples LOST/RESTORED
immediats et le maintien de la detection d'une vraie interruption.

## Principes

- ESP-NOW doit rester leger.
- Les imports de capteurs doivent rester manuels.
- Un capteur ESP-NOW importe doit etre vu comme un capteur local virtuel.
- Le dashboard ne doit pas contenir de logique specifique ESP-NOW.
- ESP-NOW ne doit jamais transporter de firmware `.bin`.
- ESP-NOW ne remplace pas le recovery local.

---

## Trames

Trames génériques implémentées, avec `ESPNOW_PROTOCOL_VERSION = 4` dans `src/communication/espnow_protocol.h` :

- `DISCOVERY`
- `SENSOR_DATA`
- `FAST_DATA`
- `DIAGNOSTIC`
- `SENSOR_DISCOVERY`
- `ACTUATOR_DISCOVERY`

`HEARTBEAT` appartient au format historique `EspNowMessage`, séparé du protocole générique v4. Aucune négociation de version v3/v4 n'est implémentée : source et destination doivent utiliser un protocole compatible.

---

## DISCOVERY

La discovery du nœud annonce actuellement :

- nom ESP ;
- MAC ;
- services et capacites actifs ;
- version firmware ;
- version LittleFS ;
- séquence et uptime.

Les capteurs et actionneurs exportés sont décrits par des annonces séparées. La partition courante reste une évolution : elle n'est pas dans `EspNowDiscoveryPacket`.

---

## Import manuel

Workflow :

1. un ESP distant annonce ses capteurs ;
2. la page ESP-NOW affiche les noeuds detectes ;
3. l'utilisateur ouvre le detail ;
4. les capteurs disponibles sont affiches ;
5. l'utilisateur clique sur Ajouter ;
6. le capteur devient virtuel local via SensorManager ;
7. le dashboard l'affiche comme les autres.

---

## Maintenance via ESP-NOW

ESP-NOW ne transporte pas le binaire.

Dans l'architecture Recovery/Main validee, ESP-NOW peut seulement annoncer un etat ou une intention de maintenance. Il ne declenche pas de telechargement GitHub et ne transporte jamais de `.bin`.

Workflow cible, non implémenté comme procédure distante complète :

1. Un ESP affiche les versions et l'etat des noeuds ;
2. utilisateur decide une maintenance ;
3. l'ESP concerne passe en recovery ou signale qu'il doit etre maintenu ;
4. la mise a jour se fait localement via le recovery ;
5. l'ESP annonce sa nouvelle version en discovery.

## État du code

### Modules et formats

| Fichier | Fonction présente |
|---|---|
| `src/communication/espnow_protocol.h` | Protocole générique v4 : structures packed, types et checksum |
| `src/communication/espnow_manager.cpp` | Gestionnaire instancié par le main : discovery, exports, réception et messages historiques |
| `src/communication/espnow_node.cpp` | Brique autonome utilisée par l'exemple ; non instanciée dans le main |
| `src/sensors/sensor_manager.cpp` | Inventaire distant en RAM, mapping, plausibilité des mesures et expiration |
| `src/actuators/actuator_manager.cpp` | Commandes locales/distantes et handler des commandes reçues |
| `src/web/web_ui.cpp`, `data/www/app.js` | API et pages de configuration/diagnostic |
| `examples/EspNowSensorSender/EspNowSensorSender.ino` | Exemple autonome de producteur de mesures |

Le main utilise un seul `EspNowManager` pour les deux formats. `EspNowNode` initialise ses propres callbacks et force le Wi-Fi en STA ; ne pas l'initialiser en parallèle du gestionnaire du main sans analyser ces interactions.

| Trame générique | Contenu |
|---|---|
| `DISCOVERY` | Identité, MAC, rôles, capacités, séquence, uptime, versions firmware/LittleFS |
| `SENSOR_DATA` | Jusqu'à 8 valeurs avec clés et unités ; notamment les groupes de valeurs JSY |
| `FAST_DATA` | Jusqu'à 6 couples type/valeur, sans noms ni unités |
| `SENSOR_DISCOVERY` | Identité, nom, rôle métier, type, versions et jusqu'à 6 descriptions de valeurs |
| `ACTUATOR_DISCOVERY` | Identité, rôle, type, mode électrique, puissance maximale, timeout et versions |
| `DIAGNOSTIC` | Structure prévue pour uptime, heap, RSSI, compteurs et erreur |

Les six structures génériques ont des `static_assert` à 250 octets maximum. Leur checksum est une somme additive sur 16 bits, pas une authentification. L'émetteur périodique diagnostic renseigne explicitement surtout uptime, heap et RSSI ; la présence des autres champs ne garantit pas leur alimentation.

Les messages historiques sont `MSG_SENSOR_VALUE`, `MSG_ACTUATOR_COMMAND`, `MSG_HEARTBEAT`, `MSG_CONFIG_SYNC`, `MSG_STATUS`, `MSG_ERROR`. La réception de `MSG_CONFIG_SYNC` ne réalise pas de synchronisation de configuration.

### Import et autorisation

La découverte alimente un inventaire temporaire ; elle ne crée pas de capteur configuré. Les mesures génériques exigent une MAC source autorisée dans les peers et un mapping configuré côté destination. L'export côté source est volontaire (`espNowExportEnabled`) et nécessite les peers destinataires.

Le bouton **Ajouter**, suivi de la sauvegarde, crée le mapping : `source = espnow`, `mac`, `sensorId`, type métier, rôle et `remoteKey`. `ALL` importe les valeurs du capteur ; une clé précise restreint l'import. Les métadonnées d'un capteur déjà configuré peuvent être resynchronisées depuis la source : ce n'est pas un import de nouveau capteur.

Les valeurs génériques passent par SensorManager puis RuntimeState et sont exposées avec `origin = ESP-NOW`. Exception historique : `MSG_SENSOR_VALUE` et certains `MSG_STATUS` écrivent directement dans RuntimeState, sans les mêmes garanties de mapping et d'expiration.

### Réglages présents

Les intervalles du transport sont sous `system.json -> espnow`. Les paramètres heartbeat/takeover sont à la racine de `system.json`.

| Réglage | Défaut | Bornes dans le gestionnaire |
|---|---:|---|
| `discoveryIntervalMs` | 3 000 ms | 1 000 à 30 000 ms |
| `sensorDiscoveryIntervalMs` | 30 000 ms | 10 000 à 120 000 ms ; capteurs et actionneurs |
| `diagnosticIntervalMs` | 10 000 ms | 5 000 à 60 000 ms |
| `heartbeatIntervalMs` | 300 ms | Relu par RedundancyManager |
| `takeoverTimeoutMs` | 1 000 ms | Attente de bascule supplémentaire de 50 ms |

EspNowManager réserve 12 nœuds découverts, 16 actionneurs découverts et 12 emplacements de suivi des exports. Ce ne sont pas les limites de la table des peers radio.

Délais de base des mesures distantes : JSY 5 s, Linky 15 s, DS18B20 30 s. Le délai effectif peut être relevé à trois intervalles d'export ou au timeout explicite (`remoteTimeoutMs` / `espNowTimeoutMs`) ; ces paramètres ne réduisent pas le délai de base.

Les peers utilisent le canal radio courant (`channel = 0`). Pour diagnostiquer une absence de réception, relever les MAC STA, le canal Wi-Fi effectif, les peers et la version de protocole des deux ESP. Voir un nœud ne valide pas le mapping de ses valeurs.

### API présentes dans le main

Ces routes passent par `requireAuth()` et suivent la configuration d'authentification Web.

| Méthode et route | Usage |
|---|---|
| `GET /api/espnow` | État local, peers et inventaire |
| `GET /api/espnow/discovery` | Nœuds découverts |
| `POST /api/espnow/discovery/announce` | Annonce immédiate du nœud |
| `POST /api/espnow/sensors/announce` | Annonce immédiate des capteurs et actionneurs exportés |
| `POST /api/espnow/peer` | Ajout explicite ; argument formulaire `mac` |
| `POST /api/espnow/peer/remove` | Retrait explicite ; argument formulaire `mac` |
| `POST /api/espnow/config` | Arguments formulaire `debugTransmission` et `debugReception` |

Les capteurs sont aussi exposés dans `/api/state` (alias `/api/status-lite`) et `/api/status`. La configuration passe par les endpoints capteurs existants, sans import implicite.

### Défauts prioritaires ouverts

Ces scénarios sont déduits du code, sans validation radio sur matériel pendant cette relecture.

- **TTL** : `acceptActuatorCommand()` soustrait le timestamp de l'émetteur au `millis()` du récepteur. Les boots ne sont pas synchronisés : une commande fraîche peut être refusée. Définir une expiration fondée sur la réception locale et une politique contre les commandes retardées/rejouées.
- **Arbitrage** : une commande distante reçue peut être écrasée par le cycle suivant de l'automate local. La propriété et la durée de validité d'une commande doivent être explicites.
- **Simulation** : l'envoi de commandes actionneurs distantes n'est pas inhibé ; un nœud resté en réel peut appliquer une commande simulée.
- **Capteurs historiques** : aligner les écritures directes dans RuntimeState sur les contrôles SensorManager en préservant la compatibilité.
- **Callbacks** : réception, chaînes, logs et états partagés sont traités dans le callback radio. Une file bornée vers la loop et la maîtrise de la concurrence restent à étudier avant d'augmenter les cadences.
- **Âges** : la correction de `updateDiscoveredNodeLoss()` ne couvre pas `isNodeOk()` ni tous les rendus JSON, qui conservent des gardes `now >= lastSeenMs`. Tester le rebouclage d'horloge et les réceptions concurrentes sur ces chemins aussi.

Le OFF local à l'entrée simulation est maintenant présent ; la simulation distante reste ouverte. Voir la réévaluation des constats 1, 5, 6 et 7 dans l'[audit courant](AUDIT_2026-10-20.md). La redondance a ete retiree le 21 octobre 2026.

## Validation sur banc

1. Deux ESP en v4 : vérifier discovery, versions et canal ; sans autorisation ni import, aucune mesure ne doit devenir source de régulation.
2. Autoriser la MAC sans importer, puis importer explicitement JSY, Linky et température ; vérifier origine, clés, unités, rôles et absence de doublon au reboot.
3. Couper puis rétablir la source : vérifier expiration effective, état perdu et réaction Safety/PID. Supprimer le mapping et vérifier qu'il ne se recrée pas.
4. Tester sur sorties basse tension : boots décalés, pair non autorisé, TTL dépassé et Safety CRITICAL.
5. Avec deux ESP mis a jour sans roles, verifier les commandes entre pairs autorises, la perte/reprise du lien et l'absence de bascule automatique.
6. Entrer en simulation avec un actionneur distant resté en réel : vérifier coupure et absence de commande simulée non nulle.
7. Observer heap, pertes, loop et latence `/api/state`, avec debug désactivé puis activé temporairement.

L'annonce de partition, la synchronisation de configuration et la maintenance distante restent des évolutions à valider séparément. Aucun workflow d'update distant n'est garanti par la présence d'un message ou d'un champ.

## Expiration en MULTI_SINUS — 3 novembre 2026

Pour un SSR local en `MULTI_SINUS`, ActuatorManager publie l’échéance de la commande acceptée au moteur temporel. Le timer peut donc couper le GPIO à expiration pendant un retard de la boucle principale. Le délai et la priorité des commandes restent soumis à l’arbitrage du propriétaire physique. Voir le [guide SSR](MULTI_SINUS.md).
