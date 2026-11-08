# Priorités AutomateManager et RuleEngine — 4 novembre 2026

Le moteur sélectionne une décision par actionneur. Le PID positionnel calcule pour les actions effectivement arbitrées. La priorité OFF, le bail local ESP-NOW, la conversion watts vers pourcentage et la libération des anciennes cibles de règle sont couverts par les scripts locaux. Voir [PID unique](PID_UNIQUE.md) et l'[audit](AUDIT_2026-11-04.md).

- Vérifier sur banc l'arrêt global et le relâchement de chaque cible depuis une règle, la WebUI, MQTT et ESP-NOW. Inclure perte de liaison, suppression d'une règle et transition simulation/réel.
- Vérifier le coût des validations de règles et la limite par règle du constat A9 à chaque cycle et les temps de boucle sous charge. Tester NTP absent, changement de fuseau et reprise de la source.
- Mesurer les commandes réellement acceptées et vérifier leur restitution par l'API et les graphes.
- Examiner l'état interne RECOVERY_REQUEST. La route Web coupe les sorties et demande directement la factory via BootManager.
- Traiter la coupure lors d'un changement d'affectation et la persistance des configurations selon les [priorités globales](TODO_PRIORITES.md).
