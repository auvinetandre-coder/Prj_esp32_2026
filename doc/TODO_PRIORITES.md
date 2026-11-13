# Priorités de stabilisation — 4 novembre 2026

Les identifiants renvoient à l'[audit du code](AUDIT_2026-11-04.md). Chaque lot se traite séparément.

| Ordre | Lot | Réception |
|---|---|---|
| 1 · P1 | A2 puis A1. Sauvegarde et persistance des cinq JSON, puis protection des uploads Recovery. | Vérifier la conservation de `/config` sur un ESP en AP seul, après disque plein, interruption et modification d'une règle seule. Refuser requête anonyme et image invalide. Vérifier reboot normal, factory et démarrage explicite du Main. |
| 2 · P1 | A3. Transition sûre lors d'une reconfiguration des sorties. | Observer ancienne et nouvelle broches en basse tension. Tester suppression, changement de polarité, local vers distant et triac avec une impulsion en attente. |
| 3 · P1 | A4. Supervision indépendante et blocages coopératifs. | Mesurer le délai maximal de coupure sous défaillance réseau et appels de maintenance. Vérifier la cadence et les deux polarités du mode MULTI_SINUS sur banc adapté. |
| 4 · P1 | A5. Validation des trames et traitement ESP-NOW. | Injecter des trames malformées, vérifier expiration, rejet d'anciennes commandes et comportement pendant un changement de configuration. Confirmer l'import manuel. |
| 5 · P2 | A6, A7, A9 et A10. Contrats MQTT et HTTP, règles et persistance. | Vérifier la fraîcheur des commandes MQTT, les valeurs malformées et l'affichage des erreurs de sauvegarde, la limite par règle et les écritures dues à `setMode`. |
| 6 · P2 | A8. Construction reproductible et versionnement. | Tester un CLI hors chemin par défaut, le CSV réellement compilé, les builds simultanés et les échecs partiels. |
| 7 | Tri des doublons après validation. | Contrôler les références, conserver le test du JavaScript embarqué, mesurer la taille et préserver les contrats d'usage. |

Les tests existants couvrent déjà le PID commun, l'arbitrage par sortie, la priorité OFF, les commandes ESP-NOW à bail local, les migrations, la simulation et la syntaxe de `/lite`. Les résultats sur matériel restent à établir. Les extensions sont répertoriées dans la [feuille de route](ROADMAP.md).

## Réception de l'automate et des règles

Critères spécifiques consolidés le 11 novembre 2026 depuis la liste automate, sans nouvelle validation :

- Sur banc, vérifier l'arrêt global et le relâchement de chaque cible depuis règles, WebUI, MQTT et ESP-NOW : perte de liaison, suppression de règle et transition simulation/réel.
- Mesurer le coût de validation et la limite par règle du constat A9, ainsi que les temps de boucle sous charge. Tester NTP absent, changement de fuseau et reprise de la source.
- Comparer les commandes effectivement acceptées à leur restitution dans l'API et les graphes.
- Examiner RECOVERY_REQUEST : la route Web coupe les sorties et demande directement la factory via BootManager.
- Rattacher la réaffectation des sorties et la persistance aux lots A3 et A2 ci-dessus.

Références : [PID unique](PID_UNIQUE.md), [automate et règles](AUTOMATE_RULEENGINE.md). Les résultats de tests antérieurs restent datés ; cette consolidation ne les réexécute pas.
