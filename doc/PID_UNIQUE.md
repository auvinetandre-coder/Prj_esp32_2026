# PID unique — 30 octobre 2026

## Fonctionnement

`positional_pid.h` calcule `biais + Kp × erreur + intégrale − Kd × dérivée filtrée de la mesure`.
L'erreur vaut consigne moins puissance réseau : import positif, injection négative.
0 % arrête la charge ; 100 % correspond à sa puissance maximale.
Kp est en %/W, Ki en %/(W·s), Kd en %·s/W. Les gains V17 ne sont pas directement transférables.

Une instance est identifiée par l'identifiant stable de l'actionneur, avec une limite de 16 sorties.
L'automate sélectionne la première action de plus haute priorité pour chaque sortie avant tout calcul PID.
Plusieurs sorties peuvent fonctionner simultanément. Deux règles visant la même sortie ne calculent pas deux PID concurrents.
Les gardes sécurité, recovery, manuel et propriété radio restent prioritaires. OFF système coupe toutes les sorties.
L'ancien module SolarRouter, jamais appelé dans la boucle principale, et ses équations inutilisées sont retirés.

Le calcul utilise l'intervalle réel entre acquisitions acceptées. Une acquisition répétée ne fait évoluer ni I, ni D, ni les rampes.
L'intervalle minimal est configurable. L'expiration est contrôlée à chaque passage, même sans acquisition nouvelle.
Une interruption de mesure n'est pas intégrée rétrospectivement. Les dépassements/valeurs non finies arrêtent le PID.
La couche capteurs applique également son propre délai d'expiration : le délai effectif est le plus court des deux.

L'intégrale est bornée à ±100 points de pourcentage. L'anti-windup bloque son accumulation dans le sens d'une saturation ou d'une limitation de rampe.
La commande acceptée par ActuatorManager sert au suivi de l'intégrale. Il s'agit de la commande logicielle acceptée, pas d'une mesure de puissance ni d'un acquittement matériel distant.
Dans la zone morte, la commande est maintenue et l'intégrale est alignée pour limiter les à-coups.
Les changements de coefficients/consigne sont suivis depuis la commande acceptée ; changement de source, reset explicite, désactivation, sécurité et reprise après manuel repartent depuis zéro.

## Réglages de l'action

Dans Automate, choisir **Réguler le surplus**, algorithme **PID** puis configurer :

- capteur réseau, activation, consigne, Kp/Ki/Kd ;
- zone morte, biais, limite de sortie ;
- vitesses de montée et descente en %/s ;
- constantes des filtres mesure et dérivée en ms (0 désactive le filtre) ;
- intervalle minimal et expiration de mesure ;
- garde optionnelle exigeant une production solaire valide.

Le bouton de réinitialisation prend effet lors de la sauvegarde de la règle.
Les paramètres PID globaux ne sont plus éditables ; les anciennes clés système restent lisibles pour migrer les imports historiques.
Les conditions d'une règle doivent permettre de maintenir la régulation autour de la consigne. Une condition exigeant en permanence une injection supérieure à 200 W coupe la règle lorsqu'elle a absorbé le surplus. Les conditions existantes ne sont pas modifiées par la migration.

## Mesures et radio

### Correction de mesure JSY — 31 octobre 2026

Le PID consomme maintenant les watts actifs signes `CH1_W_RAW` / `CH2_W_RAW`
du canal `grid`, localement et via ESP-NOW. Il ne consomme plus le secours
historique U*I*PF de `CHx_POWER`. Les champs historiques restent disponibles
pour les autres usages. Un import manuel `CHx_POWER` autorise les watts bruts
du meme canal ; aucune autre pince ni aucun nouveau capteur n'est importe.
Un emetteur sans cle brute produit une mesure PID indisponible, sans repli
vers des watts estimes. L'emetteur JSY observe en 20261030-01 fournit deja ces cles.

Pour le PID JSY, `timeoutMs` reste un delai de transaction Modbus.
L'expiration de puissance utilise `measurementTimeoutMs` du capteur (3000 ms
par defaut), bornee aussi par le timeout radio et celui de l'action PID.
Le controle utilise l'horloge locale courante pour tolerer un callback radio
survenu depuis le debut du tour de boucle. La radio reste datee a reception ;
ce correctif ne pretend pas mesurer l'age de conversion interne du JSY.

Les gains et filtres configures sont conserves. La comparaison capturee ne
justifie pas d'imposer une fenetre de 2 s ni d'empiler des filtres. Le filtre
exponentiel existant travaille desormais sur les watts actifs bruts.
`pids[]` ajoute `rawMeasuredW`, `lastCalculationMs`, `measurementAgeMs` et
`measurementFilterMs` ; les champs existants restent presents. Les dates sont
en ms depuis le demarrage de l'ESP qui calcule le PID. Une mesure indisponible
reinitialise toujours le PID et sa commande a zero, avant toute rampe.

Validation sur installation apres passage explicite en recovery : installer
le firmware sur l'ESP PID/SSR, comparer brut/filtre a commande constante,
verifier le retour AUTO et OFF, puis perte de mesure et expiration radio dans
un essai controle. Ne pas modifier les gains pendant cette comparaison.
Une mise a jour LittleFS n'est pas necessaire pour ce correctif firmware.

