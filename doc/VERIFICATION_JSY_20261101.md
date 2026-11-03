# Vérification JSY / SSR — 1er novembre 2026

## Correction précédente effectivement vérifiée

Une capture du 31 octobre à 16:21:54 (heure de Paris), après installation,
avait confirmé le firmware 20261031-01 et la source PID CH2_W_RAW.
Sur 60 instantanés pendant 30 s, aucune avance de la date de calcul n'avait
été observée lorsque l'horodatage de mesure était répété. Safety restait OK.
À une réception, CH2_W_RAW = 1,355 W, CH2_POWER = 957,3931 W, et l'entrée brute
PID = 1,355 W : le remplacement erroné des watts n'alimentait plus le PID.

Les fluctuations demeuraient : écart type brut 436,4 W, filtré PID 285,2 W.
Le filtre de mesure était toujours 869,0148 ms. Ce correctif supprimait une
transformation incorrecte ; il ne constituait pas une stabilisation complète
de la mesure ou de la modulation.

## État relu le 1er novembre

SSR 192.168.0.92 : firmware 20261031-01, LittleFS 20261030-02.
Le capteur radio annonce aussi 20261031-01. Le cycle SSR est toujours 200 ms.
La règle Test est activée, condition jsyGridAvailable, mais son action est
maintenant PROPORTIONAL. La valeur Ki mémorisée est 0,0349 et n'est pas utilisée
dans cet algorithme. Aucun réglage n'a été changé par cette vérification.

PROPORTIONAL utilise `state.surplusW / maxOutputPowerW * 100`, avec 1750 W
de référence, et ne passe ni par gridPowerSample ni par le filtre du PID.
Le défaut métrologique historique peut donc encore concerner cette voie.
Cela n'explique pas à lui seul les fluctuations signalées auparavant en PID
et en manuel : il ne faut pas confondre ces deux sujets.

## Nouvelle capture en observation

Début 1er novembre à 15:31:34 (heure de Paris), période demandée 20 s,
34 réponses HTTP et 33 dates de cache radio distinctes. L'utilisateur avait
annoncé un manuel 40 %, mais tous les instantanés retournent AUTO, PID inactif,
et une commande variable de 0 à 39,48193 %. Ce n'est donc PAS un essai à
40 % constant. Safety est OK sur tous les instantanés.

Comparaison sur 23 points communs après remplissage des fenêtres, moyennes
temporelles par maintien de la dernière puissance brute observée, avec rejet
des intervalles supérieurs à 1000 ms :

| Signal | Écart type | Minimum | Maximum |
| --- | ---: | ---: | ---: |
| CH2_W_RAW | 873,4 W | −775,3 W | +2757,8 W |
| Moyenne 1 s | 560,4 W | −773,5 W | +860,1 W |
| Moyenne 2 s | 341,3 W | −752,5 W | +410,5 W |

Les fenêtres réduisent la dispersion de ces points, mais cette courte capture
non stationnaire ne démontre ni l'absence de biais ni une fenêtre définitive.
Les changements d'onglet et les délais HTTP rendent l'observation irrégulière.
Les dates du cache ne sont pas les identifiants physiques de conversion JSY.

## Point prioritaire : le temps réel du BURST_FIRE

Le champ `performance.loopDurationMs` observé varie de 2 à 261 ms. Il est
déjà lissé par le firmware, et ne constitue pas une mesure directe des fronts
GPIO. Les requêtes HTTP de cette capture durent de 163 à 2209 ms (temps côté
navigateur, distinct du temps CPU bloqué).

Dans RouteurSolaireESP32.ino, `now` est pris avant `webUi.loop()`, Wi-Fi,
ESP-NOW et MQTT. ActuatorManager.loop(now) ne s'exécute qu'ensuite.
Dans updateSSR(), le GPIO est mis à jour selon cet horodatage et le cycle
redémarre à `now` lorsqu'il est dépassé. Il n'existe pas, dans ce chemin,
d'échéance indépendante pour couper une impulsion pendant un traitement Web.

À 40 % d'un cycle de 200 ms, l'impulsion logicielle nominale dure 80 ms.
Une boucle retardée de plus de 80 ms peut décaler un front, prolonger une
impulsion ou en manquer une ; utiliser une ancienne date augmente l'erreur.
La valeur 40 % n'est donc pas une preuve de rapport cyclique électrique 40 %.
Ce mécanisme existe en manuel comme en automatique et mérite une vérification
prioritaire. Les requêtes de diagnostic elles-mêmes peuvent le perturber.

Il s'agit d'une faiblesse du code et d'un risque de gigue étayés par les temps
observés, pas encore d'une preuve que tous les écarts de puissance viennent
du SSR. La fenêtre interne du JSY demeure également inconnue.

**Suite du 3 novembre 2026.** MULTI_SINUS cadence les fronts avec un
timer matériel dédié. BURST_FIRE conserve le cadencement décrit ci-dessus.
Le [guide MULTI_SINUS](MULTI_SINUS.md#pourquoi-la-régulation-pouvait-osciller-en-burst_fire)
explique le lien entre les retards de boucle, la mesure JSY et la commande PID.
Les résultats du 1er novembre restent ceux de l'observation historique.

## Prochain essai nécessaire et correction à préparer

1. Confirmer par l'API controlMode MANUAL et commandPercent 40 sur toute la
   capture, avec l'accord utilisateur et dans un essai instrumenté.
2. Enregistrer les fronts réels et le temps ON dans un tampon borné, puis
   exporter après l'essai. Réduire les lectures HTTP pendant le palier.
   Comparer également dashboard ouvert et sans polling. Un instrument isolé
   est nécessaire pour valider les commutations électriques du SSR.
3. Si la gigue est confirmée, rendre les échéances du BURST_FIRE indépendantes
   des requêtes Web, en préservant OFF prioritaire, sécurité, suppression et
   reconfiguration GPIO, arrêt en cas de perte de commande et états au boot.
   Déplacer un appel ou modifier son timestamp ne garantit pas à lui seul les
   fronts pendant un blocage long.
4. Refaire ensuite la comparaison watts bruts / moyennes 1 et 2 s, puis choisir
   l'entrée moyenne de régulation en conservant les expirations et protections.
   Ne pas augmenter Ki pour compenser une modulation ou une mesure irrégulière.

Aucun nouveau firmware, flash, changement de gain ni commande d'actionneur
n'a été réalisé pendant cette vérification. Le palier manuel demandé n'est
pas validé par les données reçues.
