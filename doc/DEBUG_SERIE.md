# Diagnostic serie et watchdog de connectivite — 8 novembre 2026

## Commandes USB/Web unifiees — 20261111-03

Les commandes avancees sont desormais disponibles sur USB et dans la console Web, via le meme moteur. `debug now` devient un alias de `debug status`. `wifi` affiche le bilan Wi-Fi ; `wifi history` relit les evenements sauvegardes dans LittleFS. Voir [commande, contenu et persistance](WIFI_DIAGNOSTIC.md). Les parties datees de novembre ci-dessous decrivent les anciens lots.

## Commandes

Firmware principal, moniteur serie **115200 bauds**. Selectionner une fin de
ligne Nouvelle ligne, Retour chariot ou les deux. Les commandes sont
insensibles a la casse ; espaces aux extremites acceptes.

| Commande | Effet |
|---|---|
| `debug on` | Active un bilan immediat puis toutes les 5 secondes. |
| `debug off` | Arrete le bilan et annule le rapport en cours. |
| `debug status` | Un bilan ponctuel, sans activer le mode periodique. |
| `debug events` | Les 32 dernieres transitions/lenteurs gardees en RAM, une seule fois. |
| `debug peers` | Etat des pairs ESP-NOW configures, une seule fois (20 maximum). |
| `debug help` | Rappelle les commandes. |

Le diagnostic revient sur OFF au redemarrage. Les logs existants sont conserves,
y compris apres `debug off`. Aucune commande ne pilote une sortie, ne redemarre
la carte ou ne modifie `/config`. Aucun export de configuration, SSID, mot de
passe, identifiant MQTT ou jeton n'est ajoute.

## Diagnostic avance — firmware 20261108-02

L'historique numerique en RAM enregistre, meme avec debug OFF :

- changements d'etat WiFi et du compteur de deconnexion (dernier motif) ;
- changement de canal WiFi ;
- resultat et duree de la derniere tentative MQTT observee ;
- transitions disponible/indisponible JSY et TIC ;
- module lent au-dessus du seuil existant (50 ms par defaut), au maximum
  une entree par seconde et par module.

`debug events` prend une copie chronologique des 32 dernieres entrees : la
capture affichee reste stable pendant que le programme continue a enregistrer.
La surveillance des transitions est echantillonnee toutes les 100 ms dans la
boucle principale ; elle ne pretend pas enregistrer toutes les transitions
breves ni tous les paquets radio. Les ralentissements sont captures au retour
des modules de la boucle ; un gel total ne peut pas produire sa propre trace.

Les nouvelles traces **ne sont jamais ecrites dans LittleFS**. Aucun fichier
tournant n'est cree et `/config` n'est pas modifie par le diagnostic. Les deux
anneaux fixes de 32 entrees (historique et copie de lecture) occupent environ
1 Ko de RAM, plus les petits tableaux de durees ; ni liste illimitee ni
allocation dynamique par entree. L'historique est perdu au reboot/coupure.
La cause du reboot watchdog reste annoncee separement via son marqueur RTC.

`debug peers` montre la MAC configuree, son enregistrement dans le pilote,
le canal et l'interface d'emission ainsi que l'age de sa derniere trame connue.
Canal 0 signifie canal radio courant, interface 0 STA, interface 1 AP.
`seen=0` signifie aucune reception connue : ignorer alors age et valeurs
initiales. Un pair enregistre n'est pas une preuve de sa joignabilite.

Le bilan SYS annonce aussi le motif du dernier reset (POWER_ON, SOFTWARE,
PANIC, TASK_WDT, BROWNOUT...). PERF distingue `maxBoot` (depuis demarrage)
et `recentMax` (depuis le dernier debut de bilan, premier bilan depuis boot).
Le PID etant mesure dans son module interne, recentMax y est explicitement
N/A ; sa moyenne et son maximum depuis boot restent disponibles.

Tests logiciels supplementaires : `node tools/tests/test_serial_trace.js` couvre
la borne de 32 entrees, l'ordre, la stabilite de la copie de lecture, le seuil
et la limitation des traces lentes ainsi que le debordement millis.
Le test de la console couvre aussi les nouvelles commandes ponctuelles.
Les tests radio de vivacite et du watchdog de connectivite sont repasses.

