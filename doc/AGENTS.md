# AGENTS.md - Regles Codex du projet RouteurSolaireESP32

## Mission

RouteurSolaireESP32 est un projet ESP32 local-first pour routeur solaire distribue.

Le projet utilise maintenant une architecture Recovery/Main :

```text
factory = recovery minimal
ota_0   = firmware principal
spiffs  = LittleFS
```

Le firmware principal reste unique pour tous les ESP32. Le comportement depend de la configuration locale.

Objectifs prioritaires :

1. stabilite ;
2. securite ;
3. maintenabilite ;
4. performance ;
5. evolutivite.

---

## Regles absolues

- Ne jamais refondre le projet sans demande explicite.
- Ne jamais supprimer une fonctionnalite existante sans validation.
- Ne jamais casser une API existante sans justification documentee.
- Ne jamais ecraser `/config`.
- Ne jamais effacer NVS sans demande explicite.
- Ne jamais faire d'OTA automatique silencieux.
- Ne jamais importer automatiquement des capteurs ESP-NOW.
- Ne jamais ajouter de framework Web lourd.
- Ne jamais remettre GitHub, HTTPS ou logique metier dans le recovery sans nouvelle decision documentee.
- Toujours preserver le recovery en `factory`.
- Toujours ecrire le firmware principal uniquement dans `ota_0`.
- Migrer les anciens champs de configuration sans perdre leurs valeurs ; publier uniquement le schema courant. Firmware et WebUI sont mis a jour ensemble.
- Toujours travailler par petites etapes testables.
- Toujours expliquer les fichiers modifies et les tests a faire.

---

## Documentation avant action

Pour toute tache touchant l'architecture, lire au minimum :

1. `doc/AGENTS.md`
2. `doc/ARCHITECTURE.md`
3. `doc/PARTITIONS_RECOVERY_ESP32.md`
4. `doc/OTA.md`

Pour une tache specialisee, lire aussi la doc concernee : API, Dashboard, ESP-NOW, roadmap ou priorites.

Pour toute question Arduino IDE, partition, compilation, televersement, LittleFS, recovery, ports COM, noms de fichiers ou emplacements de build, lire :

- `doc/MEMO_ARDUINO_COMPILATION.md`

---

## Firmware principal unique

Le main utilise un seul firmware pour tous les ESP.

Le comportement depend de la configuration locale :

- capteurs actives ;
- actionneurs actives ;
- regles ;
- PID ;
- reseau ;
- automatisme applicatif.

Ne pas creer plusieurs firmwares principaux specialises sauf demande explicite.

---

## Recovery

Le recovery est un firmware minimal en `factory`.

Autorise :

- AP secours ;
- Wi-Fi local si possible ;
- page Web simple ;
- upload local `firmware.bin` vers `ota_0` ;
- upload LittleFS local si conserve ;
- bouton demarrer main ;
- logs simples.

Interdit :

- ESP-NOW ;
- MQTT ;
- Dashboard complet ;
- PID ;
- capteurs ;
- actionneurs metier ;
- AutomateManager ;
- GitHub OTA ;
- HTTPS ;
- logique routeur solaire.

Le recovery est un coffre-fort local. Il doit rester petit, robuste et facile a demarrer.

---

## BootManager

`BootManager` gere le cycle Recovery/Main.

Regles :

- le recovery peut basculer vers `ota_0` ;
- le main ne selectionne `factory` que sur demande recovery explicite ;
- l'update firmware ecrit uniquement dans `ota_0` ;
- le recovery ne doit jamais ecrire dans `factory` ;
- les informations de partition doivent rester visibles dans les diagnostics.

Fonctions attendues ou presentes :

- `setNextBootFactory()`
- `setNextBootMain()`
- `bootToFactory()`
- `bootToMain()`
- `getRunningPartitionInfo()`
- `getBootPartitionInfo()`
- `getMainPartitionInfo()`

---

## AutomateManager

`AutomateManager` appartient au firmware principal.

Role :

- centraliser les automatismes applicatifs ;
- executer des sequences non bloquantes ;
- coordonner RuntimeState, RuleEngine, SolarRouter, PIDController et ActuatorManager ;
- journaliser les actions automatiques ;
- respecter strictement SafetyManager.

Regles :

- aucune dependance recovery vers AutomateManager ;
- pas de blocage dans la loop ;
- pas d'action automatique si SafetyManager interdit l'action ;
- conserver des etats observables pour la WebUI et les logs.

---

## Configuration des equipements

