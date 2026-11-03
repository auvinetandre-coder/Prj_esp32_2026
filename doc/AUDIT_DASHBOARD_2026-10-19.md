# Audit du dashboard — 19 octobre 2026

> **Document historique.** Relecture du 20 octobre dans [AUDIT_2026-10-20.md](AUDIT_2026-10-20.md) : la priorité du type source AUTO côté API est partiellement corrigée en faveur de la TIC disponible ; la page actionneurs a maintenant son polling runtime et préserve ses contrôles. Les reproductions de signe PID et de réseau absent affiché comme équilibre restent présentes. Les résultats et numéros de lignes ci-dessous sont ceux du 19 octobre ; le smoke local non adapté ne passe plus tel quel après l'ajout du polling. Voir le bilan courant pour les limites des tests.

## Périmètre et validation

Lecture du dashboard actif dans `data/www/app.js`, de `index.html`, de `style.css`, de l'API `sendStatusLite()`, de SensorManager, du PID et des chemins de commande utiles. Lecture des règles du projet, de DASHBOARD.md, API.md et des audits précédents. Les modifications locales préexistantes ont été conservées.

Audit du code et exécution de fonctions JavaScript avec DOM simulé. Aucun ESP connecté, aucun flash, aucune validation visuelle sur navigateur ou téléphone. Les défauts de logique ci-dessous sont vérifiés dans les sources actuelles ; le rendu et les comportements physiques restent à tester. Aucun fichier de production ni `/config` modifié : seul ce rapport a été ajouté.

Contrôles exécutés :

- `node --check data/www/app.js` : réussi.
- `node build_audit/webui_smoke.cjs` : génération HTML réussie pour INIT, fonctionnement normal et sécurité critique ; échappement vérifié.
- `node build_audit/api_contracts.cjs` : 33 chemins API littéraux associés à des routes. Ne valide pas leurs réponses, méthodes ou paramètres.
- Scénarios supplémentaires dans un banc Node temporaire : inversion du secours PID, STABLE en OFF, SATURATION à 0 % avec achat, deadband zéro remplacée dans la carte, réseau absent traité comme neutre et rafraîchissement 5 s ramené à 1 s reproduits.
- Pas de compilation firmware : aucune modification C++.

## Corrections prioritaires

### 1. Unifier le signe et le sens de l'écart PID

Sources : `src/logic/pid_controller.cpp:75`, `data/www/app.js:2277`, `:2889`, `:3346`.

Le firmware calcule `consigne - mesure`. La carte et le graphe interprètent pourtant le positif comme achat. Le secours dans `pidState()` calcule `mesure - consigne` et remplace même une erreur fournie égale à zéro si son écart calculé dépasse 20 W. Une même situation peut donc changer de signe selon le chemin utilisé.

Correction : conserver le calcul firmware, utiliser la même mesure filtrée et la même formule partout, ne pas remplacer arbitrairement un zéro fourni. Renommer « Erreur PID » en « Écart à la consigne ». Décrire « augmenter la chauffe », « réduire la chauffe » ou « dans la tolérance ». Achat/injection décrit le réseau réel, pas directement l'écart à une consigne éventuellement non nulle.

Tests : consignes 0 W et +100 W ; mesure inférieure, égale et supérieure à la consigne ; erreur absente et zéro valide. Vérifier carte, couleurs, historique et légendes.

### 2. Rafraîchir les alertes et le bandeau supérieur

Sources : `data/www/app.js:772`, `:3860`, `:3918`.

Le rafraîchissement partiel ne remplace que les zones énergie, principale et basse. Les badges de sécurité, Wi-Fi, simulation, le bandeau d'alerte et l'« humeur » du haut ne sont pas reconstruits. Le passage réel vers simulation ou vers une sécurité critique peut laisser le haut dans l'ancien état tandis que les cartes changent. Le retour simulation vers réel peut également laisser un bandeau périmé.

Correction : mettre à jour toutes les zones d'état, en préservant l'éditeur de graphes ouvert. Une alerte critique doit rester immédiatement visible.

Test : sans changer de page, déclencher puis lever sécurité et simulation, déconnecter le Wi-Fi ; vérifier chaque badge et le compte à rebours.

### 3. Corriger la consommation « Maison »

Sources : `src/sensors/sensor_manager.cpp`, fonction `applyGridPowerSource()` ; `data/www/app.js:2837`.

