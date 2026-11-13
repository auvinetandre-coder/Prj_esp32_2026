# JSY : roles des pinces et automate — livraison 20261025-04

Les numeros de voies restent physiques. Leur usage depend uniquement du role configure :
`grid` = echanges reseau signes, `load` = charge, `production` = production,
`custom` = diagnostic libre. Inverser les deux roles et les deux pinces conserve
le sens des nouvelles mesures dans les scenarios. Aucune configuration existante
n'est modifiee automatiquement ; les anciens defauts production/grid sont conserves.

Dans Capteurs > Modifier JSY, chaque voie peut aussi declarer un `actuatorId`.
Cette association n'est utilisee que pour le role `load`. Exemple : pince 1,
role load, actionneur ssr2 ; pince 2, role grid. La configuration inverse est valide.
Les champs inconnus des voies, leurs noms et leur liste de mesures sont preserves
lors de l'edition. Ce formulaire existe aussi pour un JSY importe manuellement
depuis ESP-NOW ; l'affectation doit etre configuree sur l'ESP qui utilise la mesure.

## Source de verite

`SensorManager::jsyRoleValue` resout les usages a partir des voies configurees.
Il exige exactement une affectation correspondante. Une voie absente, un capteur
desactive, une mesure perimee ou une affectation ambigue donne NAN, traduit en null
dans l'API. Un vrai zero reste une valeur valide. Les conditions numeriques sur une
mesure indisponible sont fausses ; les nouvelles conditions de disponibilite
permettent d'ecrire explicitement les branches SI/SINON.

Les valeurs locales sont conservees dans le pilote JSY, separement des valeurs
globales que d'autres capteurs peuvent actualiser. Les valeurs ESP-NOW viennent
du capteur configure correspondant et sont datees individuellement : recevoir
la voie 2 ne rafraichit pas artificiellement une ancienne valeur de voie 1.
La discovery ajoute des metadonnees sans inventer une mesure egale a zero.
Expiration locale : 3 s ; distante : timeout du capteur ESP-NOW existant.

Les puissances actives brutes du compteur sont conservees separement de l'ancien
secours U*I*PF (utilise quand P est presque nul malgre un courant significatif).
La puissance de charge dite mesuree utilise les watts actifs bruts signes,
jamais le pourcentage SSR ni ce secours. Les champs physiques historiques et
leur comportement sont conserves. L'interpretation reseau historique garde
ce secours ; elle doit toujours etre validee sur le compteur reel.

## Mesures des scenarios

Source JSY-MK-194T, nouvelles mesures :

| Cle | Signification |
| --- | --- |
| jsyGridPowerW | Puissance de la voie au role grid ; positive = achat, negative = injection |
| jsyPurchaseW / jsyInjectionW | Parties achat et injection, positives ; indisponibles si reseau absent |
| loadPowerW | Watts actifs bruts de la voie au role load |
| loadCurrentA | Courant de cette voie |
| loadEnergyWh | Compteur d'energie dans le sens positif de cette voie, en Wh |
| jsyProductionW | Puissance de la voie au role production |
| jsyGridAvailable / jsyLoadAvailable | Valeur valide et affectation non ambigue |

Les mesures generiques par role exigent une seule voie correspondante. Avec deux
voies load, loadPowerW est indisponible ; la puissance par actionneur reste
disponible si chaque voie load est associee a un actionneur different.

Les anciennes cles restent compatibles : activePowerW est toujours la voie 1,
et gridPowerW/injectionW/consumptionW/surplusW restent les valeurs de la source
reseau selectionnee globalement. Leur libelle distingue maintenant ces usages.
Les conditions par role ne basculent pas vers le Linky. Les voies 1/2 restent
accessibles pour le diagnostic. Les actions suivant une mesure indisponible
utilisent une commande nulle au lieu d'une ancienne valeur fixe non verifiee.

## Commande Reguler le surplus

Le nom demande est conserve. A cote de la commande : regulation PID/proportionnelle,
puissance nominale/maximale en W, cible reseau en W et plafond de sortie en %.