Les roles de module et la redondance ont ete supprimes sur demande utilisateur le 21 octobre 2026. Les capteurs, actionneurs et regles actives determinent les capacites. Aucun equipement de chauffage particulier n'est requis. Voir `doc/ROUTAGE_GENERIQUE.md` (ou `ROUTAGE_GENERIQUE.md` depuis ce dossier).

---

## SensorManager

SensorManager est la source de verite des capteurs.

Tous les capteurs doivent etre exposes de maniere uniforme :

- capteurs locaux ;
- capteurs ESP-NOW ;
- capteurs virtuels ;
- capteurs futurs.

Un capteur ESP-NOW doit etre vu comme un capteur normal avec une origine `ESP-NOW`.

Le dashboard, les regles, l'API et les diagnostics doivent consommer SensorManager ou l'etat global.

---

## ESP-NOW

ESP-NOW sert a :

- discovery ;
- heartbeat ;
- annonce des capacites ;
- annonce de capteurs ;
- annonce d'actionneurs ;
- remontee de mesures ;
- diagnostics ;

ESP-NOW ne doit jamais transporter un firmware `.bin`.

Import des capteurs ESP-NOW : manuel uniquement.

---

## Dashboard

Le dashboard doit rester leger :

- HTML ;
- CSS ;
- JavaScript natif ;
- SVG ;
- Canvas.

Interdits sauf demande explicite :

- React ;
- Vue ;
- Angular ;
- gros frameworks ;
- librairies externes inutiles.

Objectifs UX :

- comprendre l'etat energetique en moins de 2 secondes ;
- diagnostiquer un probleme en moins de 10 secondes ;
- regler le PID en moins de 30 secondes.

Le dashboard ne doit pas contenir de logique metier specifique ESP-NOW.

ESP-NOW est une origine, pas un type de capteur.

---

## Maintenance et mises a jour

Le projet gere separement :

- firmware main ;
- LittleFS ;
- configuration `/config`.

Regles :

- action utilisateur obligatoire ;
- pas d'auto-update silencieux ;
- firmware et LittleFS separes ;
- recovery conserve en `factory` ;
- main ecrit dans `ota_0` ;
- `/config` sauvegarde et restaure si LittleFS est remplace ;
- GitHub OTA cote ESP32 non valide actuellement.

---

## LittleFS

LittleFS contient notamment la WebUI et des fichiers de configuration.

A preserver absolument :

- `/config/device.json`
- `/config/system.json`
- `/config/sensors.json`
- `/config/actuators.json`
- `/config/rules.json`

Si la partition est nommee `spiffs` dans le CSV, ne pas la renommer sans analyse : elle peut etre utilisee comme partition LittleFS.

---

## PID

Le PID doit rester configurable.

Parametres importants :

- activation ;
- Kp ;
- Ki ;
- Kd ;
- consigne reseau ;
- deadband ;
- puissance sortie max ;
- filtre mesure ;
- rampe ;
- intervalle de calcul ;
- reset integrale.

Le dashboard doit afficher :

- erreur PID ;
- sortie PID ;
- commande routeur ;
- puissance sortie ;
- etat de saturation ou oscillation si disponible.

---

## Securite

A preserver :

- actionneurs en etat sur au demarrage ;
- coupure en cas d'erreur critique ;
- surveillance capteurs critiques ;
- detection perte capteur ;
- logs d'evenements.

SafetyManager est prioritaire sur AutomateManager, RuleEngine, PIDController et toute commande manuelle risquee.

---

## Logs et performance

Ne pas supprimer les logs existants.

Surveiller :

- heap ;
- fragmentation ;
- duree de loop ;
- temps `/api/state` ;
- Wi-Fi ;
- ESP-NOW ;
- maintenance / update ;
- LittleFS.

Eviter les traitements bloquants.

---

## Methode Codex

Pour chaque tache :

1. lire `doc/AGENTS.md` ;
2. lire les documents utiles dans `doc/` ;
3. analyser le code existant si la tache touche le code ;
4. limiter le perimetre ;
5. modifier uniquement les fichiers necessaires ;
6. compiler si du code a ete modifie ;
7. donner les tests a realiser.

Ne jamais faire plusieurs gros chantiers en une seule passe sans validation.

---

## Phrase de rappel

Le projet doit rester un routeur solaire ESP32 distribue, local-first, fiable, visuel, maintenable, avec recovery minimal en `factory`, main applicatif unique en `ota_0`, LittleFS separe et configuration preservee.
