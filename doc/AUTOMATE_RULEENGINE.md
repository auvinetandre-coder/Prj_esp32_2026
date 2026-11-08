# AUTOMATE_RULEENGINE.md - AutomateManager et RuleEngine

Le mode électrique SSR `MULTI_SINUS` applique la consigne retenue par l’arbitrage existant. Le choix du mode concerne le cadencement de la sortie. Voir le [guide SSR](MULTI_SINUS.md).

## Mise à jour — 30 octobre 2026

Le PID est désormais positionnel et commun à JSY/TIC, avec une instance par sortie et un arbitrage par actionneur. Les réglages sont dans chaque action ; les anciennes commandes Linky sont migrées à l'import. Voir [PID_UNIQUE.md](PID_UNIQUE.md) pour le fonctionnement, la migration et les essais. Les sections datées ci-dessous décrivent les versions antérieures.

## Historique — 20 octobre 2026

Ce document mêle cadrage cible et historique d'implémentation. Le statut courant est celui du [bilan](ETAT_DU_PROJET.md), de l'[audit](AUDIT_2026-10-20.md) et des [priorités](TODO_PRIORITES.md).

Présents : éditeur Web de règles ALORS/SINON avec conditions plates ET/OU, liste vide sauvegardable, automate, manuel Web maintenu sur une cible, action Linky +100 W, gardes PID/SSR contre les valeurs non finies et libération de la précédente cible de règle. Les neuf scripts de regression locaux passent ; pas de validation électrique déduite.

Encore ouverts : priorité système OFF, autorité des commandes radio, décisions multiples, unités watts, écritures répétées de `setMode`, rôles invalides et reconfiguration GPIO. `errorDetected()` retourne encore `false` ; l'état ERROR n'a donc pas une détection générale active. `requestRecovery()` existe, mais l'endpoint Web sélectionne directement la factory et redémarre : son passage par l'état RECOVERY_REQUEST n'est pas garanti.

La commande Linky courante -06 utilise une formule incrémentale, une rampe de montée et une limite de descente séparées. Les paragraphes datés -02 à -05 ci-dessous sont l'historique des corrections, pas des formules simultanément actives. Le maintien `LINKY_HOLD_10S` reste borné et subordonné aux gardes production/sécurité.

La condition historique `timeWindowMatches()` utilise encore l'uptime et n'est pas un calendrier validé via le catalogue/validateur. Groupes imbriqués, variables virtuelles utilisateur et simulation complète d'une règle restent des cibles. Le PID n'a pas de tâche autonome à 100 ms ; les règles/automate sont planifiés à 250 ms et Linky calcule seulement sur nouvelle lecture.

## Objectif

Ce document cadre l'architecture de l'automate central et du moteur de regles du projet RouteurSolaireESP32.

L'objectif est de permettre, a terme, la creation d'automatismes depuis la WebUI sans recompiler le firmware, tout en gardant une architecture simple, lisible et compatible ESP32.

Ce document ne demande pas une refonte du projet. Il fixe les responsabilites et les limites des modules.

---

## Principe general

Le systeme est organise en deux couches :

### AutomateManager

AutomateManager est la machine d'etat centrale.

Il arbitre les modes de fonctionnement, les priorites et la commande finale.

Il ne pilote pas directement le materiel.

### RuleEngine

RuleEngine est le moteur de regles utilisateur.

Il evalue des conditions et propose des actions.

Il ne pilote jamais directement les actionneurs.

Toutes les decisions finales passent par AutomateManager.

---

## Regle d'architecture principale

```text
RuleEngine propose.
Les règles proposent les actions par cible.
PIDController calcule.
WebUi demande.
SafetyManager protege.
AutomateManager arbitre.
ActuatorManager applique.
```

Aucune commande actionneur ne doit etre appliquee directement par :

- RuleEngine ;
- PIDController ;
- WebUi ;
- EspNowManager.

