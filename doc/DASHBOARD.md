# DASHBOARD.md — Cadrage Dashboard

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
- Température ballon.

Convention :

- vert : injection / surplus ;
- rouge : achat ;
- orange : chauffe-eau ;
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
- puissance chauffe-eau estimée.

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
