# Analyse JSY / ESP-NOW / PID — 31 octobre 2026

Suite a l'autorisation utilisateur « fait le necessaire », une correction
limitee est preparee dans le firmware 20261031-01 : selection des watts actifs
bruts pour le PID, separation du timeout Modbus et de la fraicheur, diagnostics
additionnels brut/filtre/date du calcul. Les coefficients et le filtre configure
sont conserves. Voir PID_UNIQUE.md. Les constats et captures ci-dessous decrivent
l'etat avant cette correction ; aucune installation sur les ESP n'a ete faite.

## Conclusion et périmètre

Le BURST_FIRE produit une puissance réellement intermittente. Les fluctuations
observées en manuel ne peuvent donc pas être attribuées uniquement aux gains PID.
La réduction du cycle de 1000 à 200 ms est cohérente avec une meilleure moyenne
par le compteur, mais la fenêtre exacte d'intégration JSY reste inconnue.

Un deuxième problème est confirmé sur l'installation : la voie historique
`CH2_POWER`, utilisée par le PID, peut remplacer des watts actifs presque nuls
par une estimation proche de 1 kW. Lisser ce signal ne rétablit pas la bonne
puissance active. Comparer d'abord les watts actifs signés `CH2_W_RAW`.

Aucun firmware, coefficient, configuration, mode ou ordre d'actionneur n'a été
modifié. Deux captures HTTP ont été réalisées dans le mode AUTO déjà actif,
avec le cycle SSR configuré à 200 ms. Aucun essai manuel à 40 %, aucune perte
radio volontaire et aucun essai électrique n'ont été effectués.

## Équipements et paramètres lus

| Élément | Valeur constatée |
| --- | --- |
| ESP JSY | 192.168.0.90, firmware 20261030-01, LittleFS 20261030-01 |
| ESP SSR/PID | 192.168.0.92, firmware 20261030-01, LittleFS 20261030-02 |
| JSY local | readIntervalMs 350, timeoutMs 300, pince 2 grid |
| Export JSY | 200 ms, sur changement, delta minimal 5 W |
| SSR local | ssr2, BURST_FIRE, cycleMs 200 |
| Régulation | Kp 0, Ki 0,005, Kd 0, consigne 0 W, zone morte 30 W |
| Filtre PID existant | constante exponentielle 869,0148 ms |
| PID | intervalle minimal 200 ms, expiration configurée 5000 ms |
| Rampes existantes | montée et descente 5 %/s |

La pince 1 est configurée `load` sur l'émetteur, `production` sur le récepteur.
Cela n'affecte pas la sélection de la pince 2 grid, mais explique que les usages
de la pince 1 ne soient pas cohérents entre les deux interfaces.

## Mesures réelles et limites de la capture

Captures successives, non simultanées :

- SSR : début 16:00:54 heure de Paris, 160 instantanés sur environ 40 s.
- JSY : début 16:02:33 heure de Paris, 175 instantanés sur environ 40 s.

Les lectures HTTP étaient demandées séquentiellement, avec une période cible de
200 ms. Les durées médianes observées étaient 204 ms sur le SSR et 140 ms pour
la paire status/export du JSY. C'est une charge d'observation significative :
ces captures ne prouvent pas la gigue sans client de diagnostic. Le maintien
de l'interface dashboard ajoute aussi ses propres requêtes.

| Grandeur observée | Médiane | P95 | Étendue |
| --- | ---: | ---: | ---: |
| Dates de réponses JSY valides distinctes | 415 ms | 549 ms | 228–892 ms |
| Délai requête → traitement réponse JSY (`jsyPollingMs`) | 102 ms | 213 ms | 100–251 ms |
| Dates `lastSentMs` distinctes de l'export JSY | 420 ms | 599 ms | 178–988 ms |
| Dates de réception du cache distant distinctes | 403 ms | 434 ms | 7–841 ms |
| Dates d'acquisition acceptées par le PID, observées dans l'API | 406 ms | 814 ms | 371–858 ms |

Les intervalles peuvent couvrir des événements manqués entre deux lectures.
Les très petits intervalles de réception ne représentent pas nécessairement
des acquisitions : le JSY est découpé en deux paquets, qui rafraîchissent le
même cache distant. La date par clé existe en interne mais n'est pas exposée
par cette API. `lastMeasurementMs` du PID est la date de mesure acceptée,
pas l'instant exact d'exécution du calcul.

Sur l'ESP JSY : compteur de timeouts resté à 2, CRC resté à 0, aucune observation
hors ligne pendant la capture. Ces compteurs sont cumulatifs ; les deux timeouts
ne datent pas de la capture.

Extraits des horodatages source, en ms depuis son démarrage :

| Réponse valide | Délai Modbus observé | Dernier export observé ensuite |
| ---: | ---: | ---: |
| 76625477 | 101 | 76625508 |
| 76625883 | 101 | 76625891 |
| 76626338 | 108 | 76626348 |
| 76626704 | 124 | 76626712 |

Ces couples sont compatibles avec un export rapide après traitement de la
réponse. Comme les deux API sont lues successivement et sans identifiant
d'acquisition commun, ils ne suffisent pas à certifier une latence événement
par événement. Les captures séparées ne mesurent pas le délai radio aller.