ActuatorManager reste le seul module responsable de l'application physique des sorties.

---

## Etats AutomateManager

Les etats cibles sont :

- `BOOT`
- `INIT`
- `IDLE`
- `AUTO_ROUTING`
- `MANUAL_FORCE`
- `SAFETY`
- `ERROR`
- `RECOVERY_REQUEST`

---

## Role des etats

### BOOT

Etat de demarrage minimal.

Objectifs :

- sorties coupees ;
- initialisation RuntimeState ;
- preparation des modules essentiels.

Transition normale :

```text
BOOT -> INIT
```

### INIT

Initialisation du firmware principal.

Objectifs :

- charger la configuration ;
- initialiser les capteurs ;
- initialiser les actionneurs ;
- initialiser ESP-NOW ;
- initialiser Wi-Fi ;
- initialiser la synchronisation horaire si disponible ;
- verifier que les modules critiques sont disponibles.

Transition normale :

```text
INIT -> IDLE
```

### IDLE

Systeme pret, sans routage actif.

Objectifs :

- surveiller l'etat global ;
- attendre une demande automatique, manuelle ou regle.

### AUTO_ROUTING

Mode normal de routage solaire.

La commande est issue de :

- PIDController.

AutomateManager valide ou refuse cette commande avant transmission a ActuatorManager.

### MANUAL_FORCE

Mode de forçage manuel utilisateur.

Exemples :

- charge force a 100 % ;
- SSR coupe ;
- relais force ON ;
- actionneur force OFF.

Ce mode est prioritaire sur les regles et le routage automatique.

### SAFETY

Etat de securite.

Exemples :

- temperature equipement trop elevee ;
- capteur critique perdu ;
- defaut actionneur ;
- incoherence dangereuse.

En SAFETY, les sorties sont coupees ou limitees selon la strategie definie.

SAFETY est prioritaire sur tout.

### ERROR

Erreur bloquante ou incoherence systeme.

Exemples :

- configuration invalide ;
- module critique indisponible ;
- etat impossible ;
- defaut persistant non recuperable.

### RECOVERY_REQUEST

Demande de bascule vers la procedure de recuperation.

Exemples :

- demande utilisateur depuis WebUI ;
- demande OTA/recovery ;
- firmware principal considere non fiable.

---

## Priorites d'arbitrage

### Historique du routage Linky avant le PID commun

Nouvelle commande de regle `setPowerFromLinky`, libelle WebUI
`Routage solaire — objectif 100 W Linky`. Elle s'utilise dans ALORS sur le SSR
choisi, avec sa puissance maximale en watts. Choisir la source reseau `TIC`
ou `AUTO`, le mode routeur `AUTO` et activer le PID dans Parametres.

Cette commande vise une consommation reseau de **+100 W** : augmentation sous
la cible, diminution au-dessus, maintien dans la bande morte PID configuree
(30 W par defaut, soit environ 70 a 130 W). Elle demarre meme si le Linky ne
fournit jamais de puissance negative. La production est une autorisation,
pas une estimation du surplus : `productionW` doit etre fini et strictement
positif. Avec le capteur MQTT active, sa disponibilite est aussi obligatoire.
Une production nulle, invalide ou expiree coupe la proposition PID a zero ;
une TIC indisponible ou une source JSY selectionnee l'interdit aussi.

Pour adapter une regle existante : conserver ses conditions de temperature et
de securite, remplacer son action par cette commande et retirer la condition
`injectionW > 200` (un Linky restant a zero ne la satisfera jamais).
La commande controle deja elle-meme la presence de production. Appliquer puis
Sauvegarder. Aucune regle ni configuration existante n'est migree automatiquement.

