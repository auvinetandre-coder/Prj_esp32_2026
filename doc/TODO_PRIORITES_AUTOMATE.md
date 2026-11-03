# Priorités AutomateManager / RuleEngine — 20 octobre 2026

Ce suivi spécialisé complète les [priorités globales](TODO_PRIORITES.md), sans les remplacer. Le [cadrage](AUTOMATE_RULEENGINE.md) distingue désormais code courant et historique ; l'[audit du 20 octobre](AUDIT_2026-10-20.md) détaille les preuves et tests. Chaque lot doit rester indépendant.

## Présent dans le code

- AutomateManager et états `BOOT`, `INIT`, `IDLE`, `AUTO_ROUTING`, `MANUAL_FORCE`, `SAFETY`, `ERROR`, `RECOVERY_REQUEST`.
- Priorités sécurité/erreur/recovery/manuel/règles/routage/idle et état API observable.
- Éditeur de règles Web avec conditions plates ET/OU, branches ALORS/SINON et liste vide sauvegardable.
- Manuel Web maintenu sur une cible, OFF et retour AUTO ; libération de la précédente cible de règle.
- Action `setPowerFromLinky` à cible +100 W avec gardes de production et disponibilité TIC.
- PID fail-safe sur invalides et garde SSR contre commandes non finies.

Présent ne signifie pas validé sur matériel. L'état ERROR a une détection encore vide (`errorDetected()` retourne `false`). Le chemin Web recovery sélectionne directement la factory ; il ne garantit pas une transition observable RECOVERY_REQUEST dans l'automate.

## Lot A — Autorité et coupure (P1)

- [ ] Priorité système OFF avant demandes non nulles, distincte du OFF manuel maintenu d'une cible.
- [ ] Propriété/expiration des commandes radio dans l'arbitrage, sans écrasement au cycle suivant.
- [ ] Arrêt des anciennes broches avant reconfiguration et absence de commandes distantes simulées non nulles.
- [ ] Sécurité selon les capacités/rôles, sans obligation de deux SSR historiques pour tous les nœuds.

Réception : même résultat sûr depuis règle, manuel Web, MQTT et commande distante ; NODE_ACTUATOR et BACKUP passif ; tests GPIO réels sur banc basse tension.

## Lot B — Commandes et persistance (P2)

- [ ] Une décision par actionneur, avec priorité/conflits explicites et limites mémoire.
- [ ] Unité explicite watts/pourcentage ; 500 W sur 1500 W donnant environ 33,3 %.
- [ ] Pas d'écriture persistante à chaque action `setMode` sans changement ; séparer mode d'exécution et sauvegarde.
- [ ] Retour d'erreur cohérent du gestionnaire d'actionneurs vers automate/API.

Réception : deux règles sur deux cibles et deux actions d'une règle ; conflits déterministes ; aucune écriture flash répétée sans demande persistante.

## Lot C — Temps et validité des mesures (P2)

- [ ] Unifier les conditions horaires avec une heure civile valide ; ne pas exposer l'uptime comme heure de la journée.
- [ ] Catalogue, validateur, éditeur et moteur sur le même contrat de conditions.
- [ ] Source effective exacte et qualité de mesure provenant de SensorManager.
- [ ] Distinguer consigne configurée, cible effective Linky, mesure et source de commande dans l'API.

Réception : NTP absent/perdu, minuit, changement de fuseau, perte/retour TIC et production MQTT expirée ; absence de hausse sur doublon TIC ; maintien Linky borné et sécurité prioritaire.

## Lot D — Validation et coût du moteur

- [ ] Compiler les sources courantes et conserver tailles/empreintes/CSV utilisé, sans reprendre les anciennes mesures comme actuelles.
- [ ] Livrer les sept scripts locaux ; tests bloc C++ traduit distincts de compilation/exécution ESP.
- [ ] Tester contrats HTTP réels, coupure de source, variations de charge et commande distante.
- [ ] Mesurer coût des recopies JSON et validations de règles à chaque cycle avant de déplacer ce travail à la sauvegarde.
- [ ] Mesurer heap, fragmentation, durée de boucle et API sous réseau défaillant ; corriger watchdog et blocages.

## Après stabilisation

Groupes imbriqués limités à deux niveaux, variables utilisateur, calendrier et simulation complète d'une règle restent des évolutions, pas des fonctionnalités validées. Ne pas recréer l'éditeur Web déjà présent. Pas de suppression de module, refonte globale, changement de firmware par rôle ou dépendance de la recovery à l'automate.