## Ce que fait le code actuel

1. Le sketch appelle SensorManager au mieux toutes les 100 ms. Le réglage JSY
   de 350 ms concerne le début des requêtes : environ 400 ms entre requêtes
   sur une boucle régulière. Les dates de réponses peuvent être plus rapprochées
   si le délai de réponse varie. Le timeout 300 ms n'est pas une fenêtre de mesure.
2. Le pilote date une lecture lorsque la réponse complète est traitée. Il ne
   connaît ni le début ni la durée de conversion interne du JSY. La réception
   série est elle-même traitée sur ce passage périodique. Les octets déjà en
   tampon sont lus avant le test de timeout : 300 ms n'est pas une borne dure
   sur une réponse traitée après un retard de boucle.
3. L'export à 200 ms autorise une émission, mais une garde compare
   `exportLastSampleMs` à `lastJsyReadMs`. Une même réponse Modbus n'est pas
   réexportée périodiquement par ce firmware. Une réponse suivante de même
   valeur reste une nouvelle acquisition logicielle. On ne peut pas distinguer
   une conversion interne JSY répétée d'une valeur physiquement inchangée.
4. Les deux paquets ont des séquences différentes et un horodatage d'émission.
   Aucun identifiant de conversion physique commun n'est transmis. Le récepteur
   date les clés avec son propre `millis()` ; il ne conserve pas l'âge source
   dans la mesure fournie au PID. Une réémission par un ancien émetteur ou un
   doublon reçu pourrait donc être redaté : le cache n'exclut pas les séquences
   répétées avant fusion. La garde à l'émission limite ce risque sur le code lu.
5. L'automate tourne au mieux toutes les 250 ms. `stepPID` ignore une date déjà
   traitée avant de faire avancer I, D, filtre ou rampes. L'intervalle minimal
   de 200 ms ne signifie pas qu'il existe cinq nouvelles mesures par seconde.
6. `applyGridPowerSource()` applique le filtre global alpha à chaque tick
   capteurs, y compris sur une valeur maintenue. Le PID utilise directement
   `gridPowerSample()` puis son propre filtre exponentiel, une fois par
   acquisition acceptée. Ces deux filtres ont des horloges et usages différents.

Références : RouteurSolaireESP32.ino, src/sensors/jsy_mk194t_manager.cpp,
src/sensors/sensor_manager.cpp, src/communication/espnow_manager.cpp,
src/logic/pid_controller.cpp, src/logic/positional_pid.h,
src/actuators/actuator_manager.cpp.

## Anomalie confirmée sur les watts utilisés

À la date de réception SSR 76278919 ms :

- CH2_W_RAW = +2,5227 W ;
- CH2_POWER = +985,5209 W ;
- CH2_CURR = 4,1905 A ; CH2_PF = 0,002.

Le pilote applique `calculateActivePower()` : si |P| < 5 W, I > 0,2 A et U > 50 V,
il estime U × I × PF. Si PF n'est pas supérieur à 0,05, il utilise PF = 1.
Cette règle est compatible avec le saut observé d'environ +983 W. Sous une
modulation intermittente, il n'est pas justifié d'assimiler U × I à des watts
actifs moyens. Ni les gains PID ni un lissage ne corrigent ce remplacement.

Un autre instantané contenait CH2_POWER = +1267,083 W et CH2_W_RAW = −488,4881 W.
Ce couple ne s'explique pas par le secours proche de zéro. Il a été exclu des
comparaisons ; une lecture pendant actualisation du cache est une hypothèse,
pas une cause démontrée. Il faut un snapshot cohérent ou des identifiants
d'acquisition par champ pour conclure.

## Comparaison hors ligne des fenêtres 1 et 2 secondes

Analyse du segment SSR, sur 95 dates communes après remplissage des fenêtres,
en excluant l'instantané incohérent. La moyenne est l'intégrale d'une puissance
signée maintenue entre deux échantillons observés, divisée par 1000 ou 2000 ms.
Une lacune supérieure à 1000 ms invalide la fenêtre dans cet outil hors ligne.
Cette limite d'analyse ne modifie aucun timeout firmware.

| Signal | Moyenne des points | Écart type | Minimum | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Watts actifs bruts signés | +105,6 W | 642,6 W | −607,1 W | +1528,3 W |
| Moyenne temporelle 1 s des watts bruts | +112,3 W | 474,7 W | −606,1 W | +1427,9 W |
| Moyenne temporelle 2 s des watts bruts | +123,1 W | 367,7 W | −576,5 W | +1231,0 W |
| Mesure PID existante, aux instantanés retenus | +136,5 W | 406,7 W | −418,6 W | +1317,3 W |

La fenêtre de 2 s réduit ici l'écart type d'environ 43 %, celle de 1 s d'environ
26 %. Le filtre PID existant est alimenté par CH2_POWER, avec une constante
869 ms ; il ne s'agit donc pas d'une comparaison de filtres à entrée identique.
Les valeurs PID peuvent aussi correspondre à une acquisition antérieure au
dernier paquet du cache. Les moyennes de points de cette table ne sont pas
des bilans énergétiques continus.

