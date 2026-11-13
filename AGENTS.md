# AGENTS.md — Règles Codex du projet RouteurSolaireESP32

## Mission

RouteurSolaireESP32 est un routeur solaire ESP32 distribué, local-first.
Le firmware principal est unique pour tous les ESP32. Chaque appareil fonctionne selon ses capteurs, actionneurs, règles, PID et paramètres locaux, sans rôle de module ni redondance MASTER/BACKUP.

Priorités : stabilité, sécurité, maintenabilité, performance, évolutivité, dans cet ordre.

---

## Règles absolues

- Ne jamais refondre le projet sans demande explicite.
- Ne jamais supprimer une fonctionnalité existante sans validation.
- Ne jamais casser une API existante sans justification documentée.
- Ne jamais écraser `/config`.
- Ne jamais effacer NVS sans demande explicite.
- Ne jamais faire de mise à jour automatique silencieuse.
- Ne jamais importer automatiquement des capteurs ESP-NOW.
- Ne jamais ajouter de framework Web lourd.
- Toujours préserver le passage explicite en recovery pour les mises à jour.
- Toujours préserver le recovery en `factory` ; une mise à jour du Main ne doit jamais l’écraser.
- Toujours écrire le firmware principal uniquement dans `ota_0`.
- Préserver les contrats API et WebUI existants, sauf évolution explicitement demandée et documentée.
- Migrer les anciens champs de configuration sans perdre les réglages conservés ; publier le schéma courant. Ne pas réintroduire les champs de rôles et de redondance supprimés.
- Mettre à jour ensemble firmware et WebUI lorsque leur contrat évolue.
- Toujours travailler par petites étapes testables.
- Toujours expliquer les fichiers modifiés et les tests réalisés ou restant à faire.

---

## Firmware principal unique et configuration des équipements

Le firmware principal est commun à tous les ESP, distinct du firmware recovery minimal.
Le comportement dépend de la configuration locale :

- capteurs activés ;
- actionneurs activés ;
- règles ;
- PID ;
- réseau.

Ne pas créer plusieurs firmwares principaux spécialisés sauf demande explicite.

Les rôles de module et la redondance sont supprimés depuis le 21 octobre 2026 sur demande utilisateur. Les usages des capteurs et des actionneurs (réseau, production, température, etc.) sont conservés et ne sont pas des rôles de module.
Voir `doc/SUPPRESSION_ROLES.md` et `doc/ROUTAGE_GENERIQUE.md`.

---

## SensorManager

SensorManager est la source de vérité des capteurs.
Tous les capteurs doivent être exposés de manière uniforme : locaux, ESP-NOW, virtuels et futurs.
Cette règle d’architecture n’autorise pas à réintroduire des types retirés du catalogue ; voir `doc/CATALOGUE_CAPTEURS.md`.

Un capteur ESP-NOW doit être vu comme un capteur normal avec une origine `ESP-NOW`.
Le dashboard, les règles, l’API et les diagnostics doivent consommer SensorManager ou l’état global.

---

## ESP-NOW

ESP-NOW sert à :

- discovery ;
- heartbeat ;
- annonce des capacités ;
- annonce de capteurs et d’actionneurs ;
- remontée de mesures ;
- diagnostics.

ESP-NOW ne doit jamais transporter un firmware `.bin`.
Une éventuelle notification ou demande de maintenance ne doit déclencher ni téléchargement ni installation automatique.
Le parcours de mise à jour retenu est le passage explicite en factory recovery, puis l’upload local des fichiers par l’utilisateur.

Import des capteurs ESP-NOW : manuel uniquement.

---

## Dashboard

Le dashboard doit rester léger : HTML, CSS, JavaScript natif, SVG et Canvas.

Interdits sauf demande explicite : React, Vue, Angular, gros frameworks et bibliothèques externes inutiles.

Objectifs UX :

- comprendre l’état énergétique en moins de 2 secondes ;
- diagnostiquer un problème en moins de 10 secondes ;
- régler le PID en moins de 30 secondes.

Le dashboard ne doit pas contenir de logique métier spécifique ESP-NOW.
ESP-NOW est une origine, pas un type de capteur.

---

## Recovery et mises à jour

Architecture à préserver :

```text
factory = recovery minimal
ota_0   = firmware principal
spiffs  = partition utilisée par LittleFS
```

Le projet gère séparément `firmware.bin` et `littlefs.bin`.
Les mises à jour firmware et LittleFS se font depuis la factory recovery, pas depuis le firmware principal.

Règles :

- action utilisateur obligatoire ;
- pas de mise à jour automatique silencieuse ;
- firmware et LittleFS séparés ;
- le firmware principal reste sur `ota_0` lors d’un redémarrage normal ;
- le passage en recovery est explicite ;
- `/recovery` est une page embarquée indépendante de LittleFS ;
- la recovery garde son AP actif et tente le Wi-Fi local si les identifiants sont connus ;
- la recovery ne doit jamais écrire dans `factory` lors d’une mise à jour du Main.

Le recovery reste limité à l’AP de secours, au Wi-Fi local, à une page légère, aux uploads locaux, au démarrage du Main et aux logs simples.
Ne pas y introduire ESP-NOW, MQTT, PID, capteurs/actionneurs métier, AutomateManager, dashboard complet, GitHub OTA ou HTTPS sans nouvelle décision explicite.

---

## Versions des livraisons

Instruction utilisateur du 25 octobre 2026 : les versions doivent toujours s’incrémenter.

