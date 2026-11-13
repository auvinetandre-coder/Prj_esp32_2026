# Documentation RouteurSolaireESP32

Modification du 21 octobre 2026 : [suppression des roles et de la redondance, migration et tests](SUPPRESSION_ROLES.md).

Relecture du code et de la documentation le **4 novembre 2026**.

Vérification documentaire le **13 novembre 2026** : les marqueurs Main et LittleFS présents dans les sources indiquent `20261111-04`. Le manifeste [release_20261111-04](../build/ota/release_20261111-04.json) et les images versionnées correspondantes sont présents ; cela ne prouve pas la version installée sur un ESP32.

Livraison du 3 novembre 2026 · [Mode SSR MULTI_SINUS, binaires et validation](MULTI_SINUS.md).

## Par où commencer

1. [État du projet](ETAT_DU_PROJET.md) : fonctions présentes, limites et niveau de validation.
2. [Audit du 4 novembre](AUDIT_2026-11-04.md) · constats, corrections, doublons et contrôles.
3. [Priorités](TODO_PRIORITES.md) : lots de correction et critères de réception.
4. [Feuille de route](ROADMAP.md) : évolutions après stabilisation.

## Références

| Sujet | Documents |
|---|---|
| Règles du projet | [Racine](../AGENTS.md), [documentation](AGENTS.md) |
| Architecture et décisions | [Architecture](ARCHITECTURE.md), [historique](HISTORIQUE_DECISIONS.md) |
| Boot et maintenance | [Recovery/Main](ARCHITECTURE.md), [OTA locale](OTA.md), [partitions](PARTITIONS_RECOVERY_ESP32.md) |
| Construction et installation | [Mémo Arduino](MEMO_ARDUINO_COMPILATION.md), [câblage](../WIRING.md) |
| Régulation | [Automate et règles](AUTOMATE_RULEENGINE.md), [priorités communes](TODO_PRIORITES.md), [évolutions automate](ROADMAP.md) |
| Communications | [ESP-NOW](ESP_NOW.md), [production MQTT](MQTT_PRODUCTION.md) |
| Diagnostic Wi-Fi persistant | [Commandes USB/Web et historique](WIFI_DIAGNOSTIC.md) |
| Diagnostic serie et surveillance reseau | [Commandes debug et watchdog de connectivite](DEBUG_SERIE.md) |
| Console série Web | [Commandes, transport et essais](CONSOLE_SERIE_WEB.md) |
| Pilotage SSR | [Modes et validation sur banc](MULTI_SINUS.md) |
| Systeme et diagnostic | [Organisation des pages et retrait de la simulation](SYSTEME_DIAGNOSTIC.md) |
| Interface | [API](API.md), [dashboard](DASHBOARD.md) |
| Audits historiques | [18 octobre](AUDIT_2026-10-18.md), [dashboard du 19 octobre](AUDIT_DASHBOARD_2026-10-19.md) |

## Lire les statuts

- **Présent dans le code** : chemin relu ; pas une preuve d'exécution sur ESP.
- **Test logiciel réussi** : scénario local exécuté, avec la portée indiquée.
- **À valider sur banc** : essai électrique, radio ou maintenance réelle encore nécessaire.
- **Cible** : exigence ou évolution, pas une fonctionnalité acquise.

Les audits datés conservent leurs résultats historiques. Le bilan et l'audit du 4 novembre décrivent l'état relu à cette date ; les guides plus récents les complètent. Ils ne constituent pas un audit du code du 11 novembre. Les tailles des anciens builds et les versions déclarées dans les sources ne prouvent ni la compilation du code courant ni la version installée sur un appareil.

## Organisation proposée — revue du 11 novembre 2026

Une référence par sujet, une seule liste de priorités et une seule feuille de route. Les chemins actuels sont conservés pendant ce nettoyage : les consignes AGENTS et les liens hors de ce dossier les utilisent. L'arborescence suivante est proposée pour une étape ultérieure ; aucun déplacement n'est effectué ici.

```text
doc/
  README.md        point d'entrée et index
  AGENTS.md        consignes de documentation
  architecture/    système, décisions, partitions
  guides/          installation, compilation, maintenance, diagnostic
  references/      API, capteurs, régulation, communications, WebUI
  suivi/           bilan daté, priorités, feuille de route
  historique/      audits, migrations, comptes rendus datés
```