Le segment est non stationnaire, avec une commande AUTO variable (23,32 % au
premier instantané, 4,77 % au dernier), et ne valide ni le manuel 40 % ni une
correction définitive. Les captures JSY et SSR ne doivent pas être soustraites
ou comparées comme si elles décrivaient la même période.

## Fenêtre interne et modulation

Le manuel constructeur annonce une actualisation toutes les 330 ms :
https://jsypowermeter.com/uploads/JSY-MK-194T2-Open-CTs-User-Manual.pdf

Cette cadence ne démontre pas une intégration rectangulaire de 330 ms. À 40 %,
le logiciel commande nominalement 80 ms ON / 120 ms OFF pour un cycle de 200 ms,
contre 400 ms ON / 600 ms OFF à 1000 ms. La gigue de boucle s'ajoute aux temps
réels ; le GPIO est commandé en logiciel, sans synchronisation secteur dans
ce chemin. Le comportement électrique du SSR n'a pas été mesuré.

Une fenêtre interne couvrant plusieurs petits cycles donne généralement une
mesure moins dépendante de la phase qu'une fenêtre couvrant une fraction d'un
long cycle. Les horloges JSY, requêtes et SSR restent indépendantes : repliement
spectral et battements sont plausibles. Un moyennage après sous-échantillonnage
ne reconstruit pas une puissance biaisée par un verrouillage de phase.

## Fraîcheur et protections à préserver

Le timeout Modbus et l'âge maximal d'une puissance sont deux paramètres différents.
Or `gridPowerSample()` relit `cfg.timeoutMs` comme TTL. Sur un JSY local configuré
à 300 ms, cela peut refuser une lecture avant la suivante. Sur le SSR observé,
ce champ n'est pas configuré : TTL JSY par défaut 3000 ms, timeout du cache radio
5000 ms, timeout PID 5000 ms. Le délai effectif de la source du PID est donc
3000 ms dans le code lu, sous réserve des passages de boucle. Le piège du
timeout 300 ms n'est pas établi comme cause de ces fluctuations distantes.

La moyenne future ne doit jamais redater la dernière acquisition ni permettre
à son historique de maintenir une source valide. Vérifier la disponibilité et
l'âge avant utilisation ; vider l'historique après invalidité, changement de
source ou redémarrage ; publier une couverture de fenêtre. Une perte doit
continuer à entraîner la coupure existante, sans rampe de filtrage. Les seuils
de tension, fréquence, température, OFF et watchdog actionneur restent séparés.

## Proposition d'étape suivante, sans réglage des gains

1. Instrumenter un petit tampon borné : début requête, réponse valide, compteur
   d'acquisition, watts bruts/historiques, émission et séquence des deux paquets,
   réception par clé, date effective du calcul PID et commande appliquée.
   Exporter le tampon après capture pour limiter l'effet des requêtes HTTP.
   `lastUpdateMs` interne du PID existe mais n'est actuellement pas publié.
2. Conserver l'identité et l'âge source à travers la radio, sans soustraire
   directement les millis de deux ESP non synchronisés. Utiliser âge à l'envoi,
   identité de démarrage et séquence d'acquisition avec une politique de réception
   des doublons/retards. Préserver la compatibilité des paquets et clés historiques.
3. Comparer en observation seule CH2_W_RAW, CH2_POWER, moyenne 1 s, moyenne 2 s
   et filtre exponentiel existant alimenté par les mêmes watts bruts. Faire un
   palier manuel 40 % seulement dans un essai explicitement autorisé et instrumenté,
   avec 0/100 % comme références et vérification de puissance moyenne indépendante.
4. Si le biais d'échantillonnage persiste, privilégier un moyennage proche de la
   source sur conversions réellement nouvelles, avant décimation et transport.
   Ne pas supposer qu'accélérer Modbus crée des conversions JSY indépendantes.
5. Choisir ensuite l'entrée et la fenêtre du PID. Une fenêtre rectangulaire 1/2 s
   ajoute approximativement 0,5/1 s de retard sur un signal correctement échantillonné.
   Ne pas empiler sans mesure ce filtre avec le filtre PID existant de 869 ms.

## Livrables et validation locale

Captures JSON, synthèses JSON et comparaisons CSV : analysis/jsy_pid_20261031/.
Outil reproductible : tools/analyse_jsy_capture.js. Il ne contacte aucun ESP.

Commande : `node tools/analyse_jsy_capture.js capture.json dossier-sortie`.

Tests passés : pondération temporelle signée (durées irrégulières), rejet de lacune,
fenêtre incomplète ; scripts existants test_pid_sampling.js, test_positional_pid.js,
test_jsy_roles.js et test_jsy_apparent_power.js. Ces tests exécutent des adaptations
des fonctions C++ en JavaScript ; ils ne prouvent pas le comportement électrique.
Aucune compilation firmware ni livraison binaire : seul l'outil d'analyse et ce
rapport ont été ajoutés. Les sources/configurations préexistantes sont conservées.