Livraison firmware `20261031-01` compilee avec le core ESP32 3.3.12 :
1 499 268 octets de programme sur 1 966 080 (76 %), 80 632 octets de RAM
statique (24 %). Les 23 scripts `tools/tests/test_*.js` passent ; le test de selection
JSY couvre aussi zero, valeurs invalides, absence de watts bruts, pinces,
expiration, perte radio et horloge de callback. Binaire et SHA-256 dans
`build/ota/release_20261031-01.json`. Aucun televersement realise.

`SensorManager::gridPowerSample()` fournit valeur, source et horodatage d'acquisition.
JSY utilise uniquement son canal déclaré `grid` ; un rôle absent/ambigu est refusé.
TIC utilise l'acquisition d'un champ de puissance, pas l'arrivée d'un tarif ou d'une autre donnée.
Une mesure distante respecte l'import manuel et la fraîcheur de la clé de puissance, indépendamment des autres clés.
Les autres types sont acceptés seulement avec `powerSemantics: "signed_grid"` ; `powerSign: "EXPORT_POSITIVE"` inverse explicitement la convention.
Une température, une production, une consommation de charge ou une grandeur arbitraire en W ne devient pas implicitement une mesure réseau.
Une TIC ne fournissant qu'une estimation à partir des VA conserve cette limite métrologique ; le nouveau PID n'invente pas la puissance active ou l'injection absente.

Le firmware commun n'exporte une puissance JSY/TIC qu'une fois par nouvelle acquisition physique, même si l'intervalle radio est plus court.
Le format des paquets est conservé ; discovery et heartbeat continuent indépendamment.
Pour garantir cette propriété à distance, le nœud émetteur doit également exécuter ce firmware. Un ancien émetteur ne fournit pas nécessairement une information distinguant réémission et nouvelle acquisition.

## Migration et diagnostic

`pid_config_migration.h` convertit `setPowerFromLinky` en `setPowerFromSurplus`, algorithme PID, capteur TIC explicite, consigne d'action +100 W et garde de production.
Une TIC absente/ambiguë produit une source indisponible, sans bascule silencieuse vers JSY.
Les anciennes actions surplus héritent une fois des gains et limites système. Les valeurs explicites, y compris zéro, les identifiants, conditions et métadonnées sont préservés.
Les branches ALORS/SINON et leurs alias sont traités ; `pidSchema: 1` rend la migration idempotente.
Le chargement utilise une copie de travail puis l'écriture temporaire/renommage existante. Un échec mémoire ne remplace pas le fichier original.
Les gains sont conservés numériquement ; leur comportement change avec la correction mathématique. Leur réglage doit être validé sur banc.

`/api/status-lite` expose `pids[]` avec sortie, contexte, source, P/I/D, consigne, erreur, commande acceptée, saturation et rampe.
Les champs scalaires historiques et `pid` restent présents et décrivent le dernier PID effectif. Le graphe sélectionné conserve les mesures de chaque instance.

## Validation

Les tests `tools/tests/test_positional_pid.js`, `test_pid_migration.js`, `test_pid_arbitration.js` et `test_pid_sampling.js` exécutent les fonctions de production après adaptation syntaxique C++ vers JavaScript.
Ils couvrent signe, temps réel, répétitions, expirations, NaN, saturation, rampes, transition, sorties indépendantes, arbitrage, migration et acquisitions radio.
Ils complètent la compilation ESP32 et les régressions existantes ; ils ne valident pas les équipements.

Sur banc isolé et instrumenté, avant exploitation : vérifier JSY puis TIC, les deux SSR simultanés, disparition du surplus, perte mesure/radio, seuil thermique, OFF, passage manuel/AUTO, suppression de règle, changement de source/gains et redémarrage après migration.
Commencer avec une limite de sortie réduite ; conserver les protections thermiques et électriques matérielles indépendantes.
Aucune mise à jour automatique ni téléversement ne fait partie de cette modification. L'installation reste explicite via recovery, avec sauvegarde préalable de la configuration.

## Fichiers concernés

- Moteur : `src/logic/positional_pid.h`, `pid_controller.h/.cpp`.
- Sélection/application par sortie : `rule_engine.h/.cpp`, `automate_manager.h/.cpp`.
- Mesures et cadence radio : `sensor_manager.h/.cpp`, `jsy_mk194t_manager.h`, `linky_tic_manager.h/.cpp`, `espnow_manager.h/.cpp`.
- Suivi de commande et diagnostic : `actuator_manager.h/.cpp`, `runtime_state.h`, `web_ui.cpp`.
- Migration et exemples : `pid_config_migration.h`, `config_manager.h/.cpp`, `config_examples/rules.example.json`.
- Interface : `data/www/app.js`, `style.css`.
- Ancien chemin inactif retiré : `solar_router.h/.cpp` et son instanciation dans le sketch.
- Tests : nouveaux scripts PID et adaptation des régressions concernées ; marqueurs de version firmware/LittleFS alignés.
