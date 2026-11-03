# État du projet — 20 octobre 2026

Mise a jour du 21 octobre 2026 : roles et redondance supprimes du firmware, des configurations actives, des API et de la WebUI ; onglet Secours retire. Voir [le contrat actuel et la migration](SUPPRESSION_ROLES.md). Les observations datees du 20 octobre ci-dessous sont historiques pour ces points.

## Mise à jour du 3 novembre 2026

Le mode SSR `MULTI_SINUS` est compilé et couvert par les simulations locales. Les mesures de cadence sous charge réseau et les essais électriques restent à faire. Voir le [bilan et les binaires](MULTI_SINUS.md). Les sections suivantes documentent l’état du 20 octobre.

## Conclusion et niveau de validation

Le firmware principal unique, la recovery séparée, SensorManager, l'automate/règles/PID, ESP-NOW, MQTT et la WebUI native sont présents. La priorité reste la stabilisation des sorties, de la configuration et de la redondance.

La relecture porte sur l'arbre local modifié, pas uniquement sur HEAD. Versions déclarées : firmware `20261020-06`, LittleFS `20261020-01` ; aucune version installée vérifiée. Voir l'[audit courant](AUDIT_2026-10-20.md) et l'[index documentaire](README.md).

**Présent dans le code**, **testé localement** et **validé sur matériel** sont trois statuts distincts. Aucune validation matérielle ni compilation nouvelle n'est revendiquée par cette mise à jour documentaire.

## Cartographie du code actuel

| Ensemble | Présent et limite principale |
|---|---|
| Main / BootManager | `ota_0` au reboot normal, recovery explicite, boucle coopérative instrumentée |
| Recovery | AP et tentative STA, uploads séparés ; authentification des écritures et validation image LittleFS à corriger |
| ConfigManager | Cinq JSON et backup NVS ; copie récente et transactionnelle non garantie, RAM modifiée avant succès disque |
| Runtime / logs / LED / OLED | État, événements, métriques, identification et affichage ; durée/heap à vérifier sur ESP |
| Capteurs | JSY Modbus, Linky TIC, DS18B20 asynchrone, ESP-NOW et production MQTT ; voie radio historique hors SensorManager |
| Actionneurs | SSR/relais/triac et commandes distantes ; OFF à l'entrée simulation et gardes NaN présents ; anciennes broches et simulation distante à corriger |
| Sécurité | Température, capteurs, réseau, double MASTER ; watchdog inefficace et dépendance aux identifiants SSR historiques |
| Automate | Priorités, manuel Web maintenu, libération de l'ancienne cible de règle ; priorité système OFF incomplète |
| Règles | Éditeur Web, ALORS/SINON, liste vide acceptée ; une seule décision, unités watts et persistance de `setMode` à corriger |
| PID | Gardes de validité et zéro/reset ; branche Linky +100 W distincte du calcul JSY historique |
| ESP-NOW | v4 et historique, découverte/import manuel ; fausses pertes corrigées localement, TTL/heartbeat/redondance ouverts |
| MQTT / Wi-Fi | Production validée/expirée via SensorManager ; reconnexion MQTT et test Wi-Fi synchrones |
| WebUI / API | Dashboard natif, polling runtime actionneurs ; `/lite`, erreurs HTTP et cohérence énergétique/PID ouverts |
| Scripts | Builds/transferts/releases et sept scripts de régression ; certains tests non suivis par Git, repli Arduino CLI défectueux |

## Chemin de contrôle et cadences

Le main instancie `EspNowManager`. `EspNowNode` sert à l'exemple autonome. `SolarRouter` est instancié mais sa boucle n'est pas appelée par le main actuel ; ne pas le présenter comme la régulation active.

Capteurs/Safety sont planifiés à 100 ms, règles/automate à 250 ms et watchdog à 1000 ms. Le PID est appelé par les actions de règles, sans tâche autonome à 100 ms. La branche Linky calcule sur nouveau timestamp TIC avec un pas entre lectures borné de 100 à 2000 ms. Les chemins bloquants peuvent dépasser ces périodes.

## Changements depuis l'audit du 18 octobre

- OFF physique demandé à l'entrée simulation ; OFF deux secondes au retour réel. Les commandes radio simulées restent un risque ouvert.
- PID : mesure/configuration/résultat non finis provoquent zéro/reset ; gardes sur entrée actionneur et SSR.
- Manuel Web : une cible maintenue en RAM, y compris OFF prioritaire sur les règles ; AUTO libère. Ce n'est pas un arbitrage général multi-actionneurs.
- Règles : liste vide valide ; ancienne cible relâchée lors du changement de règle/priorité. Cela ne sécurise pas un GPIO supprimé.
- Linky : `setPowerFromLinky`, cible +100 W, production valide positive requise, formule incrémentale, montée/descente distinctes, maintien borné `LINKY_HOLD_10S`.
- Présence radio : correction ciblée du faux LOST si RX actualise l'heure après le début de boucle ; autres gardes d'âge encore à tester.
- Production MQTT : source SensorManager/API ; ne remplace pas la mesure réseau et sert d'autorisation à l'action Linky lorsqu'elle est activée.

`STANDALONE` reste un rôle de redondance annoncé sans valeur distincte de `DeviceRole`. Un texte inconnu retombe sur MASTER. La séparation fonction/redondance n'est pas achevée.

## Priorités ouvertes

1. Anciennes broches, priorité système OFF, simulation distante et autorité des commandes reçues.
2. Sauvegarde/restauration des cinq JSON, écritures transactionnelles, montage sans formatage implicite, recovery protégée.
3. TTL indépendant des boots, heartbeat centralisé et reprise MASTER/BACKUP déterministe.
4. Blocages réseau, watchdog indépendant et sécurité selon rôles/capacités.
5. Actions par actionneur, unités explicites, fin des sauvegardes répétées et contrats HTTP cohérents.
6. Dashboard : source effective, écart PID, mesure absente, consommation totale et puissance estimée/mesurée.

Lots et critères : [TODO_PRIORITES.md](TODO_PRIORITES.md). Les audits des 18 et 19 octobre restent historiques.

## Contrôles du 20 octobre

Les sept scripts `tools/test_*.js` passent, ainsi que la syntaxe de `app.js`. Ils vérifient surtout des fonctions UI et des blocs C++ traduits en JavaScript ; ils ne compilent pas le firmware et ne pilotent pas de matériel.

Contrôle statique local : 17 succès et un échec dans `/lite`. Les 33 chemins API littéraux ont une route parmi 61, sans vérification des méthodes/réponses. L'ancien smoke échoue sur un banc devenu incomplet ; adapté en mémoire pour retirer le nouveau démarrage du polling, il passe INIT/OK/CRITICAL et l'échappement HTML. Le signe inversé du secours PID et l'équilibre affiché sur mesure absente sont reproduits.

Aucune compilation relancée pour ce lot documentaire ; les anciens résultats ne certifient pas le code actuel. Aucun firmware ni `/config` modifié, aucun appareil flashé.

## Validation matérielle restante

Sur banc basse tension : simulation/réel, OFF, anciennes broches, commandes locales/distantes, capteurs perdus/retrouvés, charge Linky variable et vrai délai de mesure, double MASTER et boots décalés. Contrôler cinq JSON avant/après maintenance interrompue, reboot `ota_0`, recovery explicite, timing triac/SSR, heap et latence API sous polling et réseau défaillant.