La cible 100 W est propre a cette action, sans modifier `router.gridSetpointW`
ni les autres commandes PID. L'API affiche la cible effectivement utilisee.
Les gains, la bande morte et la rampe existants restent configurables.
Un seul pas de regulation est calcule par nouvel horodatage TIC ; les pas
utilisent depuis `20261020-03` l'intervalle entre lectures TIC (100 a 2000 ms),
sans montee repetee sur une meme lecture lente. L'ajustement doit etre verifie
sur l'installation ; le Linky historique fournit une puissance apparente,
donc ce suivi ne garantit pas une mesure exacte de puissance active en watts.
Les securites et le forcage manuel gardent leurs priorites.

Correction du firmware `20261020-02` : apres une consommation soutenue, une
integrale negative pouvait encore diminuer le SSR alors que le Linky etait
revenu a 0 W. Le test reproduit ce blocage avec la version precedente.
Pour `setPowerFromLinky` uniquement, l'integrale opposee a l'erreur courante
est effacee hors bande morte, et elle n'accumule plus d'erreur poussant une
sortie deja saturee a 0 ou 100 %. Le sens de correction est garanti hors bande
morte : augmentation sous la cible, diminution au-dessus, toujours avec la
rampe configuree et un seul calcul par nouvel horodatage TIC. Les autres
commandes PID conservent leur calcul historique. Cette correction ne demande
que le firmware principal ; la WebUI LittleFS `20261020-01` reste compatible.
Sur ESP : apres une periode de consommation, verifier que le retour a 0 W
relance bien la montee du SSR ; verifier aussi le maintien a 70-130 W,
la reduction au-dessus, les pertes de mesure et la priorite des securites.

Correction du firmware `20261020-03` : le temps PID et la rampe Linky utilisaient
encore 250 ms par calcul, meme avec des mesures recues toutes les 1 ou 2 s,
parce que le temps de boucle etait actualise avant le rejet des lectures deja
traitees. La rampe et l'integrale utilisent desormais l'intervalle entre les
horodatages TIC acceptes, avec un plafond de 2 s pour limiter le rattrapage
apres une interruption. Les autres commandes gardent leur intervalle existant.
La rampe est une limite maximale, pas une vitesse imposee : les gains PID et
la mesure determinent aussi le pas. Le test reproduit une limitation erronnee
a 1,25 point au lieu de 10 points autorises apres 2 s a 5 %/s. Il verifie aussi
la montee au-dela de 80 % avec Linky constamment a 0, les doublons sans montee,
le plafonnement apres une longue interruption et la convergence dans 70-130 W
pour cinq surplus et une mesure retardee de 2 s. Ce modele utilise une puissance
moyenne ; il ne simule pas la metrologie apparente du Linky ni les impulsions
electriques du SSR. Le maintien de la cible doit etre confirme sur l'ESP.
Firmware principal seul a mettre a jour ; LittleFS `20261020-01` compatible.

Evolution demandee pour `20261020-04` : en cas de courte indisponibilite Linky,
une commande deja calculee a partir d'une mesure valide est figee pendant
10 s maximum depuis cette derniere mesure (statut `LINKY_HOLD_10S`). Le PID
n'augmente pas la sortie et n'accumule aucune integrale pendant le maintien.
Une nouvelle mesure valide relance le calcul avec la rampe ; a 10000 ms sans
mesure valide, la commande passe a zero et le PID est reinitialise. Le delai
est controle meme si le drapeau de disponibilite est encore vrai. Aucun
maintien au demarrage sans mesure prealable. Source autre que TIC/AUTO,
mesure non finie, production nulle/invalide/expiree, mode OFF et securites
gardent leurs coupures ; une securite critique peut donc couper avant 10 s.
Le maintien ne contourne pas l'arbitrage manuel ni SafetyManager.
La correction associee des fausses pertes de noeud est decrite dans ESP_NOW.md.
Fichiers de cette etape : `src/logic/pid_controller.cpp`,
`src/communication/espnow_manager.cpp`, `src/build_info.h`,
`tools/test_linky_routing.js`, `tools/test_espnow_node_liveness.js` et ces docs.
Firmware seul a mettre a jour ; WebUI LittleFS `20261020-01` compatible.

