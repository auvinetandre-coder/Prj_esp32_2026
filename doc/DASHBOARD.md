# DASHBOARD.md — Cadrage Dashboard

Ajout du 27 octobre 2026 : la carte Reseau conserve les W comme valeur principale et affiche aussi les VA apparents du JSY, calcules par U x I sur la voie configuree `grid`. La valeur apparente reste positive, y compris en injection ; la comparaison avec l'ecran Linky est a faire en soutirage et au meme instant. Une mesure absente, perimee ou un role ambigu affiche « indisponible ». Les logs `JSY decode` incluent `VA=` pour chaque voie. La regulation reste en W. L'API ajoute `CH1_APPARENT` et `CH2_APPARENT` aux valeurs du JSY local, sans modifier les champs existants.

Mise a jour du 21 octobre 2026 : roles et redondance supprimes du firmware, des configurations actives, des API et de la WebUI ; onglet Secours retire. Voir [le contrat actuel et la migration](SUPPRESSION_ROLES.md). Les observations datees du 20 octobre ci-dessous sont historiques pour ces points.

## Observations historiques — 20 octobre 2026

Les sections suivantes sont des objectifs et critères d'acceptation. Le dashboard existe déjà en JavaScript natif avec SVG, Canvas, historique navigateur et réglages ; ne pas interpréter chaque objectif comme un élément absent ou validé. Références : [audit courant](AUDIT_2026-11-04.md), [audit historique dashboard](AUDIT_DASHBOARD_2026-10-19.md), [API](API.md).

Reproduits lors de cette relecture : secours d'erreur PID de signe opposé au firmware, mesure réseau absente affichée comme équilibre. `consumptionW` représente l'achat réseau et ne doit pas être présenté comme consommation totale Maison. Le rafraîchissement partiel ne reconstruit pas tout le bandeau supérieur. Source exacte, commande effective et puissance mesurée/estimée restent à expliciter.

La sélection de type source API en AUTO privilégie désormais la TIC disponible ; l'origine/identifiant exact restent inférés. La page actionneurs possède un polling runtime indépendant à 1 s qui préserve les contrôles, et les commandes Web peuvent être maintenues avec OFF/AUTO. Ce mécanisme ne valide pas la fraîcheur de tout le dashboard et ne constitue pas un ACK distant.

Tests du 20 octobre : syntaxe `app.js`, sept régressions et smoke INIT/OK/CRITICAL avec banc adapté en mémoire réussis. L'ancien smoke local non adapté échoue sur son faux DOM. Aucun contrôle visuel navigateur/téléphone ou de fluidité sur appareil effectué pour cette relecture.

## Configuration des SSR — 3 novembre 2026

L’éditeur d’actionneur propose `MULTI_SINUS` pour le type `SSR`. Le champ `Cycle ms` continue de régler la fenêtre des modes historiques concernés. En `MULTI_SINUS`, le firmware calcule la longueur de trame à partir de la consigne. Voir le [guide SSR](MULTI_SINUS.md).

## Objectif

Le dashboard doit devenir un cockpit énergie moderne :

- simple ;
- visuel ;
- léger ;
- robuste ;
- compatible ESP32 ;
- basé sur SensorManager et `/api/state`.

---

## Règle principale

Le dashboard ne doit pas avoir de logique spécifique ESP-NOW.

Un capteur distant ESP-NOW doit être affiché comme un capteur local, avec simplement un badge d’origine.

Cette règle reprend le cadrage validé : capteurs locaux et ESP-NOW doivent être affichés de la même manière, ESP-NOW restant une origine et non une logique spéciale.

---

## UX cible

- comprendre l’état en 2 secondes ;
- diagnostiquer en 10 secondes ;
- régler le PID en 30 secondes.

---

## Technologies

Autorisé :

- HTML ;
- CSS ;
- JavaScript natif ;
- SVG ;
- Canvas.

Interdit sauf demande explicite :

- framework lourd ;
- dépendance externe inutile.

---

## Bandeau énergie

Afficher :

- Production solaire ;
- Réseau ;
- Chauffe-eau / routeur ;
- Température equipement.

Convention :

- vert : injection / surplus ;
- rouge : achat ;
- orange : charge ;
- bleu : température ;
- gris : inconnu.

---

## Schéma énergétique

Créer un schéma SVG :

```text
Soleil / production
   ↓
Maison
   ↔
Réseau
   ↓
Chauffe-eau
```

Les flèches changent selon :

- achat ;
- injection ;
- routage actif ;
- données absentes.

---

## PID

Afficher :

- mode ;
- source active ;
- consigne réseau ;
- deadband ;
- erreur PID ;
- sortie PID ;
- commande ;
- état ;
- puissance charge estimée.

Graphes :

- erreur PID ;
- sortie PID ;
- énergie.

---

## Capteurs actifs

Afficher tous les capteurs actifs :

- locaux ;
- ESP-NOW ;
- JSY ;
- Linky ;
- DS18B20 ;
- futurs capteurs.

Chaque carte affiche :

- nom ;
- type ;
- origine ;
- état ;
- âge mesure ;
- valeurs principales.

Ne pas limiter au Linky.

---

## Santé système

Afficher :

- Wi-Fi ;
- ESP-NOW ;
- MQTT si présent ;
- sécurité ;
- uptime ;
- heap ;
- capteurs OK ;
- capteurs perdus ;
- dernier défaut.

---

## Graphes

Fonctions recommandées :

- `drawLineChart()`
- `drawAreaChart()`
- `drawPidErrorChart()`
- `drawPidOutputChart()`

Contraintes :

- Canvas natif ;
- responsive ;
- historique court côté navigateur ;
- pas de fuite mémoire ;
- valeurs absentes tolérées.

---

## Critères d’acceptation

- achat/injection visible ;
- routeur en % visible ;
- erreur PID visible ;
- source mesure visible ;
- tous les capteurs actifs visibles ;
- ESP-NOW autre que Linky visible ;
- JSY ESP-NOW visible ;
- DS18B20 ESP-NOW visible ;
- pas de logique métier ESP-NOW dans le dashboard ;
- graphes fluides ;
- code léger.
