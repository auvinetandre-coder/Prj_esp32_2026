# Priorités de stabilisation — 20 octobre 2026

Référence : [audit courant](AUDIT_2026-10-20.md), [état du projet](ETAT_DU_PROJET.md). Les lots suivants précèdent les extensions de la [roadmap](ROADMAP.md). Une case cochée ci-dessous signifie code présent et test logiciel indiqué, jamais validation matérielle implicite.

## Corrections présentes à confirmer sur banc

- [x] OFF demandé à l'entrée simulation ; `test_actuator_simulation.js` réussi.
- [x] PID invalide vers zéro/reset, commandes non finies et garde SSR ; `test_ssr_nan.js` réussi.
- [x] Manuel Web maintenu, OFF et retour AUTO, libération de l'ancienne cible de règle ; `test_manual_actuator.js` réussi.
- [x] Règles vides, routage Linky dédié, gardes et calcul par nouvelle lecture ; `test_linky_routing.js` réussi.
- [x] Correction ciblée des fausses pertes de nœud ; `test_espnow_node_liveness.js` réussi.
- [x] Sauvegarde production MQTT ; tests locaux correspondants reussis.
- [ ] Confirmer ces chemins sur ESP, GPIO observés et radio réelle ; inclure réception/retour des capteurs et changements de charge.

## Lot 1 — Sorties et propriété des commandes (P1)

- [ ] Couper anciennes broches/timers/interruptions avant suppression ou changement d'affectation.
- [ ] Définir et appliquer la priorité système OFF avant toute demande non nulle.
- [ ] Inhiber les commandes radio simulées non nulles et traiter la coupure distante avec expiration locale.
- [ ] Arbitrer commandes reçues et automate local, notamment sur NODE_ACTUATOR/NODE_MIXED.

Réception : sortie ON puis simulation ; règle vraie en OFF ; suppression/changement GPIO ; commande distante non écrasée hors politique explicite. Tester SSR1, SSR2, triac et cible personnalisée.

## Lot 2 — Configuration et maintenance (P1)

- [ ] Sauvegarde vérifiée/récente des cinq JSON, sans obligation d'avoir un Wi-Fi configuré.
- [ ] Préserver l'ancienne copie NVS jusqu'au succès de la nouvelle ; restauration par intégrité de chaque fichier.
- [ ] Écriture LittleFS transactionnelle ; commit RAM après succès persistant et contrôle des tailles écrites.
- [ ] Supprimer le formatage implicite au montage normal ; conserver un diagnostic exploitable.
- [ ] Protéger les écritures recovery et valider l'image LittleFS avant annonce de succès.
- [ ] Distinguer scripts USB d'installation/maintenance et mise à jour utilisateur via recovery explicite.

Réception : comparaison des cinq JSON avant/après, y compris règle seule modifiée, AP seul, espace insuffisant, coupure et fichier individuel absent ; refuser vide/tronqué/arbitraire. Confirmer `factory` préservée, firmware dans `ota_0`, reboot normal sur `ota_0` et accès recovery explicite.

## Lot 3 — ESP-NOW et redondance (P1)

- [ ] TTL indépendant des uptimes ; politique séquence/epoch et commandes retardées/rejouées.
- [ ] Heartbeats MASTER/BACKUP filtrés et réception centralisée dans RedundancyManager.
- [ ] Double MASTER persistant et politique explicite de démarrage BACKUP sans heartbeat antérieur.
- [ ] Contrôle de présence cohérent sur tous les chemins, dont rebouclage `millis()` ; la correction de perte ciblée ne suffit pas.
- [ ] Messages capteurs historiques alignés sur SensorManager ; callbacks et concurrence bornés.

Réception : MASTER + deux BACKUP, deux MASTER, boots décalés, retour ancien MASTER, interruption réelle radio et horloge rebouclée. Import manuel seulement ; aucun `.bin` par ESP-NOW.

## Lot 4 — Réactivité, sécurité et rôles (P1/P2)

- [ ] Watchdog utile avec supervision capable d'agir malgré un blocage de boucle.
- [ ] Test Wi-Fi et reconnexion MQTT bornés/non bloquants ; mesurer sous serveur/broker indisponible.
- [ ] Rôle inconnu refusé ou état sûr explicite, sans conversion MASTER implicite.
- [ ] Validation GPIO/capacités/conflits ; sécurité adaptée au rôle plutôt qu'aux deux identifiants SSR historiques.

Réception : perte réseau/broker, mauvais rôle, nœud capteur sans SSR, conflit UART/OneWire/OLED, broche non utilisable en sortie. Mesurer durée de boucle et délai effectif de coupure.

## Lot 5 — Règles et contrats API (P2)

- [ ] Décisions par actionneur et arbitrage des conflits ; commandes en watts distinctes des pourcentages.
- [ ] `setMode` sans sauvegarde répétée ; persistance uniquement sur changement explicite.
- [ ] Unifier les horaires avec heure valide ; ne pas promettre calendrier/imbrications/variables avant implémentation.
- [ ] HTTP d'erreur cohérent et vérification `ok` par les formulaires ; configuration non sauvée toujours visible.
- [ ] Réparer le script réellement embarqué de `/lite`.

Réception : deux sorties/deux actions, 500 W sur 1500 W donnant environ 33,3 %, aucune écriture périodique sans changement, erreur disque/JSON visible et `/lite` valide.

## Lot 6 — Dashboard et diagnostic (P2)

- [ ] Signe PID unique, zéro valide conservé, source/mesure effectivement utilisées.
- [ ] Mesure absente distincte d'équilibre ; achat réseau distinct de consommation totale.
- [ ] Bandeaux/alertes rafraîchis sans écraser les contrôles en édition.
- [ ] Commande demandée/limitée, puissance estimée/mesurée et exécution distante confirmée ou inconnue.
- [ ] États saturation/oscillation seulement si régulation active ; défaut actif distinct du dernier événement.

Réception : source absente, consigne non nulle, forçage, sécurité et simulation sans navigation ; téléphone et plusieurs compteurs du même type. Voir [cadrage dashboard](DASHBOARD.md).

## Lot 7 — Validation reproductible et évolutions

- [ ] Livrer les sept scripts de régression actuellement présents, dont certains non suivis par Git.
- [ ] Adapter le smoke dashboard et compléter les tests API (méthodes, données, statuts).
- [ ] Compiler main/recovery du même état de sources ; tailles, empreintes et CSV réellement utilisé enregistrés.
- [ ] Mesurer triac/SSR, JSY face à une référence, TIC, sondes critiques, heap/fragmentation et latence API dans la durée.
- [ ] Après stabilisation seulement : rôles séparés avec migration, bilans persistants, supervision de parc et étude ESP32-S3.

Aucune fonctionnalité supprimée, aucun changement de partition ni réintroduction de GitHub OTA/HTTPS dans la recovery n'est décidé par cette liste.

## Réception MULTI_SINUS — 3 novembre 2026

- [x] Compiler le firmware et vérifier le contenu de l’image LittleFS.
- [x] Simuler les rapports, transitions, expirations et défauts de cadence.
- [ ] Mesurer les fronts, la coupure de sécurité, les transitions de simulation et la reconfiguration sous charge réseau.
- [ ] Comparer les deux polarités du courant et l’énergie délivrée sur charge résistive.

Voir la [procédure sur banc](MULTI_SINUS.md#validation-sur-banc-avant-utilisation).