`consumptionW` est actuellement la partie positive de la puissance réseau : l'achat réseau. Le schéma l'affiche dans « Maison ». Une maison alimentée par le solaire peut ainsi apparaître à 0 W.

Correction : conserver le champ historique et son sens, mais ne pas le présenter comme consommation totale. Ajouter un champ explicite si les mesures permettent un bilan fiable. Sans batterie ni autre flux, et avec des mesures comparables, consommation totale = production + réseau signé. Indiquer si le chauffe-eau est inclus et qualifier ce résultat de calculé. Sinon afficher « indisponible ».

Test : production 2 000 W, injection 500 W : total calculé 1 500 W, et non 0 W. Tester production absente et installation avec stockage.

### 4. Distinguer mesure absente et équilibre

Source : `data/www/app.js:2837`.

`Number(null)` vaut zéro : le schéma choisit la classe `neutral` et le texte « équilibre » quand la mesure réseau est absente. La carte réseau affiche pourtant une donnée indisponible.

Correction : vérifier la présence avant conversion ; état inconnu gris et absence d'animation interprétable comme un flux connu. La même distinction doit exister au chargement, où plusieurs valeurs initiales sont actuellement zéro.

Test : valeurs nulles au boot et perte de source ; aucun faux équilibre ni production nulle déduite d'une absence.

### 5. Afficher l'état effectif de régulation

Sources : `data/www/app.js:2299`, `:2324`, `:2356`, `:2457`, `:2467` ; `src/logic/automate_manager.cpp:31`.

Les états déduits dans le navigateur peuvent prendre le dessus sur le firmware. OFF avec erreur zéro donne STABLE ; achat 500 W avec commande zéro donne SATURATION. L'exécution est dite active sur MASTER sans vérifier une règle de régulation réellement active. Le mode configuré AUTO ne dit pas si une commande manuelle ou horaire est actuellement prioritaire. `pidEnabled=false` peut faire afficher OFF même si une autre origine de commande existe.

Correction : privilégier l'état effectif de l'automate, sa raison, la source de commande et l'activité réelle du PID. Séparer « routeur arrêté », « PID désactivé », « attente de surplus », « règle active », « forçage manuel », « forçage horaire », « sécurité » et « mesure indisponible ». Les diagnostics saturation/oscillation doivent être conditionnés à une régulation active et rester des indications si calculés dans le navigateur.

Tests : OFF, PID désactivé, aucune règle de régulation, forçage manuel et horaire en mode AUTO, BACKUP passif et NODE_ACTUATOR commandé à distance.

### 6. Identifier la source réseau effectivement utilisée

Sources : `src/sensors/sensor_manager.cpp`, fonction `applyGridPowerSource()` ; `src/web/web_ui.cpp:897` ; `data/www/app.js:2307`.

En AUTO, SensorManager privilégie la TIC disponible puis le JSY. L'API construit le nom de source en privilégiant le JSY en ligne. L'origine ESP-NOW est déduite de la présence d'une configuration du même type. Le navigateur choisit aussi le premier capteur ressemblant au nom de type. Avec plusieurs compteurs, la source et l'origine affichées peuvent donc être fausses.

Correction : SensorManager doit exposer l'identifiant exact, l'origine, le canal, la validité et l'âge de la mesure retenue. Afficher la source effective et garder le choix configuré séparé.

Test : JSY et TIC disponibles en AUTO, compteurs du même type locaux et distants, puis perte et retour de la source prioritaire.

### 7. Séparer commande demandée, état logiciel et puissance mesurée

Sources : `data/www/app.js:2821`, `:2872` ; `src/logic/automate_manager.cpp:38` ; `src/actuators/actuator_manager.cpp:288` et `:356` ; `src/logic/pid_controller.cpp`.

La « commande appliquée / sortie réelle » du dashboard vient de la décision globale de l'automate, avant les limites du gestionnaire d'actionneurs. La puissance globale est calculée par le PID et ne prouve pas la consommation réelle de l'actionneur ; elle peut être périmée pour une commande passant par un autre chemin. Le runtime actionneur lui-même ne constitue pas une mesure électrique ; les ACK distants sont annoncés NOT_SUPPORTED.

Correction : nommer l'actionneur concerné, afficher commande demandée, commande retenue après limites si disponible, verrouillage et expiration. Marquer la puissance « estimée » ou « mesurée » avec sa source. Ne pas affirmer qu'une sortie distante a exécuté une commande sans retour confirmé. Remplacer le libellé fixe « Sortie SSR » pour les autres types.