Validation du lot avance : compilation Main 20261108-02 reussie (core ESP32
3.3.12), programme 1523164 / 1966080 octets (77 %), variables globales
85792 / 327680 octets (26 %), soit 1208 octets de RAM globale de plus que
20261108-01. Suite complete : 26 scripts reussis sur 27 ; seul l'echec
preexistant `test_generic_routing.js` reste present. Aucun essai sur carte
ni televersement realise. Le watchdog de connectivite conserve sa condition ET.

Pour recuperer l'historique : laisser le moniteur serie ouvert a 115200 bauds,
envoyer `debug events` avec une fin de ligne et copier les lignes `[TRACE ...]`.
Ne pas redemarrer la carte avant la lecture. `debug off` arrete l'affichage
periodique mais laisse l'enregistrement RAM borne actif. Aucune lecture brute
de toute la RAM ni connexion Web n'est necessaire.

Les binaires courants du projet sont toujours livres dans :

- `build/release/firmware.bin`, avec le manifeste `version.json` actualise ;
- `build/ota/RouteurSolaireESP32_firmware.bin` et sa copie versionnee ;
- `build/esp32.esp32.esp32/RouteurSolaireESP32.ino.bin`.

LittleFS reste en 20261106-15 pour ces livraisons firmware seules. Les images
versionnees anterieures sont conservees. Le dossier `build_audit/serial-debug`
est un espace de compilation, pas l'emplacement utilisateur de livraison.

## Lecture du bilan

Chaque ligne `[DBG uptime_ms][categorie]` donne l'etat lu a cet instant ;
le rapport progressif n'est pas un snapshot atomique.

- SYS/MEM/LOOP : version, uptime, heap libre/minimale, plus grand
  bloc allouable (indicateur de fragmentation), moyenne/max de boucle.
- WIFI : statut numerique Arduino, connexion effective, mode, canal, RSSI,
  IP STA/AP, compteur, motif numerique et age de derniere deconnexion.
  L'age n'est significatif que si `disconnects` est non nul.
- MQTT : etat, tentatives/echecs, duree derniere/max de `connect()`, resultat.
  La duree mesure l'appel de connexion, pas la periode entre les tentatives.
- ESPNOW : reception brute, callbacks d'emission OK/echec, tentatives de
  decouverte, dernier retour immediat numerique et nom ESP-IDF.
  Un envoi accepte ne prouve pas la reception applicative par le pair ; un
  callback broadcast OK n'est pas un accuse de reception de tous les pairs.
- JSY/TIC/DS18B20 : disponibilite, age derniere lecture, erreurs, mesures.
  Si `lastValid=0`, l'age n'est pas une preuve de lecture ; `nan` est indisponible.
- GRID : origine choisie, puissance et age de derniere mesure radio globale.
  Les donnees viennent de RuntimeState, alimente par SensorManager.
- SAFETY/WEB/PERF : securite, LittleFS, automate, cout construction `/api/state`,
  compteurs et durees moyennes/max par module (depuis demarrage).
- WATCHDOG : WiFi attendu, nombre de pairs valides configures, durees sans
  WiFi et sans reception d'un pair configure.

Les compteurs restent actifs meme diagnostic OFF pour garder l'historique de
la panne. La reception brute inclut les paquets rejetes ; seuls les pairs
reconnus par le chemin ESP-NOW existant alimentent la surveillance de liaison.

## Bornes et performances

Reception limitee a 64 caracteres par boucle, commande de 47 caracteres max.
Une ligne trop longue/invalide est integralement rejetee jusqu'a sa fin ;
aucun suffixe n'est interprete comme commande. Pas d'attente d'une ligne
complete, de `readString`, `flush` ou `delay` dans le diagnostic.

Emission d'une ligne de 192 octets maximum par boucle, seulement si le buffer
TX a la place. Le buffer UART0 est configure a 512 octets avant Serial.begin.
En cas de saturation, le rapport attend sans bloquer volontairement la boucle.
Les chemins historiques de logs restent inchanges et peuvent encore ralentir
le programme. Les nouvelles callbacks WiFi n'effectuent ni impression ni
allocation de String. RAM additionnelle fixe pour la console et le buffer TX.

## Watchdog : WiFi ET tous les pairs ESP-NOW perdus