- Chaque nouvelle livraison porte une version supérieure à la précédente, même pour une modification LittleFS seule.
- Conserver le format `YYYYMMDD-NN` : pour une même date, prendre le plus grand numéro déjà généré pour le firmware, LittleFS, Recovery ou un manifeste de livraison, puis incrémenter `NN`.
- Ne jamais réutiliser un numéro ni écraser un binaire versionné existant. Les copies courantes sans numéro peuvent être actualisées.
- Aligner les versions annoncées, les marqueurs embarqués et les noms des fichiers effectivement livrés.
- Les images produites ensemble portent la même version. Une image non reconstruite conserve sa version réelle ; le manifeste précise les versions des composants livrés.

---

## Stockage et nommage des compilations

Les images compilées destinées à être livrées sont stockées dans :

`C:\ARDUINO\V2_PCB\RouteurSolaireESP32\build\ota`

Les dossiers de travail temporaires du compilateur peuvent être distincts ; les images livrées doivent être présentes dans `build/ota/`.

Convention de nommage :

| Artefact | Nom versionné |
|---|---|
| Firmware principal | `RouteurSolaireESP32_firmware_YYYYMMDD-NN.bin` |
| LittleFS | `RouteurSolaireESP32_littlefs_YYYYMMDD-NN.bin` |
| Recovery, si compilée et livrée | `RouteurSolaireESP32_recovery_YYYYMMDD-NN.bin` |
| Manifeste de livraison | `release_YYYYMMDD-NN.json` |

Exemple : `RouteurSolaireESP32_firmware_20261111-01.bin`. Cet exemple n’est ni une version réservée ni une preuve de compilation.

- Ne jamais écraser un fichier versionné existant.
- Les copies courantes `RouteurSolaireESP32_firmware.bin`, `RouteurSolaireESP32_littlefs.bin` et, si applicable, `RouteurSolaireESP32_recovery.bin` peuvent être actualisées uniquement après réussite des contrôles de construction.
- Le manifeste identifie les images réellement produites : versions, noms, tailles, empreintes SHA-256 et contrôles exécutés.
- Ne pas annoncer de livraison complète si une construction a échoué.
- La livraison d’une image Recovery n’autorise pas son flash ni le remplacement de `factory` pendant une mise à jour du Main.

---

## LittleFS

LittleFS contient notamment la WebUI et des fichiers de configuration.
À préserver absolument :

- `/config/device.json`
- `/config/system.json`
- `/config/sensors.json`
- `/config/actuators.json`
- `/config/rules.json`

Avant remplacement de LittleFS, exporter indépendamment les cinq JSON et vérifier leur intégrité après restauration. Ne pas présumer que leur conservation automatique est garantie.

Si la partition est nommée `spiffs` dans le CSV, ne pas la renommer sans analyse : elle peut être utilisée comme partition LittleFS.

---

## PID

Le PID doit rester configurable.
Paramètres importants :

- activation ;
- Kp, Ki, Kd ;
- consigne réseau ;
- deadband ;
- puissance de sortie maximale ;
- filtre de mesure ;
- rampe ;
- intervalle de calcul ;
- reset de l’intégrale.

Le dashboard doit afficher :

- erreur PID ;
- sortie PID ;
- commande routeur ;
- puissance de sortie ;
- état de saturation ou oscillation si disponible.

---

## Sécurité

À préserver :

- actionneurs en état sûr au démarrage ;
- coupure en cas d’erreur critique ;
- surveillance des capteurs critiques ;
- détection de perte de capteur ;
- logs d’événements.

SafetyManager est prioritaire sur AutomateManager, RuleEngine, PIDController et les commandes manuelles ou distantes. Aucune commande ne doit contourner ses protections.

---

## Logs et performance

Ne pas supprimer les logs existants sans demande explicite.

Surveiller :

- heap ;
- fragmentation ;
- durée de loop ;
- temps `/api/state` ;
- Wi-Fi ;
- ESP-NOW ;
- OTA ;
- LittleFS.

Éviter les traitements bloquants.

---

## Méthode Codex

Pour chaque tâche :

1. lire `AGENTS.md` et les consignes AGENTS.md applicables aux fichiers concernés ;
2. lire les documents utiles dans `doc/`, en commençant par `doc/README.md` ;
3. analyser le code existant si la tâche touche le code ;
4. limiter le périmètre ;
5. modifier uniquement les fichiers nécessaires ;
6. compiler si du code a été modifié et si l’outillage est disponible ; préciser les contrôles exécutés et les limites éventuelles ;
7. donner les tests à réaliser.

Pour l’architecture, consulter `doc/ARCHITECTURE.md`, `doc/PARTITIONS_RECOVERY_ESP32.md` et `doc/OTA.md`.
Pour Arduino IDE, compilation, téléversement et LittleFS, consulter `doc/INSTALLATION_PAR_PHASES.md` et `doc/MEMO_ARDUINO_COMPILATION.md`.

Distinguer documentation, code relu, tests logiciels exécutés et validation sur équipement. Un audit daté ne prouve pas l’état du code courant ni la version installée.

Ne jamais faire plusieurs gros chantiers en une seule passe sans validation.

---

## Phrase de rappel

Le projet doit rester un routeur solaire ESP32 distribué, local-first, fiable, visuel et maintenable, avec un firmware principal unique, une configuration par équipements, un recovery minimal en `factory`, le Main en `ota_0`, LittleFS séparé et la configuration préservée.
