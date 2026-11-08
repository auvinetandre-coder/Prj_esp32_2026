# Feuille de route AutomateManager / RuleEngine

## Statut — 4 novembre 2026

Le PID commun, les décisions par actionneur, la priorité système OFF et les commandes radio partagées sont présents. Les [priorités automate](TODO_PRIORITES_AUTOMATE.md) et l'[audit](AUDIT_2026-11-04.md) priment sur le cadrage historique qui suit.

## P1 - Stabilisation Recovery/Main

- valider le partitionnement `factory` / `ota_0` / LittleFS ;
- verifier `upload.maximum_size = 1966080` pour le main ;
- garder le recovery sous `0x110000` ;
- garder le main sous `0x1E0000` ;
- valider le cycle boot `factory -> ota_0`, puis reboot normal sur `ota_0` ;
- garantir que l'update firmware ecrit uniquement dans `ota_0` ;
- verifier que `/config` et NVS ne sont pas effaces.

---

## P2 - BootManager

- documenter les partitions courantes et de boot ;
- fiabiliser `setNextBootFactory()` et `setNextBootMain()` ;
- exposer les informations utiles a la WebUI Systeme ;
- verifier que le main ne passe en `factory` que sur demande recovery explicite ;
- garder le recovery comme point de maintenance permanent.

---

## P3 - Recovery minimal

- conserver AP secours et acces Wi-Fi local si possible ;
- garder une page Web simple et legere ;
- upload local `firmware.bin` vers `ota_0` ;
- conserver ou valider l'upload LittleFS local ;
- exclure ESP-NOW, MQTT, GitHub OTA, HTTPS, dashboard complet et logique metier ;
- surveiller la taille du recovery.

---

## P4 - Firmware principal et AutomateManager

- stabiliser `AutomateManager` deja present dans le firmware principal uniquement ;
- clarifier son role de machine d'etat centrale ;
- definir les etats `BOOT`, `INIT`, `IDLE`, `AUTO_ROUTING`, `MANUAL_FORCE`, `SAFETY`, `ERROR`, `RECOVERY_REQUEST` ;
- centraliser l'arbitrage entre securite, erreur, recovery, manuel, regles et routage automatique ;
- garder `SafetyManager` prioritaire sur toutes les commandes ;
- conserver `RuleEngine`, `SolarRouter` et `PIDController` comme modules metier distincts ;
- faire passer la commande finale normalisee par `AutomateManager` avant `ActuatorManager` ;
- publier l'etat automate dans `RuntimeState` et `/api/state` si simple ;
- rendre les transitions observables via logs, RuntimeState ou API ;
- ne creer aucune dependance du recovery vers AutomateManager.

---

## P5 - RuleEngine configurable WebUI

- stabiliser le moteur et l'editeur de regles utilisateur deja presents ;
- permettre a terme des regles `SI / ALORS / SINON` ;
- supporter conditions capteurs, conditions systeme et conditions temporelles ;
- supporter groupes `ET / OU` ;
- supporter l'imbrication de `SI` limitee a 2 niveaux ;
- supporter variables virtuelles utilisateur ;
- supporter actions multiples ;
- prevoir un mode simulation de regle depuis la WebUI ;
- faire produire au RuleEngine une proposition, jamais une commande directe actionneur ;
- laisser `AutomateManager` arbitrer toutes les actions proposees ;
- garantir qu'aucune regle ne contourne `SafetyManager`.

---

## P6 - Dashboard cockpit energie

- afficher tous les capteurs via SensorManager ;
- corriger les mappings ESP-NOW ;
- afficher achat, injection, routage et temperature equipement ;
- afficher l'etat AutomateManager quand disponible ;
- ajouter graphes PID ;
- fiabiliser personnalisation dashboard ;
- garder HTML/CSS/JavaScript natif.

---

## P7 - ESP-NOW propre

- discovery enrichie ;
- versions firmware/LittleFS ;
- roles redondance et roles fonctionnels ;
- capteurs/actionneurs exportes ;
- import manuel robuste ;
- diagnostic heartbeat ;
- aucun transport de binaire OTA via ESP-NOW.

---

## P8 - Redondance MASTER/BACKUP

- clarification MASTER/BACKUP/STANDALONE ;
- detection double MASTER ;
- supervision BACKUP ;
- strategie de bascule ;
- synchronisation config uniquement si validee.

---

## P9 - PID et routage

- parametres PID propres ;
- affichage erreur/sortie ;
- anti-oscillation ;
- rampes ;
- deadband ;
- delta-sigma ou vol de cycles si necessaire ;
- faire produire au PID une proposition de commande, pas une application directe.

---

## P10 - Capteurs/actionneurs

- JSY local et ESP-NOW ;
- Linky local et ESP-NOW ;
- DS18B20 local et ESP-NOW ;
- SSR ;
- relais ;
- RobotDyn ;
- actionneurs distants ;
- garder `ActuatorManager` comme seul pilote materiel.

---

## P11 - Industrialisation

- scripts de compilation main et recovery ;
- scripts de televersement explicites ;
- logs diagnostics ;
- inventaire ESP ;
- supervision parc ;
- documentation tenue a jour.

---

## Hors feuille de route actuelle

- GitHub OTA cote ESP32 ;
- HTTPS dans le recovery ;
- retour au double slot OTA `ota_0` / `ota_1` ;
- logique metier dans le recovery ;
- dependance recovery vers AutomateManager ;
- moteur de regles complet avant stabilisation AutomateManager ;
- reduction LittleFS sans mesure prealable.
