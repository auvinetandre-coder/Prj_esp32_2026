# AUTOMATE_RULEENGINE.md - AutomateManager et RuleEngine

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
SolarRouter propose.
PIDController calcule.
WebUi demande.
SafetyManager protege.
AutomateManager arbitre.
ActuatorManager applique.
```

Aucune commande actionneur ne doit etre appliquee directement par :

- RuleEngine ;
- PIDController ;
- SolarRouter ;
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
- `SCHEDULE_FORCE`
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
- attendre une demande automatique, manuelle, horaire ou regle.

### AUTO_ROUTING

Mode normal de routage solaire.

La commande est issue de :

- SolarRouter ;
- PIDController.

AutomateManager valide ou refuse cette commande avant transmission a ActuatorManager.

### MANUAL_FORCE

Mode de forçage manuel utilisateur.

Exemples :

- chauffe-eau force a 100 % ;
- SSR coupe ;
- relais force ON ;
- actionneur force OFF.

Ce mode est prioritaire sur le planning, les regles et le routage automatique.

### SCHEDULE_FORCE

Mode de forçage par planning.

Exemples :

- chauffe-eau force de 22h00 a 06h00 ;
- commande reduite sur une plage horaire ;
- mode hiver / ete.

Ce mode est prioritaire sur RuleEngine et AUTO_ROUTING.

### SAFETY

Etat de securite.

Exemples :

- temperature ballon trop elevee ;
- capteur critique perdu ;
- double MASTER detecte ;
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

L'ordre est strict :

```text
SAFETY
ERROR
RECOVERY_REQUEST
MANUAL_FORCE
SCHEDULE_FORCE
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
ANY -> SCHEDULE_FORCE
```

Au demarrage, rester simple. Ne pas creer une logique de transitions trop complexe.

---

## Donnees lues par AutomateManager

AutomateManager lit des informations consolidees.

Sources principales :

- RuntimeState ;
- SafetyManager ;
- SolarRouter ;
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

## Lien avec PIDController et SolarRouter

PIDController et SolarRouter ne doivent plus etre vus comme decideurs finaux.

Ils produisent une proposition :

```text
routingAllowed
pidOutputPercent
heaterPowerTarget
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
SI temperature ballon > 65
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
SI role = MASTER
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
    temperature ballon < 55
    ET
    surplus > 500 W
)
ALORS
    force chauffe-eau 80 %
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
    SI temperature ballon < 55
    ALORS
        force chauffe-eau 100 %
    SINON
        chauffe-eau OFF

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
SI temperature ballon < 50
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

## Etat d'implementation V1

Premiere fondation mise en place :

- `AutomateManager` existe dans le firmware principal.
- Les etats `BOOT`, `INIT`, `IDLE`, `AUTO_ROUTING`, `MANUAL_FORCE`, `SCHEDULE_FORCE`, `SAFETY`, `ERROR`, `RECOVERY_REQUEST` sont definis.
- L'arbitrage applique l'ordre de priorite cible : `SAFETY > ERROR > RECOVERY_REQUEST > MANUAL_FORCE > SCHEDULE_FORCE > RULE_ENGINE > AUTO_ROUTING > IDLE`.
- `RuntimeState` expose l'etat automate, la priorite active, la source de commande, les autorisations PID/routage, et l'etat safety/error.
- `RuleEngine` produit une `RuleDecision` au lieu d'appliquer directement la commande actionneur.
- `Rule`, `Condition`, `Action` et `RuleDecision` sont presents comme fondations pour le futur editeur.
- La V1 sait evaluer une condition simple ou un groupe simple de conditions existant, puis choisir une branche `ALORS`; une branche `SINON` est supportee via `elseActions` ou `else`.
- La commande finale est transmise a `ActuatorManager` par `AutomateManager`.

Limites volontaires de cette V1 :

- pas d'editeur WebUI ;
- pas de simulation de regle ;
- pas de variables virtuelles ;
- pas de groupes imbriques complets ;
- pas de calendrier ;
- pas de notifications ;
- pas de refonte Dashboard ;
- pas de modification Recovery ou ESP-NOW.
