# Suppression des roles — 21 octobre 2026

## Fonctionnement retenu

Un firmware commun pour tous les ESP32. Chaque appareil execute ses capteurs,
actionneurs, regles et PID configures. Aucun role de module, election, veille,
bascule automatique ou detection de double MASTER.

Le demarrage sur ota_0, le passage explicite en factory recovery, les imports
manuels ESP-NOW et les protections locales de SafetyManager sont conserves.
L'onglet Secours est retire. Les outils embarques de maintenance restent accessibles.

## Migration et contrat API

- device.json : suppression de role, redundancyRole et functionalRole.
- system.json : suppression de heartbeatIntervalMs, takeoverTimeoutMs, redundancy
  et des trois champs de roles si presents.
- Migration au chargement, y compris apres restauration d'une sauvegarde. Ecriture
  dans un fichier temporaire, controle de taille puis renommage. Aucun reset usine.
  Un echec d'ecriture est signale en serie et loadAll retourne false.
- Les sauvegardes et imports API retirent egalement ces champs avant validation.
- Les anciennes cles ne sont plus exposees dans les API d'etat, les diagnostics
  Web ou la publication d'etat MQTT. Ce changement de contrat est volontaire,
  demande explicitement par l'utilisateur ; adapter les clients externes.
- Les sauvegardes historiques ne sont pas effacees. Leur restauration repasse par
  le nettoyage avant utilisation de la configuration active.
- Les usages des capteurs/actionneurs (grid, production, temperature_1...) sont
  conserves : ils servent a identifier les mesures et les sorties.
- Les anciennes regles utilisant Systeme.role ou Redondance ne sont plus valides
  et ne s'executent pas, y compris leur branche SINON. Les corriger explicitement
  dans l'editeur ; ne jamais supprimer une condition pour rendre la regle vraie.

## ESP-NOW

Les services annonces sont deduits de la configuration active. serviceFlags
remplace roleFlags dans les structures et API. Les anciens octets de redondance
et d'autorite des trames deviennent reserves, sans modifier leur taille ni les
offsets radio v4. Les capteurs restent importes manuellement.

Les commandes exigent un pair autorise, un checksum valide, le controle TTL
existant et l'autorisation locale de securite. Elles ne dependent plus d'un
MASTER, d'un heartbeat ou d'un epoch. Aucun arbitrage automatique entre plusieurs
emetteurs : configurer explicitement les pairs autorises. Mettre a jour ensemble
les appareils qui echangent des commandes ; les anciens firmwares peuvent
refuser les nouvelles commandes sans metadonnees de MASTER.

Le controle TTL existant compare les uptimes emetteur/recepteur ; sa limitation
preexistante reste a corriger dans un lot distinct. Les tests locaux ne valident
ni la radio reelle ni la robustesse de LittleFS face a une coupure electrique.

## Fichiers concernes

- RouteurSolaireESP32.ino : retrait de RedundancyManager et de sa tache.
- src/communication/redundancy_manager.* : supprimes.
- src/runtime/runtime_state.* : retrait des roles et etats de redondance.
- src/config/config_manager.* : migration ciblee et validation sans role requis.
- src/communication/espnow_* et mqtt_manager.cpp : annonces, API et commandes.
- examples/EspNowSensorSender : services radio sans roles de module.
- src/actuators/actuator_manager.cpp et src/safety/safety_manager.* : commande
  selon la configuration et les protections locales, retrait des defauts MASTER.
- src/logic/rule_engine.cpp : retrait des mesures de roles/redondance.
- src/web/web_ui.* et data/www/{app.js,index.html} : suppression complete des
  champs, selecteurs, badges, colonnes et adaptations PID selon le role.
- config_examples/device.example.json et system.example.json : exemples nettoyes.
- src/build_info.h et data/www/littlefs_version.txt : versions 20261021-01.
- README.md, ARCHITECTURE.md et notices documentaires : nouveau fonctionnement.
- tools/test_role_removal.js : migration, preservation des reglages, API et rendu.

## Verification locale

Compilation finale ESP32 reussie (core 3.3.12, partition Recovery/Main) :
1 453 480 octets de programme sur 1 966 080 (73 %) et 71 320 octets de RAM
statique sur 327 680 (21 %). Firmware 20261021-01, marqueur verifie dans le binaire.
Sortie : build/ota/RouteurSolaireESP32_firmware_20261021-01.bin. Aucun flash realise.

Syntaxe JavaScript valide. Les huit scripts tools/test_*.js passent.
Le nouveau test execute le corps de nettoyage C++ adapte en JavaScript et rend
le dashboard INIT/OK/CRITICAL, les parametres, ESP-NOW et Systeme avec des donnees
historiques. Il ne constitue pas un test du firmware sur appareil.

## Tests sur appareil a realiser

1. Exporter les cinq JSON. Installer explicitement le firmware et la WebUI par
   le parcours recovery habituel, en preservant la configuration.
2. Demarrer un appareil precedemment BACKUP ou NODE_SENSOR. Verifier la disparition
   des cles obsoletes dans les exports device/system, puis redemarrer et verifier
   que tous les autres reglages sont identiques.
3. Ouvrir Dashboard, Parametres, Systeme et ESP-NOW ; verifier l'absence des roles
   et de l'onglet Secours, y compris apres rechargement du navigateur.
4. Sur banc basse tension, tester OFF/manuel/AUTO, PID, perte d'un capteur critique,
   temperature excessive et simulation : les coupures locales doivent fonctionner.
5. Avec deux appareils mis a jour, verifier decouverte, import manuel des capteurs,
   commande d'un actionneur par un pair autorise et refus d'un pair non autorise.
   Verifier les commandes apres boots decales compte tenu du TTL historique.
6. Verifier reboot normal en ota_0 et passage en recovery seulement sur demande.