Avant tout déplacement, adapter ensemble les liens relatifs, les chemins cités dans les deux AGENTS.md et les références hors de doc/. Éviter les copies de compatibilité qui recréeraient les doublons.

### Inventaire et destination recommandée

Tous les documents conservés sont indexés ci-dessous. Un document historique reste une preuve datée ; ses anciennes procédures et ses résultats ne décrivent pas nécessairement le firmware courant.

| Destination proposée | Document | Sujet |
|---|---|---|
| Racine | [README.md](README.md) | Documentation RouteurSolaireESP32 |
| Racine | [AGENTS.md](AGENTS.md) | AGENTS.md - Regles Codex du projet RouteurSolaireESP32 |
| architecture/ | [ARCHITECTURE.md](ARCHITECTURE.md) | ARCHITECTURE.md - Architecture RouteurSolaireESP32 |
| architecture/ | [PARTITIONS_RECOVERY_ESP32.md](PARTITIONS_RECOVERY_ESP32.md) | RouteurSolaireESP32 — Partitionnement Recovery/Main ESP32 |
| architecture/ | [HISTORIQUE_DECISIONS.md](HISTORIQUE_DECISIONS.md) | Historique des decisions |
| guides/ | [INSTALLATION_PAR_PHASES.md](INSTALLATION_PAR_PHASES.md) | Installation ESP32 par phases |
| guides/ | [MEMO_ARDUINO_COMPILATION.md](MEMO_ARDUINO_COMPILATION.md) | Memo Arduino, partitions et fichiers |
| guides/ | [OTA.md](OTA.md) | OTA.md - Recovery/Main local |
| guides/ | [DEBUG_SERIE.md](DEBUG_SERIE.md) | Diagnostic serie et watchdog de connectivite — 8 novembre 2026 |
| guides/ | [CONSOLE_SERIE_WEB.md](CONSOLE_SERIE_WEB.md) | Console série Web — correctif transport 20261111-04 |
| references/ | [API.md](API.md) | API.md - Contrat API RouteurSolaireESP32 |
| references/ | [DASHBOARD.md](DASHBOARD.md) | DASHBOARD.md — Cadrage Dashboard |
| references/ | [AUTOMATE_EDITEUR_COMPACT.md](AUTOMATE_EDITEUR_COMPACT.md) | Editeur compact de scenarios — 24 octobre 2026 |
| references/ | [AUTOMATE_RULEENGINE.md](AUTOMATE_RULEENGINE.md) | AUTOMATE_RULEENGINE.md - AutomateManager et RuleEngine |
| references/ | [PID_UNIQUE.md](PID_UNIQUE.md) | PID unique — 30 octobre 2026 |
| references/ | [MULTI_SINUS.md](MULTI_SINUS.md) | Modulation MULTI_SINUS |
| references/ | [CATALOGUE_CAPTEURS.md](CATALOGUE_CAPTEURS.md) | Catalogue des capteurs — 21 octobre 2026 |
| references/ | [ESP_NOW.md](ESP_NOW.md) | ESP_NOW.md - Architecture ESP-NOW |
| references/ | [MQTT_PRODUCTION.md](MQTT_PRODUCTION.md) | Production solaire recue depuis Jeedom par MQTT |
| suivi/ | [ETAT_DU_PROJET.md](ETAT_DU_PROJET.md) | État du projet — 4 novembre 2026 |
| suivi/ | [TODO_PRIORITES.md](TODO_PRIORITES.md) | Priorités de stabilisation — 4 novembre 2026 |
| suivi/ | [ROADMAP.md](ROADMAP.md) | ROADMAP.md - Feuille de route RouteurSolaireESP32 |
| historique/ | [AUDIT_2026-10-18.md](AUDIT_2026-10-18.md) | Reverification du projet — 18 octobre 2026 |
| historique/ | [AUDIT_2026-10-20.md](AUDIT_2026-10-20.md) | Audit transversal du code — 20 octobre 2026 |
| historique/ | [AUDIT_2026-11-04.md](AUDIT_2026-11-04.md) | Audit du code — 4 novembre 2026 |
| historique/ | [AUDIT_DASHBOARD_2026-10-19.md](AUDIT_DASHBOARD_2026-10-19.md) | Audit du dashboard — 19 octobre 2026 |
| historique/ | [SUPPRESSION_ROLES.md](SUPPRESSION_ROLES.md) | Suppression des roles — 21 octobre 2026 |
| historique/ | [ROUTAGE_GENERIQUE.md](ROUTAGE_GENERIQUE.md) | Routage generique — 21 octobre 2026 |
| historique/ | [SUPPRESSION_FORCAGE_HORAIRE.md](SUPPRESSION_FORCAGE_HORAIRE.md) | Suppression du forcage horaire — 21 octobre 2026 |
| historique/ | [JSY_ROLES_AUTOMATE.md](JSY_ROLES_AUTOMATE.md) | JSY : roles des pinces et automate — livraison 20261025-04 |
| historique/ | [ANALYSE_JSY_PID_20261031.md](ANALYSE_JSY_PID_20261031.md) | Analyse JSY / ESP-NOW / PID — 31 octobre 2026 |
| historique/ | [VERIFICATION_JSY_20261101.md](VERIFICATION_JSY_20261101.md) | Vérification JSY / SSR — 1er novembre 2026 |
| historique/ | [AUTOMATE_LOGS_20261026.md](AUTOMATE_LOGS_20261026.md) | Logs de scenarios : repetition corrigee — 26 octobre 2026 |
| historique/ | [VERIFICATION_COMMANDES_PARTAGEES.md](VERIFICATION_COMMANDES_PARTAGEES.md) | Verification des commandes partagees — 26 octobre 2026 |