Evolution `20261020-05` : les releves en lecture seule montraient environ
1000 W d'achat pendant que le SSR descendait a 5 points par seconde. La meme
rampe limitait la recherche de surplus et la reduction lors d'une consommation.
`setPowerFromLinky` utilise maintenant une descente separee, limitee par
`router.maxOutputDropPercentPerSecond` (100 points/s par defaut si absent).
La montee garde `maxOutputRampPercentPerSecond` (5 points/s par defaut).
Le PID determine toujours la correction : 100 points/s est une limite maximale,
pas une chute imposee de 100 points. Le test a 1000 W reseau, cible 100 W et
commande initiale 70 % passe a environ 50,2 % au premier calcul d'une seconde,
contre 65 % auparavant. La limitation ne s'applique qu'a la commande Linky ;
le calcul JSY historique conserve sa rampe existante. Les delais TIC/ESP-NOW,
le maintien de 10 s lors d'une vraie absence et les securites sont preserves.

Le nouveau champ est optionnel, modifiable dans le JSON avance de `system`,
expose dans `/api/state` et ne provoque aucune migration de `/config`.
Fichiers : `src/logic/pid_controller.cpp`, `src/web/web_ui.cpp`,
`tools/test_linky_routing.js`, `src/build_info.h` et ce document.
Tests : forte consommation, rampe descendante personnalisee, configuration
invalide, montee inchangee et absence de corrections repetees sur un doublon,
en plus des modeles de feedback retarde et des coupures deja verifies.
Sur ESP : verifier une consommation nouvelle puis son retrait, la duree des
depassements reseau et le retour vers la bande 70-130 W. La metrologie Linky
et les impulsions SSR peuvent toujours produire des pointes ; les tests en
puissance moyenne ne garantissent pas 100 W instantanes sur le materiel.
Firmware seul a mettre a jour ; LittleFS `20261020-01` compatible.

Correction `20261020-06` : la trace en lecture seule du firmware -05 montre
une commande a 69,4 %, puis 48,9 %, 24,1 % et 0 %, tandis que les paquets TIC
continuent a annoncer 1148 W pendant plusieurs secondes. Un nouveau paquet
ne garantit pas que la valeur physique a deja reagi a la commande precedente.
La formule ajoutait le terme P complet et l'integrale cumulee a la sortie
precedente a chaque paquet : elle repetait la correction et accumulait deux
fois l'action integrale.

Pour `setPowerFromLinky` seulement, la correction utilise desormais la forme
incrementale `Kp*(e-e_precedente) + Ki*e*dt + Kd*(derivee-derivee_precedente)`.
L'integrale historique n'est pas reutilisee dans cette branche. Une mesure
d'achat constante ne reapplique donc plus le choc proportionnel ; seule
l'action integrale courante persiste. Quand le Linky masque l'injection a
zero, l'exploration progresse a `max(2, Kp*erreur)` points/s, sous la rampe
configurable de montee. La vitesse ne depend plus du nombre de paquets.
Au-dessus de la cible, une baisse de l'achat ne relance pas encore la charge.
La deadband, la rampe de descente configurable, le maintien de 10 s et toutes
les coupures restent applicables. Le PID JSY/surplus historique est conserve.

Fichiers modifies pour -06 : `src/logic/pid_controller.cpp`,
`src/logic/pid_controller.h`, `tools/test_linky_routing.js`, `src/build_info.h`
et ce document. Le test reproduit le plateau 1148 W observe et interdit la
chute repetee jusqu'a zero. Un modele supplementaire simule 5 s de retard,
des mesures physiques maintenues 5 s et retransmises chaque seconde, puis
deux changements de charge, avec charge 1500 et 2000 W ; il verifie le
retour dans 70-130 W. Les sept scripts de regression passent.
Ces modeles de puissance moyenne ne valident pas la metrologie apparente ni
les impulsions du SSR. Sur ESP : comparer plusieurs cycles de variation
solaire et d'ajout/retrait de charge, l'amplitude de la commande et le retour
vers la cible. La validation materielle reste a faire apres installation.
Firmware seul a mettre a jour via recovery explicite ; LittleFS -01 compatible,
aucune ecriture de configuration ni mise a jour automatique.