Instruction utilisateur du 8 novembre : la condition finale est **ET**, pas OU.
Redemarrage si le WiFi attendu est deconnecte depuis au moins **300000 ms**
ET si aucun pair ESP-NOW configure n'a ete entendu depuis au moins **300000 ms**.
Le retour du WiFi ou une trame d'un seul pair configure annule la condition.
Les deux durees sont suivies independamment, sans ajouter le timeout de 30 s
de presentation des noeuds au delai de cinq minutes.

Sans identifiants WiFi renseignes, ou sans pair unicast valide dans
`system.peers`, ce redemarrage est inactif. Les annonces d'un inconnu ne
maintiennent pas le watchdog en vie. Un changement de liste de pairs redonne
une grace complete ; aucune ecriture de configuration n'est necessaire.
Une panne totale durable peut donc causer un redemarrage toutes les cinq
minutes environ, plus le temps de boot.

Avant ESP.restart, les sorties sont verrouillees et la coupure existante
`forceAllOff()` est appliquee. Les commandes OFF radio sont au mieux : une
liaison perdue ne garantit pas leur livraison ; les baux/protections du pair
restent necessaires. Le boot reste celui du Main (`ota_0`), sans OTA ni passage
recovery automatique. Un marqueur RTC signale la cause au boot suivant, sans
ecriture NVS. Le debug peut rester OFF : le watchdog fonctionne independamment.

Ce watchdog de connectivite est coopératif : il ne peut pas recuperer un gel
total de la boucle. Le `softwareWatchdog()` historique reste present ; cela
ne constitue pas un watchdog materiel independant. La regulation et les
protections electriques ne doivent pas dependre du delai de cinq minutes.

## Validation

Test logiciel : `node tools/tests/test_connectivity_watchdog.js` (predicat C++ reel,
frontiere exacte, ET, liens non configures, retours, debordement millis).
`node tools/tests/test_serial_debug.js` execute les chemins commandes/RX/TX du C++
avec un adaptateur JavaScript : fins de ligne, commande partielle, casse,
annulation par OFF, debordement, caracteres invalides, budget RX, saturation
TX, lignes completes et periode de cinq secondes avec debordement millis.
La compilation ESP32 valide l'integration et les API du core installe.

Regression locale du 8 novembre : 25 scripts passent sur 26. Le test
`test_generic_routing.js` echoue sur l'interdiction textuelle de `tank` dans
`data/www/app.js` (ligne 3966). Le meme motif est present dans la sauvegarde
Git `be3b3a6` ; ce fichier n'est pas modifie par ce lot. Cet echec preexistant
reste ouvert et n'est pas masque par une modification du test.

Compilation Main `20261108-01` reussie sur ESP32 Arduino core 3.3.12,
partition `partitions_recovery_main_4MB` : programme 1520296 / 1966080 octets
(77 %), variables globales 84584 / 327680 octets (25 %). Binaire dans
`build/ota/RouteurSolaireESP32_firmware_20261108-01.bin` ; version embarquee
verifiee. Aucun flash ni essai de perte reseau sur carte realise dans ce lot.

Sur banc, sorties securisees :

1. Verifier OFF au boot, `debug on`, `debug off`, `debug status`, `debug help`.
2. Tester CR/LF/CRLF, casse, commande partielle, ligne >47 caracteres puis
   commande correcte ; OFF interrompt aussi un bilan en cours.
3. Saturer l'entree serie/verifier que capteurs, Web et Safety restent servis.
4. WiFi perdu seul pendant >5 min avec un pair radio actif : aucun reboot.
5. Tous les pairs perdus seuls pendant >5 min avec WiFi actif : aucun reboot.
6. Les deux perdus : reboot apres cinq minutes communes (precision du controle
   1 s, plus tout blocage preexistant), sorties OFF et cause visible au boot.
7. Retablir chaque liaison juste avant expiration : pas de reboot ; reperdre
   le lien doit recommencer son compteur. Pair inconnu seul : pas de remise a zero.
8. Aucun pair configure / aucun WiFi configure : pas de reboot de connectivite.

Pas de validation materielle presumee. Pas de remplacement LittleFS requis
pour cette fonctionnalite firmware ; sauvegarder la configuration avant toute
operation de maintenance et installer via recovery explicite.