Tests : sécurité, timeout, chauffe manuelle/horaire, autre actionneur que SSR1, liaison distante perdue et thermostat du ballon ouvert.

### 8. Corriger la promesse de sécurité du bandeau simulation

Sources : `data/www/app.js:775` ; `src/actuators/actuator_manager.cpp`, fonctions `loop()`, `setCommandPercent()` et `setOutput()`.

Le bandeau annonce « sorties 230 V forcées OFF ». Le code actuel ne garantit pas cette coupure physique à l'entrée en simulation et transmet encore les commandes distantes. Le constat de l'audit précédent reste applicable dans les sources examinées.

Correction nécessaire côté firmware avant de maintenir cette affirmation. Une simple correction de texte ne résout pas le défaut physique. Afficher simulation, durée restante et état de coupure seulement selon ce qui est établi.

Validation sur banc basse tension : entrée en simulation avec sortie déjà ON et actionneur distant ; mesurer la coupure et vérifier l'absence de commande distante active issue de la simulation.

## Robustesse, graphiques et lisibilité

### 9. Fiabiliser les graphes

Sources : `data/www/app.js:3236`, `:3240`, `:3383`, `:3413`, `:3439`, `:3874`.

- Les abscisses suivent l'indice des points, pas leurs timestamps : pauses et pertes réseau sont compressées.
- Les valeurs absentes sont sautées puis les valeurs restantes reliées : une interruption peut apparaître comme une mesure continue.
- Les graphes Canvas n'affichent pas de repères temporels ; les graphes configurables ne tracent pas de légende dans leur chemin actuel.
- Le même axe peut accueillir des unités incompatibles choisies dans l'éditeur.
- Le calcul d'échelle écarte les 5 % extrêmes avec suffisamment de valeurs ; des pointes utiles au diagnostic peuvent sortir du cadre sans indication.
- Les graphes dédiés restent affichés et `graph_pid` est exclu de la liste configurable ; sa configuration ne pilote donc pas directement ces graphes dédiés.

Correction : temps réel sur l'axe X, ruptures aux absences, unités et légendes, axes séparés ou refus du mélange d'unités, indication des valeurs hors échelle et réglages cohérents avec les graphes visibles. Adapter les graphes aux rôles sans PID local.

Tests : perte réseau 30 s, rechargement après une pause, pointe brève, données nulles, graphe W/%/°C, écran étroit.

### 10. Préserver explicitement une tolérance de zéro

Sources : `data/www/app.js:2356`, `:2467`, `:2877`.

Plusieurs `deadbandW || 30` remplacent un zéro configuré par 30 W. Le réseau utilise en outre un seuil visuel fixe de 20 W et l'humeur 50 W : « équilibre réseau » et « consigne atteinte » ne désignent pas le même critère.

Correction : distinguer absence et zéro. Définir et expliquer séparément le seuil visuel réseau et la tolérance PID. À 0 W d'erreur, éviter le sous-titre « injection » actuellement choisi par défaut.

### 11. Éviter les rafraîchissements concurrents

Sources : `data/www/app.js:334`, `:618`, `:3812`, `:6819`.

Le délai est plafonné à 1 000 ms sans minimum positif. Un choix 5 s devient 1 s. `setInterval` peut relancer `refresh()` avant la fin du précédent appel ; le timeout avec Promise.race n'annule pas fetch. Les réponses peuvent arriver dans le désordre et multiplier la charge sur l'ESP.

Correction : une seule actualisation en cours, cadence choisie respectée et bornée, requêtes annulables, reprise après erreur. Afficher l'heure de dernière réponse et « données anciennes / connexion perdue ». Une erreur remplace actuellement tout le dashboard.

Test : API lente au-delà de la cadence, timeout puis retour, passage à un autre onglet en cours de requête.

### 12. Rendre les informations capteurs cohérentes

Sources : `data/www/app.js:2921`, `:2988`, `:3027`, `:3052`, `:3117` ; `src/sensors/sensor_manager.cpp`, fonction `mqttProductionSensorToJson()`.