En TIC standard, une valeur `SINSTI=0` n'efface plus une consommation `SINSTS`
positive ; reciproquement `SINSTS=0` preserve l'injection jusqu'au champ
`SINSTI`. Les deux ordres de champs et le retour a zero sont testes, ainsi que
la compatibilite `PAPP` historique. Cette correction evite de monter le SSR
sur une fausse mesure nulle alors que le Linky mesure deja de la consommation.

### Suppression de la derniere regle

`{"version":2,"rules":[]}` est une configuration valide, sauvegardable et
rechargeable au demarrage. La validation refusait auparavant cette liste et
la protection contre les configurations vides bloquait aussi son ecriture :
l'ancienne regle restait donc sur l'ESP et reapparaissait au rechargement.
Les protections des configurations Wi-Fi, capteurs et actionneurs restent
identiques. Supprimer une regle dans la WebUI puis cliquer sur Sauvegarder.
L'automate coupe egalement l'ancien actionneur de regle lorsqu'elle cesse
de fournir la commande retenue, ou lorsque sa cible change.

Validation locale : `node tools/test_linky_routing.js`,
`node tools/test_manual_actuator.js`, `node tools/test_ssr_nan.js` et compilation.
Les tests Node traduisent les corps C++ et executent les fonctions WebUI ; ils
ne remplacent pas un essai sur ESP. Sur banc : production positive / Linky 0,
100, puis 400 ; production 0, expiree, puis retour ; perte TIC ; mode OFF ;
securite ; forcage manuel ; suppression de la derniere regle suivie
de Sauvegarder, rechargement et reboot. Tester aussi un SSR2 comme cible.
Firmware et WebUI sont necessaires ; mise a jour via recovery explicite avec
preservation/restauration de `/config`, aucune mise a jour automatique.

Fichiers de cette evolution : `src/config/config_manager.cpp` pour accepter
les regles vides ; `src/logic/automate_manager.*` pour liberer l'ancienne sortie ;
`src/logic/pid_controller.*` et `src/logic/rule_engine.cpp` pour la nouvelle
commande ; `src/runtime/runtime_state.h` et `src/web/web_ui.cpp` pour la cible
PID effective ; `src/sensors/linky_tic_manager.cpp` pour les champs TIC standard ;
`data/www/app.js` et `data/www/index.html` pour le choix de la
commande et son cache ; `tools/test_linky_routing.js`,
`tools/test_manual_actuator.js`, `tools/test_ssr_nan.js` pour la verification.

### Commande manuelle maintenue

Le bouton `Commande manuelle` remplace le test fixe 25 %. Le panneau propose un
curseur 0-100 %, une saisie numerique et deux actions : `OFF`, `AUTO`.
Le curseur et la saisie appliquent automatiquement la consigne, avec regroupement
des changements sur 150 ms et envoi de la derniere valeur au relachement.
Les requetes sont serialisees ; OFF et AUTO annulent les anciennes valeurs
en attente pour eviter un rallumage tardif. La consigne demande un forcage
maintenu dans la RAM de l'ESP, sans requetes periodiques du navigateur.
OFF maintient 0 % pour que les regles ne rallument pas la sortie ; AUTO libere
le forcage et rend la main aux regles. Une seule sortie peut
etre forcee a la fois : changer de cible coupe l'ancienne sortie.

L'automate renouvelle l'ordre maintenu a chaque calcul ; le timeout actionneur
reste actif et n'est pas allonge. Securite, erreur, recovery, desactivation de
l'actionneur, changement reel/simulation et reboot annulent le maintien.
Le maintien n'est jamais sauvegarde dans `/config`.

