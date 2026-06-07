# AGENTS.md â€” RÃ¨gles Codex du projet RouteurSolaireESP32

## Mission

RouteurSolaireESP32 est un firmware ESP32 unique destinÃ© Ã  piloter un routeur solaire distribuÃ©.

Le mÃªme firmware doit pouvoir fonctionner sur plusieurs ESP32 avec des rÃ´les diffÃ©rents selon la configuration locale.

Objectifs prioritaires :

1. stabilitÃ© ;
2. sÃ©curitÃ© ;
3. maintenabilitÃ© ;
4. performance ;
5. Ã©volutivitÃ©.

---

## RÃ¨gles absolues

- Ne jamais refondre le projet sans demande explicite.
- Ne jamais supprimer une fonctionnalitÃ© existante sans validation.
- Ne jamais casser une API existante sans justification documentÃ©e.
- Ne jamais Ã©craser `/config`.
- Ne jamais faire dâ€™OTA automatique silencieux.
- Ne jamais importer automatiquement des capteurs ESP-NOW.
- Ne jamais ajouter de framework Web lourd.
- Toujours preserver le passage explicite en recovery pour les mises a jour.
- Toujours prÃ©server la compatibilitÃ© avec les champs historiques utilisÃ©s par la WebUI.
- Toujours travailler par petites Ã©tapes testables.
- Toujours expliquer les fichiers modifiÃ©s et les tests Ã  faire.

---

## Firmware unique

Le projet utilise un seul firmware pour tous les ESP.

Le comportement dÃ©pend de la configuration locale :

- rÃ´le de lâ€™ESP ;
- capteurs activÃ©s ;
- actionneurs activÃ©s ;
- rÃ¨gles ;
- PID ;
- rÃ©seau ;
- redondance.

Ne pas crÃ©er plusieurs firmwares spÃ©cialisÃ©s sauf demande explicite.

---

## RÃ´les

RÃ´les actuels ou attendus :

- `MASTER`
- `BACKUP`
- `STANDALONE`
- `NODE_SENSOR`
- `NODE_ACTUATOR`
- `NODE_MIXED`

Ã‰volution cible :

- rÃ´le de redondance : `MASTER`, `BACKUP`, `STANDALONE`
- rÃ´le fonctionnel : `SENSOR`, `ACTUATOR`, `MIXED`

Ne pas supprimer brutalement les rÃ´les historiques.

---

## SensorManager

SensorManager est la source de vÃ©ritÃ© des capteurs.

Tous les capteurs doivent Ãªtre exposÃ©s de maniÃ¨re uniforme :

- capteurs locaux ;
- capteurs ESP-NOW ;
- capteurs virtuels ;
- capteurs futurs.

Un capteur ESP-NOW doit Ãªtre vu comme un capteur normal avec une origine `ESP-NOW`.

Le dashboard, les rÃ¨gles, lâ€™API et les diagnostics doivent consommer SensorManager ou lâ€™Ã©tat global.

---

## ESP-NOW

ESP-NOW sert Ã  :

- discovery ;
- heartbeat ;
- annonce de rÃ´les ;
- annonce de capteurs ;
- annonce dâ€™actionneurs ;
- remontÃ©e de mesures ;
- diagnostics ;
- supervision MASTER/BACKUP.

ESP-NOW ne doit jamais transporter un firmware `.bin`.

Pour lâ€™OTA distante, ESP-NOW transmet seulement un ordre ou une notification. Chaque ESP tÃ©lÃ©charge ensuite son fichier via Wi-Fi.

Import des capteurs ESP-NOW : manuel uniquement.

---

## Dashboard

Le dashboard doit rester lÃ©ger :

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

- comprendre lâ€™Ã©tat Ã©nergÃ©tique en moins de 2 secondes ;
- diagnostiquer un problÃ¨me en moins de 10 secondes ;
- rÃ©gler le PID en moins de 30 secondes.

Le dashboard ne doit pas contenir de logique mÃ©tier spÃ©cifique ESP-NOW.

ESP-NOW est une origine, pas un type de capteur.

---

## Recovery et mises a jour

Le projet doit gerer separement :

- `firmware.bin`
- `littlefs.bin`

Les mises a jour firmware et LittleFS se font depuis la factory recovery, pas depuis le firmware principal.

Regles :

- action utilisateur obligatoire ;
- pas d'auto-update silencieux ;
- firmware et LittleFS separes ;
- le firmware principal doit rester sur `ota_0` lors d'un redemarrage normal ;
- le passage recovery doit etre explicite ;
- `/recovery` est une page embarquee independante de LittleFS ;
- la recovery garde son AP actif et tente le WiFi local si les identifiants sont connus.
---

## LittleFS

LittleFS contient notamment la WebUI et des fichiers de configuration.

Ã€ prÃ©server absolument :

- `/config/device.json`
- `/config/system.json`
- `/config/sensors.json`
- `/config/actuators.json`
- `/config/rules.json`

Si la partition est nommÃ©e `spiffs` dans le CSV, ne pas la renommer sans analyse : elle peut Ãªtre utilisÃ©e comme partition LittleFS.

---

## PID

Le PID doit rester configurable.

ParamÃ¨tres importants :

- activation ;
- Kp ;
- Ki ;
- Kd ;
- consigne rÃ©seau ;
- deadband ;
- puissance chauffe-eau max ;
- filtre mesure ;
- rampe ;
- intervalle de calcul ;
- reset intÃ©grale.

Le dashboard doit afficher :

- erreur PID ;
- sortie PID ;
- commande routeur ;
- puissance chauffe-eau ;
- Ã©tat de saturation ou oscillation si disponible.

---

## SÃ©curitÃ©

Ã€ prÃ©server :

- actionneurs en Ã©tat sÃ»r au dÃ©marrage ;
- coupure en cas dâ€™erreur critique ;
- surveillance capteurs critiques ;
- dÃ©tection perte capteur ;
- dÃ©tection double MASTER ;
- logs dâ€™Ã©vÃ©nements.

---

## Logs et performance

Ne pas supprimer les logs existants.

Surveiller :

- heap ;
- fragmentation ;
- durÃ©e de loop ;
- temps `/api/state` ;
- Wi-Fi ;
- ESP-NOW ;
- OTA ;
- LittleFS.

Ã‰viter les traitements bloquants.

---

## MÃ©thode Codex

Pour chaque tÃ¢che :

1. lire `AGENTS.md` ;
2. lire les documents utiles dans `/docs` ;
3. analyser le code existant ;
4. limiter le pÃ©rimÃ¨tre ;
5. modifier uniquement les fichiers nÃ©cessaires ;
6. compiler si possible ;
7. donner les tests Ã  rÃ©aliser.

Ne jamais faire plusieurs gros chantiers en une seule passe sans validation.

---

## Phrase de rappel

Le projet doit rester un routeur solaire ESP32 distribuÃ©, local-first, fiable, visuel, maintenable, avec un firmware unique et une configuration par rÃ´le.