Les seuils de vieillissement sont codés par type dans le navigateur, indépendamment des délais réellement configurés. Par exemple, une production MQTT de type Power utilise le seuil générique 15 s. Les états WAITING, INVALID et DISABLED sont parfois résumés par « Jamais reçu ». Le secours basé uniquement sur la configuration peut présenter un capteur comme OK sans mesure. L'humeur utilise `s.ok`, tandis que la liste applique d'autres critères de vieillissement.

Les cartes affichent des clés techniques telles que CH1_POWER, limitent les valeurs à six et ne donnent pas d'unité à toutes les clés de canal. Cela gêne la lecture des mesures utiles.

Correction : validité, âge et timeout viennent du gestionnaire de capteurs ; utiliser un état commun pour les cartes et le résumé. Garder les états inconnus explicites, afficher le nom du canal, son usage et les unités, puis proposer les détails techniques.

### 13. Ne pas présenter le défaut courant comme un historique

Sources : `src/web/web_ui.cpp`, construction de `systemHealth.lastFault` ; `data/www/app.js:3080`.

« Dernier défaut » représente actuellement la raison de sécurité disponible, sans date ni événement historisé garanti. Le rôle principal n'expose pas non plus clairement la distinction rôle fonctionnel / redondance pourtant présente dans l'API.

Correction : séparer « défaut actif » et « dernier événement », horodater ce dernier si les logs le permettent, afficher la fonction du nœud et son état MASTER/BACKUP actif ou en veille.

## Informations nécessaires à afficher

### Vue immédiate

- Réseau : achat/injection, puissance, qualité et âge de la source retenue.
- Production : watts, origine, état absent/périmé explicite.
- Chauffe-eau : commande, puissance avec mention mesurée/estimée, actionneur concerné.
- Température ballon : sonde explicitement affectée, âge et état ; afficher les autres températures en détail.
- État effectif et raison : « absorbe le surplus », « attente de surplus », « forçage jusqu'à… », « arrêté par température », « bloqué : mesure réseau perdue ».
- Alerte sécurité ou simulation toujours visible ; dernière actualisation du dashboard.

Ne pas déduire qu'une sonde quelconque représente le ballon. Le bandeau principal actuel ne contient pas de température ; la configuration temperatureMainSource existe mais n'est pas exploitée pour une carte principale dans le chemin actif.

### Détail régulation

- Consigne, mesure réellement utilisée et écart cohérent.
- Tolérance expliquée en watts, sortie calculée, commande finale, cause d'une limitation.
- Source de commande, règle active et état automate, déjà largement disponibles dans l'API.
- Saturation ou oscillation uniquement avec contexte et activité effective.
- Accès aux réglages Kp/Ki/Kd, filtre et rampe ; pas besoin de tout exposer dans les grandes cartes.

### Diagnostic secondaire

- Santé des capteurs et actionneurs, timeout et état distant confirmé ou non confirmé.
- Rôle fonctionnel, rôle de redondance et contrôleur actif ; perte MASTER et double MASTER quand disponibles.
- Dernier événement daté et accès aux logs.
- Versions firmware/LittleFS ; heap libre/minimum/plus gros bloc, durée loop et construction API. Les métriques disponibles peuvent rester dans un panneau dépliable.
- Forçage horaire : plage, fin prévue et état de synchronisation de l'heure.

### Évolutions utiles après fiabilisation

Énergie routée, achetée, injectée et produite en kWh sur la journée. Ces compteurs nécessitent une source et une persistance fiables ; ne pas les présenter comme des totaux journaliers issus du seul historique court du navigateur. Économies et taux d'autoconsommation seulement avec les mesures et paramètres nécessaires.

## Ordre proposé pour les corrections

1. Fiabilité des informations : écarts PID, consommation maison, valeurs absentes, bandeaux et sources réelles.
2. États effectifs : automate, commande, limites, rôle et distinction puissance mesurée/estimée. Traiter le défaut simulation côté firmware dans un lot séparé avec essai sur banc.
3. Lecture courante : température ballon affectée, raison du fonctionnement, fraîcheur des données.
4. Graphes, cadence réseau et détails diagnostics.
5. Bilans énergétiques persistants si souhaités.

Fichiers probablement concernés par ces futurs lots : `data/www/app.js`, `data/www/style.css`, éventuellement `data/www/index.html` pour le cache des assets ; API/SensorManager/runtime pour les sources et états manquants. Les corrections de commande et simulation nécessitent leurs propres modifications firmware et tests matériels. Ne pas transformer cet audit en refonte globale.