L'API historique `/api/actuator/command` reste compatible. Le parametre optionnel
`controlMode=manual` active le maintien avec une valeur numerique finie entre
0 et 100 ; `controlMode=auto` le libere. Sans ce parametre, la commande reste
ponctuelle, y compris les commandes MQTT. Le runtime expose `manualOverride`,
`manualPercent` et la source `MANUAL`.

Verification : `node tools/test_manual_actuator.js`, compilation, puis sur ESP
maintien au-dela de 5 secondes et apres fermeture de la page, OFF sans reprise
par les regles, AUTO, desactivation et interruption par securite.

### Mesures invalides et affichage des actionneurs

Une mesure reseau non finie (`NaN`, infini) coupe la sortie du PID a 0 et
reinitialise son integrale et son historique. Les statuts `INVALID_MEASUREMENT`,
`INVALID_PID_CONFIG`, `INVALID_PID_STATE` et `INVALID_PID_RESULT` rendent le
defaut observable. Avec des donnees valides, la regulation repart depuis zero
en respectant la rampe. ActuatorManager rejette aussi les commandes non finies
et le SSR reste OFF avant tout calcul de duree du burst.

La page Actionneurs actualise les pourcentages chaque seconde sans reconstruire
le formulaire. Elle distingue le pourcentage commande de l'etat instantane ON/OFF.
Une valeur non numerique affiche `Commande invalide`, jamais un faux `0 %`.
En cas d'erreur de lecture runtime, les cellules affichent des donnees indisponibles.
Les watts affiches sont estimes a partir du pourcentage et de la puissance maximale
configuree ; ils ne constituent pas une mesure de consommation.

Verification locale : `node tools/test_ssr_nan.js`, puis compilation ESP32.
Le test Node execute les corps arithmetiques C++ traduits mecaniquement en JS
et les fonctions reelles de l'interface ; il ne remplace pas un essai materiel.
Sur ESP : verifier la perte puis le retour du capteur reseau, l'absence de `nan%`,
la coupure du SSR pendant le defaut et le rafraichissement du pourcentage affiche.

L'ordre est strict :

```text
SAFETY
ERROR
RECOVERY_REQUEST
MANUAL_FORCE
RULE_ENGINE
AUTO_ROUTING
IDLE
```

Aucune regle utilisateur, commande manuelle, commande PID ou commande ESP-NOW ne peut contourner SafetyManager.

---

## Transitions minimales

Transitions a prevoir :

```text
BOOT -> INIT
INIT -> IDLE
IDLE -> AUTO_ROUTING
AUTO_ROUTING -> IDLE
ANY -> SAFETY
ANY -> ERROR
ANY -> RECOVERY_REQUEST
ANY -> MANUAL_FORCE
```

Au demarrage, rester simple. Ne pas creer une logique de transitions trop complexe.

---

## Donnees lues par AutomateManager

AutomateManager lit des informations consolidees.

Sources principales :

- RuntimeState ;
- SafetyManager ;
- PIDController ;
- RuleEngine ;
- RedundancyManager ;
- ActuatorManager ;
- WebUi.

AutomateManager ne doit pas lire directement :

- les GPIO ;
- les trames ESP-NOW ;
- les bus capteurs ;
- les fichiers JSON bas niveau si un manager existe deja.

---

## Donnees ecrites par AutomateManager

AutomateManager publie dans RuntimeState :

- `automateState`
- `previousAutomateState`
- `automateReason`
- `activePriority`
- `commandSource`
- `commandPercent`
- `pidAllowed`
- `routingAllowed`
- `safetyActive`
- `errorActive`
- `lastTransitionMs`

AutomateManager transmet a ActuatorManager une commande finale normalisee.

---

## Commande finale normalisee

Structure conceptuelle :