`gridSetpointW` surcharge la consigne pour cette action PID sans reecrire les
parametres systeme. Une ancienne action sans ce champ utilise la consigne systeme.
`maxPercent` plafonne PID et proportionnel (100 % par defaut, 0 % autorise).
Le PID borne sa sortie interne et annule l'integrale lorsqu'il atteint ce plafond.
Le mode proportionnel continue de suivre directement le surplus : sa cible PID
n'a pas d'effet. Les gains, filtre et rampes restent ceux du PID global.

La source est affichee sur la ligne mais se choisit toujours dans les parametres
systeme. Pour utiliser le JSY, choisir JSY ; AUTO privilegie toujours le Linky.
Le mode specifique Linky 100 W garde son comportement distinct et sa temporisation.
Pas de modification de l'algorithme general de stabilisation du PID JSY ici.

## API et affichage de la charge

`/api/state`, `/api/status-lite` et `/api/actuators/runtime` exposent pour chaque
actionneur `measuredPowerW` et `measurementAvailable`, en conservant estimatedPowerW.
Le dashboard utilise la puissance mesuree de l'actionneur commande par l'automate
(ou l'actionneur de routage configure si aucune cible precise), sinon l'estimation
issue du pourcentage reellement applique et de sa puissance nominale. Le calcul
PID en arriere-plan ne remplace plus cette estimation pendant un forcage manuel.
Sans puissance nominale connue ni mesure, la valeur est indisponible.
Les libelles distinguent mesure JSY et estimation ; un zero mesure ne declenche
pas de repli vers l'estimation. `outputPowerSource` et `outputPowerActuatorId`
documentent le choix. Les unites et les cles historiques outputPowerW sont preservees.

ESP-NOW conserve la structure et les numeros de protocole. Deux valeurs optionnelles
CH1_W_RAW et CH2_W_RAW completent le deuxieme paquet JSY (8 valeurs maximum).
Pour obtenir la puissance active brute distante, mettre a jour l'emetteur JSY et
le recepteur. Un ancien emetteur reste compatible mais ne fournit pas cette
mesure brute : l'affichage de sortie reste alors explicitement estime.

## Fichiers et validation

Code : pilote JSY, SensorManager, RuleEngine et son branchement dans le sketch,
PIDController (cible/plafond), RuntimeState/AutomateManager (cible de commande),
WebUi (mesures de charge), app.js/index.html et marqueurs de version. Aucun fichier
de /config ni aucune installation sur ESP n'est modifie par cette livraison.

Tests : tools/tests/test_jsy_roles.js execute le resolveur C++ apres adaptation des types
Arduino en JavaScript (pas une execution du firmware sur ESP). Il couvre inversion,
association, watts bruts distincts du secours, zero, absence, ambiguite, capteur
desactive, perte/stale par voie, discovery et conservation des champs dans l'editeur.
Le banc Linky couvre aussi cible par action, plafond, zero et valeurs invalides.
Les 13 scripts test_*.js passent ; controles visuels dans l'apercu local pour le
formulaire JSY et les parametres de commande. Compilation et images verifiees dans
build/ota/release_20261025-04.json.

Essais sur installation apres mise a jour explicite via recovery :

1. Sauvegarder les cinq fichiers /config ; installer firmware et LittleFS -04.
2. Affecter une seule pince grid et une pince load associee au bon SSR ; choisir
   JSY comme source reseau pour le PID. Verifier achat positif et injection negative.
3. Verifier les watts mesures et le pourcentage en manuel, y compris a zero.
4. Inverser roles et emplacement des pinces ; verifier que les mesures semantiques
   conservent le meme sens. Ne pas modifier les anciennes references physiques.
5. Essayer cible 0 W et plafond 50 % ; verifier que la sortie ne depasse pas 50 %.
6. Couper la reception JSY : disponibilite fausse, watts mesures indisponibles,
   affichage explicitement estime et comportement de securite/PID preserve.
7. En ESP-NOW, mettre a jour les deux ESP ; verifier les deux voies, puis interrompre
   leurs mesures pour verifier le timeout. Aucun import automatique.

Les essais electriques et la dynamique reelle de regulation restent a realiser.
