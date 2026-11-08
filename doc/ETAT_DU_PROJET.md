# État du projet — 4 novembre 2026

Le firmware principal reste unique et se configure par équipement. Les rôles de module et la redondance ont été retirés le 21 octobre. Les marqueurs de sources Main et LittleFS indiquent `20261103-05`. La version installée doit être lue sur l'ESP32.

| Sous-système | État actuel |
|---|---|
| Boot | Factory réservée à la recovery, application dans `ota_0`, LittleFS dans `spiffs`. Le passage en recovery est explicite. |
| Configuration | Cinq JSON, migrations historiques et sauvegarde NVS conditionnelle. Le formatage implicite et la sauvegarde non transactionnelle restent prioritaires. |
| Capteurs | JSY, TIC, DS18B20, import ESP-NOW manuel et production MQTT. Le chemin radio historique demande encore une mise en cohérence avec SensorManager. |
| Régulation | PID positionnel commun, calcul sur nouvelle mesure, décisions de règles par actionneur et priorité système OFF. Les détails de source, consigne et limites sont dans [PID unique](PID_UNIQUE.md). |
| Sorties | SSR, relais et triac. Le mode [MULTI_SINUS](MULTI_SINUS.md) dispose d'un timer et d'une garde de cadence. Les changements de broche et la coupure physique restent à vérifier sur banc. |
| ESP-NOW | Découverte, télémétrie, commandes partagées avec bail local et retour d'état. Le contrôle d'un pair repose sur la MAC et les contrôles applicatifs, avec pairs non chiffrés. |
| WebUI | Interface native, diagnostic, configuration, scénarios, pilotage et graphes. Le script embarqué de `/lite` passe le test de syntaxe. |
| Validation | Les 24 scripts de régression passent. Le dossier [audit](AUDIT_2026-11-04.md) distingue contrôles locaux et essais à effectuer sur ESP32. |

Le Main appelle SensorManager et Safety à 100 ms, RuleEngine et AutomateManager à 250 ms, dans une boucle coopérative. Ces périodes dépendent du temps passé dans le réseau, l'API et les périphériques. Le PID calcule sur les nouveaux échantillons de la source configurée pour l'action retenue. Le module SolarRouter a été retiré. EspNowNode est utilisé par l'exemple autonome.

Consulter les [priorités de stabilisation](TODO_PRIORITES.md) et l'[audit détaillé](AUDIT_2026-11-04.md) pour les corrections et le tri proposé. Les audits datés d'octobre restent consultables comme archives.
