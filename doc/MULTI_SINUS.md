# Modulation MULTI_SINUS

## Choisir le mode

Dans la configuration des actionneurs de la WebUI, sélectionner le type
`SSR` et le mode `MULTI_SINUS` ou `DEMI_SINUS`. Ces modes sont prévus pour un SSR AC
zero-cross pilotant une charge résistive.

| Mode | Commande |
| --- | --- |
| `BURST_FIRE` | Un bloc ON par fenêtre configurable avec `cycleMs`. Les fronts sont pilotés depuis la boucle du firmware. |
| `MULTI_SINUS` | Un bloc ON par trame variable, cadencé par un timer matériel au pas nominal de 10 ms. |
| `DEMI_SINUS` | Impulsions réparties au pas nominal de 10 ms selon le principe F1ATB. |

Les configurations et règles qui utilisent `TRAIN_ONDES_ENTIERES` ou `ZERO_CROSS_BURST` sont interprétées comme `BURST_FIRE`.

`PHASE_ANGLE` utilise le chemin dédié au RobotDyn et sa détection
zero-cross.

## Pourquoi la régulation pouvait osciller en BURST_FIRE

Sur le routeur 192.168.0.92, le cycle BURST_FIRE était réglé à 200 ms.
À 40 %, la commande nominale vaut 80 ms ON puis 120 ms OFF. Les fronts
dépendent du prochain passage dans la boucle principale, après les
traitements Web et réseau. Le 1er novembre, l'indicateur de durée de
boucle variait de 2 à 261 ms. Un retard pendant le bloc ON pouvait donc
prolonger la chauffe et faire varier le temps ON réel. Cet indicateur
est lissé et les fronts GPIO n'ont pas été mesurés pendant ce relevé.
Voir la [vérification du 1er novembre](VERIFICATION_JSY_20261101.md#point-prioritaire--le-temps-réel-du-burst_fire).

Le JSY mesure une puissance qui varie au fil des salves. En mode PID,
ces variations peuvent se retrouver dans la mesure de régulation et
entraîner de nouvelles corrections de chauffe. Le relevé du 1er novembre
était en mode proportionnel avec une commande variable. Il documente
un risque de décalage des fronts, sans isoler sa contribution aux
oscillations observées auparavant avec le PID.

En MULTI_SINUS, un timer matériel de 10 ms cadence les fronts GPIO
indépendamment du passage dans la boucle. Le JSY peut toujours mesurer
pendant un passage OFF normal. Si la **commande retenue** descend
elle-même vers 0 %, comparer la sortie PID et la branche ALORS ou SINON
au même instant pour distinguer un ajustement du PID d'un arrêt du scénario.

## Demi-Sinus

À chaque pas de 10 ms, le moteur ajoute la consigne entière à un
accumulateur. Il émet un pas ON lorsque le crédit atteint 100 et que
la parité du pas diffère de celle de la dernière impulsion. Il retire
alors 100 du crédit. À forte consigne, les pas ON peuvent se suivre.
À 0 % la sortie est OFF, et à 100 % elle reste ON tant que la commande
est autorisée. Le diagnostic donne la moyenne théorique demandée.

Le moteur alterne la parité de son horloge interne. Celle-ci ne donne
pas la polarité réelle du secteur. Vérifier la conduction et la
répartition du courant sur la charge avec un SSR AC zero-cross.

## Trames et diagnostic

Chaque canal dispose de son propre compteur. Pour une consigne intermédiaire,
le moteur arrondit la consigne au pourcentage entier, entre 1 % et 99 % pour une commande intermédiaire. Comme F1ATB V17.13, il examine les trames de 20 à 100 pas dans l’ordre croissant et retient la première dont l’erreur est inférieure à 0,4 point. Une trame paire exige un bloc ON pair. Une trame impaire admet les deux parités.

| Consigne | Durée ON | Durée OFF | Durée de trame |
| ---: | ---: | ---: | ---: |
| 1 % | 10 ms | 720 ms | 730 ms |
| 25 % | 60 ms | 180 ms | 240 ms |
| 50 % | 100 ms | 100 ms | 200 ms |
| 99 % | 720 ms | 10 ms | 730 ms |

Une modification intermédiaire prend effet à la prochaine frontière de
trame. La commande à 100 % prend effet au prochain pas du timer et maintient
la sortie ON tant que la commande reste autorisée. Une commande explicite
à 0 % révoque la sortie. L'échéance d'une commande locale ou ESP-NOW est
contrôlée par le timer.

Les diagnostics d'actionneur exposent `commandPercent` pour la consigne,
`outputOn` pour l'état logique et `theoreticalDutyPercent` pour le rapport
de modulation appliqué. `timingFault`, `missedSteps` et `maxTickIntervalUs`
décrivent l'état du cadencement. Un intervalle de 20 ms ou plus verrouille
le moteur OFF jusqu'au redémarrage et entraîne le journal
`SSR_TIMING_FAULT`. La coupure sur perte totale de cadence dépend du
délai d'exécution du contrôle dans la tâche `esp_timer` ou dans la boucle.

Sans référence de phase, l'horloge ESP32 peut dériver par rapport au
secteur. La règle de parité favorise l’équilibre quand les pas correspondent aux demi-alternances. Le SSR commute au passage par zéro, mais le nombre de
demi-alternances conduites sur chaque polarité peut différer. Le rapport
théorique décrit le temps logique ON du GPIO. L'énergie délivrée se
mesure sur la charge.

## Livraison du 3 novembre 2026

Les deux images sont dans `build/ota` et se chargent séparément depuis le
Recovery. Voir le [mémo Arduino et Recovery](MEMO_ARDUINO_COMPILATION.md)
pour la procédure et la sauvegarde de `/config` avant remplacement de LittleFS.

| Image | Fichier versionné | Copie courante |
| --- | --- | --- |
| Firmware **20261103-05** | `RouteurSolaireESP32_firmware_20261103-05.bin` | `RouteurSolaireESP32_firmware.bin` |
| LittleFS **20261103-05** | `RouteurSolaireESP32_littlefs_20261103-05.bin` | `RouteurSolaireESP32_littlefs.bin` |

La compilation Arduino-ESP32 3.3.12 a réussi. Le firmware utilise 76 %
de la partition applicative et 24 % de la RAM pour les variables globales.
Les 24 tests JavaScript ont passé, dont les simulations des neuf consignes,
des transitions, des deux canaux et du défaut de cadence. L'image LittleFS
de 1 Mio a été extraite pour vérifier la WebUI et son marqueur de version.
Les essais électriques et les mesures de cadence sur ESP32 restent à faire.

La correction du calcul est comparée à la recherche F1ATB dans le test logiciel pour les 101 consignes entières. Les deux algorithmes sélectionnent les mêmes trames intermédiaires. Les commandes à 0 % et 100 % donnent les mêmes états permanents.

## Courbe réseau JSY

Le graphe réseau utilise les watts actifs signés du canal JSY configuré. La mesure doit être valide et récente. Le graphe laisse un trou lorsqu’elle expire. Le champ `gridGraphPowerW` des API d’état et de temps réel porte cette valeur. Les anciens points enregistrés peuvent encore contenir les pointes de l’estimation.

## Validation sur banc avant utilisation

1. Hors secteur, observer les GPIO à l'analyseur logique. Vérifier les deux
   polarités de commande, les niveaux au démarrage et au reset, puis les
   neuf consignes de [test_multi_sinus.js](../tools/tests/test_multi_sinus.js).
2. Charger simultanément Wi-Fi, MQTT et Web. Mesurer les intervalles,
   puis vérifier la coupure sur expiration ESP-NOW, entrée en simulation,
   arrêt de sécurité pendant ON et perte de cadence. Tester aussi le
   délai de deux secondes en sortie de simulation et la coupure de
   l'ancienne broche lors d'une reconfiguration.
3. Vérifier la référence du SSR, le seuil de son entrée, son niveau de
   repos au reset, l'isolation et la dissipation. Procéder aux changements
   de câblage hors tension. Sur un banc secteur protégé avec instrumentation
   isolée, comparer l'énergie et le courant sur les deux polarités,
   notamment à 1 %, 50 % et 99 %.