```text
enabled
commandPercent
commandSource
reason
safetyLimited
```

Sources possibles :

```text
NONE
AUTO_ROUTING
PID
RULE
MANUAL
SCHEDULE
SAFETY
ERROR
RECOVERY
```

---

## Lien avec ActuatorManager

ActuatorManager applique uniquement la commande finale validee.

Il reste responsable :

- SSR ;
- relais ;
- RobotDyn ;
- PWM ;
- actionneurs locaux ;
- actionneurs distants si prevus plus tard.

AutomateManager ne pilote jamais directement une sortie.

---

## Lien avec SafetyManager

SafetyManager garde la priorite maximale.

Si SafetyManager signale un danger critique :

```text
AutomateManager -> SAFETY
commande finale -> 0 %
ActuatorManager -> coupe les sorties
RuntimeState -> expose la raison
```

---

## Lien avec PIDController

PIDController calcule la demande pour l'action retenue par l'automate.

Ils produisent une proposition :

```text
routingAllowed
pidOutputPercent
outputPowerTarget
reason
```

AutomateManager decide si cette proposition devient la commande finale.

---

## Lien avec RuleEngine

Dans un premier temps, RuleEngine peut rester simple.

Il doit produire une decision exploitable :

```text
ruleActive
requestedMode
requestedCommandPercent
priority
reason
```

AutomateManager arbitre ensuite cette decision.

---

# RuleEngine configurable

## Objectif

Permettre de creer des regles depuis la WebUI.

Format general :

```text
SI condition
ALORS action
SINON autre action
```

---

## Conditions capteurs

Exemples :

```text
SI temperature equipement > 65
SI puissance reseau < -500
SI production solaire > 1500
SI capteur perdu
SI age mesure > 5000 ms
```

---

## Conditions temporelles

Le RuleEngine doit permettre :

- heure exacte ;
- plage horaire ;
- jour de semaine ;
- jour du mois ;
- mois ;
- date ;
- periode annuelle.

Exemples :

```text
SI heure entre 22h00 et 06h00
SI lundi au vendredi
SI date entre 01/11 et 31/03
```

Condition obligatoire :

Si l'heure n'est pas valide, les regles horaires ne doivent pas s'executer.

Le statut doit etre expose dans RuntimeState :

```text
timeValid
```

---

## Conditions systeme

Exemples :

```text
SI automate = AUTO_ROUTING
SI securite active
SI mode systeme = AUTO
SI role = BACKUP
SI Wi-Fi connecte
SI ESP-NOW perdu
```

---

## Groupes ET / OU

Le RuleEngine doit supporter les groupes de conditions :

```text
SI
(
    temperature equipement < 55
    ET
    surplus > 500 W
)
ALORS
    force charge 80 %
```

---

## Imbrication

Les SI imbriques sont autorises avec une limite stricte :

```text
maximum 2 niveaux
```

Exemple :

```text
SI heure entre 22h00 et 06h00

ALORS
    SI temperature equipement < 55
    ALORS
        force charge 100 %
    SINON
        charge OFF

SINON
    AUTO_ROUTING
```

Cette limite permet de garder une WebUI comprehensible et un code maintenable sur ESP32.

---

## Actions

Actions possibles a terme :

### Automate

- passer en AUTO_ROUTING ;
- passer en IDLE ;
- demander MANUAL_FORCE ;
- demander SAFETY.

### Actionneurs

- SSR ON/OFF ;
- relais ON/OFF ;
- PWM % ;
- RobotDyn %.

### Variables virtuelles

- affecter une valeur ;
- incrementer ;
- decrementer ;
- mettre TRUE/FALSE.

### Systeme

- ajouter un log ;
- envoyer une notification ;
- lever une alarme.

---

## Variables virtuelles

Le RuleEngine doit permettre des variables internes utilisateur.

Exemple :

```text
SI temperature equipement < 50
ALORS besoin_chauffe = TRUE
SINON besoin_chauffe = FALSE
```

