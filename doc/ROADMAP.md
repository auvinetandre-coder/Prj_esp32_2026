# ROADMAP.md - Feuille de route RouteurSolaireESP32

Relecture du 20 octobre 2026 : les lots de [TODO_PRIORITES.md](TODO_PRIORITES.md) précèdent ces extensions. Consulter le [bilan](ETAT_DU_PROJET.md) et l'[audit](AUDIT_2026-10-20.md) pour distinguer code présent, défaut ouvert et validation. Cette feuille de route ne décide aucune refonte.

## Ordre de travail courant

1. Anciennes sorties, priorité système OFF et autorité locale/distante.
2. Configuration transactionnelle et maintenance protégée avec préservation vérifiée.
3. TTL/heartbeat/redondance et traitements réseau bornés.
4. Décisions par actionneur, unités explicites, persistance maîtrisée et contrats API.
5. Dashboard fidèle aux mesures/états ; puis rôles séparés avec migration, bilans énergétiques persistants et supervision du parc.

Déjà présents : AutomateManager, éditeur Web de règles, commande manuelle Web maintenue, routage Linky et production MQTT. Il faut les stabiliser et les tester sur appareil ; ne pas les recréer. Les rubriques P1 à P10 ci-dessous conservent le cadrage par domaine.

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

- clarifier le role d'AutomateManager ;
- centraliser les automatismes non bloquants ;
- garder SafetyManager prioritaire ;
- conserver RuleEngine, SolarRouter et PIDController comme modules metier distincts ;
- rendre les automatismes observables via logs, RuntimeState ou API.

---

## P5 - Dashboard cockpit energie

- afficher tous les capteurs via SensorManager ;
- corriger les mappings ESP-NOW ;
- afficher achat, injection, routage et temperature equipement ;
- ajouter graphes PID ;
- fiabiliser personnalisation dashboard ;
- garder HTML/CSS/JavaScript natif.

---

## P6 - ESP-NOW propre

- discovery enrichie ;
- versions firmware/LittleFS ;
- roles redondance et roles fonctionnels ;
- capteurs/actionneurs exportes ;
- import manuel robuste ;
- diagnostic heartbeat ;
- aucun transport de binaire OTA via ESP-NOW.

---

## P7 - Redondance MASTER/BACKUP

- clarification MASTER/BACKUP/STANDALONE ;
- detection double MASTER ;
- supervision BACKUP ;
- strategie de bascule ;
- synchronisation config uniquement si validee.

---

## P8 - PID et routage

- parametres PID propres ;
- affichage erreur/sortie ;
- anti-oscillation ;
- rampes ;
- deadband ;
- delta-sigma ou vol de cycles si necessaire.

---

## P9 - Capteurs/actionneurs

- JSY local et ESP-NOW ;
- Linky local et ESP-NOW ;
- DS18B20 local et ESP-NOW ;
- SSR ;
- relais ;
- RobotDyn ;
- actionneurs distants.

---

## P10 - Industrialisation

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
- reduction LittleFS sans mesure prealable.


Les conditions calendaires, variables utilisateur, simulation complète de règle et imbrication limitée à deux niveaux restent des extensions de l'automate après stabilisation ; voir [ROADMAP_AUTOMATE.md](ROADMAP_AUTOMATE.md).
