Ce dossier est envoye dans la partition LittleFS de l'ESP32.

Contenu autorise ici :
- www/index.html
- www/app.js
- www/style.css

Ne pas placer ici les vrais fichiers de configuration :
- device.json
- system.json
- sensors.json
- actuators.json
- rules.json

Ces fichiers sont crees automatiquement par le firmware dans /config sur l'ESP32.
Les mettre dans data/ risquerait d'ecraser les reglages a chaque televersement LittleFS.