Puis :

```text
SI besoin_chauffe = TRUE
ET surplus > 300
ALORS AUTO_ROUTING
```

---

## Simulation de regle

La WebUI devra proposer un test de regle.

Exemple de resultat attendu :

```text
Condition heure : TRUE
Condition temperature : FALSE
Branche executee : SINON
Action proposee : AUTO_ROUTING
```

La simulation ne doit pas appliquer les sorties.

---

## Interface WebUI cible

Interface par blocs configurables :

- bloc SI ;
- bloc ALORS ;
- bloc SINON ;
- bouton ajouter condition ;
- bouton ajouter groupe ET/OU ;
- bouton ajouter SI imbrique ;
- bouton ajouter action ;
- bouton tester ;
- bouton activer/desactiver.

Eviter le texte libre pour la logique critique.

Utiliser prioritairement :

- listes deroulantes ;
- selecteurs de capteurs ;
- selecteurs de valeurs ;
- operateurs ;
- seuils numeriques ;
- plages horaires ;
- cases jours de semaine.

---

## Contraintes

- Ne pas refondre le projet.
- Ne pas casser le recovery.
- Ne pas ajouter de dependance lourde.
- Ne pas modifier ESP-NOW dans cette etape.
- Ne pas refaire le dashboard dans cette etape.
- Ne pas piloter les actionneurs hors ActuatorManager.
- Limiter l'imbrication a 2 niveaux.
- SafetyManager reste prioritaire.

---

## Criteres d'acceptation AutomateManager

- AutomateManager existe.
- Les etats sont definis.
- Les priorites sont centralisees.
- Safety est prioritaire.
- Les commandes finales passent par AutomateManager.
- ActuatorManager reste le seul module qui applique les sorties.
- RuntimeState expose l'etat automate.
- L'existant continue de fonctionner.
- L'architecture prepare le futur RuleEngine configurable.

---

## Criteres d'acceptation RuleEngine futur

- Creation de regles depuis WebUI.
- Conditions capteurs.
- Conditions horaires.
- Conditions systeme.
- Groupes ET / OU.
- SI / ALORS / SINON.
- Imbrication limitee a 2 niveaux.
- Variables virtuelles.
- Simulation de regles.
- Aucune regle ne contourne SafetyManager.

---

## Historique de la fondation V1

Premiere fondation mise en place :

- `AutomateManager` existe dans le firmware principal.
- Les etats `BOOT`, `INIT`, `IDLE`, `AUTO_ROUTING`, `MANUAL_FORCE`, `SAFETY`, `ERROR`, `RECOVERY_REQUEST` sont definis.
- L'arbitrage applique l'ordre de priorite cible : `SAFETY > ERROR > RECOVERY_REQUEST > MANUAL_FORCE > RULE_ENGINE > AUTO_ROUTING > IDLE`.
- `RuntimeState` expose l'etat automate, la priorite active, la source de commande, les autorisations PID/routage, et l'etat safety/error.
- `RuleEngine` produit une `RuleDecision` au lieu d'appliquer directement la commande actionneur.
- `Rule`, `Condition`, `Action` et `RuleDecision` sont presents comme fondations pour le futur editeur.
- La V1 sait evaluer une condition simple ou un groupe simple de conditions existant, puis choisir une branche `ALORS`; une branche `SINON` est supportee via `elseActions` ou `else`.
- La commande finale est transmise a `ActuatorManager` par `AutomateManager`.

Limites volontaires de cette V1 :

- pas d'editeur WebUI lors de cette fondation (editeur present depuis ; voir l'etat courant en tete) ;
- pas de simulation de regle ;
- pas de variables virtuelles ;
- pas de groupes imbriques complets ;
- pas de calendrier ;
- pas de notifications ;
- pas de refonte Dashboard ;
- pas de modification Recovery ou ESP-NOW.
