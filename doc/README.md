# Documentation RouteurSolaireESP32

Modification du 21 octobre 2026 : [suppression des roles et de la redondance, migration et tests](SUPPRESSION_ROLES.md).

Relecture du code et de la documentation : **20 octobre 2026**.

Livraison du 3 novembre 2026 · [Mode SSR MULTI_SINUS, binaires et validation](MULTI_SINUS.md).

## Par où commencer

1. [État du projet](ETAT_DU_PROJET.md) : fonctions présentes, limites et niveau de validation.
2. [Audit du 20 octobre](AUDIT_2026-10-20.md) : anciens constats réévalués, preuves et tests exécutés.
3. [Priorités](TODO_PRIORITES.md) : lots de correction et critères de réception.
4. [Feuille de route](ROADMAP.md) : évolutions après stabilisation.

## Références

| Sujet | Documents |
|---|---|
| Règles du projet | [Racine](../AGENTS.md), [documentation](AGENTS.md) |
| Architecture et décisions | [Architecture](ARCHITECTURE.md), [historique](HISTORIQUE_DECISIONS.md) |
| Boot et maintenance | [Recovery/Main](ARCHITECTURE_OTA_RECOVERY.md), [OTA locale](OTA.md), [partitions](PARTITIONS_RECOVERY_ESP32.md) |
| Construction et installation | [Mémo Arduino](MEMO_ARDUINO_COMPILATION.md), [câblage](../WIRING.md) |
| Régulation | [Automate et règles](AUTOMATE_RULEENGINE.md), [priorités automate](TODO_PRIORITES_AUTOMATE.md), [évolutions automate](ROADMAP_AUTOMATE.md) |
| Communications | [ESP-NOW](ESP_NOW.md), [production MQTT](MQTT_PRODUCTION.md) |
| Pilotage SSR | [Modes et validation sur banc](MULTI_SINUS.md) |
| Interface | [API](API.md), [dashboard](DASHBOARD.md) |
| Audits historiques | [18 octobre](AUDIT_2026-10-18.md), [dashboard du 19 octobre](AUDIT_DASHBOARD_2026-10-19.md) |

## Lire les statuts

- **Présent dans le code** : chemin relu ; pas une preuve d'exécution sur ESP.
- **Test logiciel réussi** : scénario local exécuté, avec la portée indiquée.
- **À valider sur banc** : essai électrique, radio ou maintenance réelle encore nécessaire.
- **Cible** : exigence ou évolution, pas une fonctionnalité acquise.

Les audits datés conservent leurs résultats historiques. Le bilan courant et l'audit du 20 octobre priment pour le statut des défauts. Les tailles des anciens builds et les versions déclarées dans les sources ne prouvent ni la compilation du code courant ni la version installée sur un appareil.