### Nettoyage effectué

| Document supprimé | Motif et remplacement |
|---|---|
| `ARCHITECTURE_OTA_RECOVERY.md` | Synthèse redondante : schéma, capacités et restrictions présents dans [Architecture](ARCHITECTURE.md), [partitions](PARTITIONS_RECOVERY_ESP32.md) et [OTA](OTA.md). Les anciennes tailles non remesurées sont déjà consignées dans les partitions et les audits. |
| `ROADMAP_AUTOMATE.md` | Duplique la roadmap générale et prévoit encore MASTER/BACKUP, retiré. Les éléments spécifiques utiles sont intégrés aux [extensions de l'automate](ROADMAP.md#extensions-de-lautomate-après-stabilisation). |
| `TODO_PRIORITES_AUTOMATE.md` | Liste complémentaire intégrée aux [critères de réception communs](TODO_PRIORITES.md#réception-de-lautomate-et-des-règles). |

Les audits, analyses, décisions de migration et comptes rendus sont conservés : ils apportent des constats, contextes ou essais uniques. Le rôle des pinces dans JSY_ROLES_AUTOMATE.md est distinct des rôles de module supprimés.

### Limites de la revue et suites documentaires

Cette passe porte sur l'inventaire, les recouvrements, la navigation et les contradictions documentaires repérées. Elle ne constitue pas un nouvel audit complet du code ni une validation sur ESP32.

- Le bilan du 4 novembre et ses versions sont datés. Les marqueurs sources relus le 13 novembre dans `src/build_info.h` et `data/www/littlefs_version.txt` indiquent `20261111-04`. Le manifeste et les images versionnées correspondantes sont présents dans `build/ota` ; ces éléments ne prouvent pas la version installée.
- [Automate et règles](AUTOMATE_RULEENGINE.md), [API](API.md), [JSY et pinces](JSY_ROLES_AUTOMATE.md) et [MQTT](MQTT_PRODUCTION.md) mêlent encore cadrage, anciens chemins Linky et comportement plus récent du [PID unique](PID_UNIQUE.md). Une réécriture technique doit confronter chaque contrat au code ; ce nettoyage ne les certifie pas.
- Le mémo Arduino conserve des procédures historiques et une ancienne mention du rôle de redondance. Commencer par [l'installation par phases](INSTALLATION_PAR_PHASES.md) pour le parcours actuel.
- Les anciens résultats d'audit ne sont pas réécrits rétroactivement. Les mises à jour de statut doivent être datées et distinguer code relu, tests logiciels et validation matérielle.
