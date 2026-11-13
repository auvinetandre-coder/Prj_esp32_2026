# ROADMAP.md - Feuille de route RouteurSolaireESP32

Relecture du 4 novembre 2026. Les [priorités de stabilisation](TODO_PRIORITES.md) précèdent les extensions. L'[audit courant](AUDIT_2026-11-04.md) indique les corrections et les critères de réception.

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
- conserver RuleEngine et PIDController comme modules metier distincts ;
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
- capteurs/actionneurs exportes ;
- import manuel robuste ;
- diagnostic heartbeat ;
- aucun transport de binaire OTA via ESP-NOW.

---

## P7 - Validation du contrôle distribué

Valider les commandes partagées, les expirations et la perte/reprise du lien sur deux ESP. La redondance par rôle a été retirée le 21 octobre.

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


## Extensions de l'automate après stabilisation

Consolidation documentaire du 11 novembre 2026. Le PID commun, les décisions par actionneur, OFF et les commandes partagées sont décrits dans [PID unique](PID_UNIQUE.md) et l'[audit du 4 novembre](AUDIT_2026-11-04.md). Leur présence ne remplace pas les essais sur banc.

- Stabiliser l'éditeur SI / ALORS / SINON existant et les conditions plates ET/OU avant les extensions.
- Étudier conditions calendaires avec heure valide, variables virtuelles utilisateur, imbrication limitée à deux niveaux et simulation complète de règle.
- Conserver RuleEngine et PIDController comme producteurs de propositions, AutomateManager pour l'arbitrage, SafetyManager prioritaire et ActuatorManager pour l'application physique.
- Publier les transitions et l'état automate dans RuntimeState, l'API et les diagnostics, sans dépendance du recovery vers l'automate.
- Examiner RECOVERY_REQUEST et préserver la demande explicite de passage en factory.

Les rôles MASTER/BACKUP et leur redondance, retirés le 21 octobre, ne font plus partie de la feuille de route. Les critères de réception sont centralisés dans [les priorités](TODO_PRIORITES.md).
