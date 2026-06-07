# ESP_NOW.md - Architecture ESP-NOW

## Objectif

ESP-NOW permet aux ESP32 du projet de communiquer localement sans dependre d'un serveur externe.

Il sert a :

- decouverte ;
- heartbeat ;
- remontee de mesures ;
- annonce de capteurs ;
- annonce d'actionneurs ;
- diagnostics ;
- supervision de redondance.

---

## Principes

- ESP-NOW doit rester leger.
- Les imports de capteurs doivent rester manuels.
- Un capteur ESP-NOW importe doit etre vu comme un capteur local virtuel.
- Le dashboard ne doit pas contenir de logique specifique ESP-NOW.
- ESP-NOW ne doit jamais transporter de firmware `.bin`.
- ESP-NOW ne remplace pas le recovery local.

---

## Trames

Trames connues ou attendues :

- `DISCOVERY`
- `HEARTBEAT`
- `SENSOR_DATA`
- `FAST_DATA`
- `DIAGNOSTIC`
- `SENSOR_DISCOVERY`
- `ACTUATOR_DISCOVERY`

---

## DISCOVERY

La discovery doit annoncer :

- nom ESP ;
- MAC ;
- role ;
- role de redondance si disponible ;
- roles fonctionnels ;
- version firmware ;
- version LittleFS ;
- partition courante si disponible ;
- capteurs exportes ;
- actionneurs exportes.

---

## Import manuel

Workflow :

1. un ESP distant annonce ses capteurs ;
2. la page ESP-NOW affiche les noeuds detectes ;
3. l'utilisateur ouvre le detail ;
4. les capteurs disponibles sont affiches ;
5. l'utilisateur clique sur Ajouter ;
6. le capteur devient virtuel local via SensorManager ;
7. le dashboard l'affiche comme les autres.

---

## Maintenance via ESP-NOW

ESP-NOW ne transporte pas le binaire.

Dans l'architecture Recovery/Main validee, ESP-NOW peut seulement annoncer un etat ou une intention de maintenance. Il ne declenche pas de telechargement GitHub et ne transporte jamais de `.bin`.

Workflow possible :

1. MASTER affiche les versions et l'etat des noeuds ;
2. utilisateur decide une maintenance ;
3. l'ESP concerne passe en recovery ou signale qu'il doit etre maintenu ;
4. la mise a jour se fait localement via le recovery ;
5. l'ESP annonce sa nouvelle version en discovery.
