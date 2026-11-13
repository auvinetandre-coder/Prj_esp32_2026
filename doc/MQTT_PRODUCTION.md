# Production solaire recue depuis Jeedom par MQTT

Le firmware principal peut utiliser une mesure externe en watts comme source de
production. Reception desactivee par defaut, configuration locale a chaque ESP.
La mesure reseau et le PID ne changent pas de source.

Relecture du 20 octobre 2026 : cette réception ne remplace pas la mesure réseau. L'action récente `setPowerFromLinky` utilise toutefois une production finie strictement positive comme autorisation de chauffe ; si la production MQTT est activée, son indisponibilité/expiration interdit cette action. L'indépendance des sources ne signifie donc pas absence d'effet sur cette régulation. Voir [AUTOMATE_RULEENGINE.md](AUTOMATE_RULEENGINE.md) et l'[audit courant](AUDIT_2026-10-20.md).

Le test local `tools/tests/test_mqtt_production.js` passe pour la configuration WebUI ; `tools/tests/test_linky_routing.js` vérifie les gardes de production dans un modèle logiciel. Aucun échange broker/Jeedom, contrôle de données cloud ou essai ESP n'a été exécuté pendant cette relecture documentaire. Les mentions de scénario Jeedom configuré ci-dessous décrivent le contexte antérieur, pas un état vérifié aujourd'hui.

## Configuration

Dans la page MQTT, activer MQTT et renseigner le broker habituel. Dans le bloc
« Capteur de production solaire MQTT », activer la reception et renseigner :

- topic entrant exact : `maison/solax/productionW` ;
- expiration : `900` secondes par defaut, reglable entre 1 et 86400 secondes.

Sauvegarder. L'abonnement est actualise sans redemarrage (y compris apres un
changement de topic ou une reconnexion). Le topic entrant doit etre distinct des
topics publies par cet ESP et des topics de commande ; pas de joker MQTT.
Plusieurs ESP peuvent ecouter le meme topic avec des Client ID distincts.

La configuration est conservee dans l'objet `mqtt.productionSensor` de
`system.json` : `enabled`, `topic`, `timeoutMs`. Les autres reglages sont preserves.
La production MQTT remplace la production locale/ESP-NOW uniquement lorsqu'elle
est activee. En attente, en erreur ou expiree, elle est indisponible (pas zero et
pas de repli automatique vers une autre production).

## Publication Jeedom

La commande SolaX `Puissance AC` (`ac_power`) est deja en watts. La commande action
MQTT Manager publie un nombre seul sur `maison/solax/productionW`, par exemple
`241` ou `241.5`, sans suffixe W. Conserver Retain decoche : une valeur ancienne
retenue par le broker ne comporte pas de date et serait datee de sa reception.

Le scenario sur evenement deja configure envoie les changements de puissance.
Il faut aussi assurer une publication reguliere des mesures valides meme quand
la puissance est constante (notamment zero la nuit), avant l'expiration choisie.
Ne pas republier aveuglement une ancienne valeur en cas d'echec SolaX.

Important : ce premier format numerique controle le temps depuis la reception,
pas l'age de la mesure cloud SolaX. Le plugin conserve ses anciennes valeurs en
cas d'erreur : verifier cote Jeedom l'etat API et la date des donnees avant une
republication periodique. La source cloud n'est pas adaptee a une regulation rapide.

## Etat et consommation

SensorManager valide le nombre (fini, entre 0 et 100000 W), gere l'expiration et
alimente `productionW`. Les erreurs ne sont jamais converties en 0 W.
Le dashboard et les regles existantes `Solaire / powerW` utilisent cette valeur.
En simulation, les messages reels sont ignores et une nouvelle mesure est requise
a la sortie du mode simulation.

`/api/state` et `/api/status-lite` exposent `mqttProduction` avec `enabled`,
`available`, `status`, `topic`, `powerW`, `lastSeenAgeMs`, `timeoutMs`, `unit`.
Le capteur actif `mqtt_production`, origine `MQTT`, role `production`, est ajoute
a `sensors[]` avec `values.POWER` et ses informations de disponibilite.
Statuts : DISABLED, WAITING, OK, INVALID, INVALID_TOPIC, TIMEOUT, MQTT_DISABLED,
SIMULATION. Une valeur indisponible est exposee en JSON comme `null`.

## Validation

Fichiers de cette evolution : `src/communication/mqtt_manager.*` pour l'abonnement
et la reception, `src/sensors/sensor_manager.*` pour la validation et le capteur,
`src/runtime/runtime_state.h` pour l'etat, `src/config/config_manager.cpp` pour les
valeurs par defaut, `RouteurSolaireESP32.ino` pour l'injection de SensorManager,
`src/web/web_ui.cpp` pour l'API, `data/www/app.js` pour les reglages,
`data/www/index.html` pour le rafraichissement du cache JavaScript et
`tools/tests/test_mqtt_production.js` pour les tests de sauvegarde WebUI.

- `node --check data/www/app.js` et `node tools/tests/test_mqtt_production.js`.
- Compiler le main pour le schema de partitions existant.
- Sur banc : activer la reception, lancer le scenario Jeedom et comparer la
  puissance AC a la production affichee par l'ESP et a `sensors[]`.
- Publier `0`, `241.5`, puis `null`, `abc`, `241 W`, `-1`, `100001`, `NaN`,
  `1e999` et un message de plus de 64 octets. Les deux premiers sont acceptes,
  les autres rendent le capteur indisponible sans commande d'actionneur.
- Choisir temporairement une expiration de 5 s : verifier TIMEOUT, production
  `null`, puis retour OK sur une nouvelle publication. Restaurer ensuite 900 s.
- Tester reconnexion broker, changement de topic, desactivation de MQTT,
  activation/desactivation du capteur, simulation puis retour au reel, reboot.
  Aucun ancien topic ne doit continuer a alimenter la production ; aucune valeur
  n'est conservee en RAM apres reboot. Tester aussi le JSY/ESP-NOW actif pour
  confirmer la priorite de la production MQTT et l'independance de la mesure reseau.

Pour installer cette evolution, mettre a jour le firmware principal et la WebUI
LittleFS via le passage explicite en recovery habituel. Sauvegarder et verifier
la restauration de `/config` avant une mise a jour complete de LittleFS : l'image
generee depuis `data/` contient la WebUI et remplace la partition, pas seulement
les fichiers Web. Aucun televersement n'est effectue par cette modification.
