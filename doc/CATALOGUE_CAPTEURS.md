# Catalogue des capteurs — 21 octobre 2026

Le catalogue de creation et d'import propose uniquement JSY-MK-194T, TIC Linky et DS18B20. ESP-NOW est une origine de ces capteurs. La production MQTT reste configuree dans la page MQTT.

Les anciens types Battery, Solar, Analog, Digital et Virtual ne sont plus proposes. Les branches batterie et solaire dediees ont ete retirees de SensorManager, RuntimeState, des exports/imports ESP-NOW et des affichages. Les numeros radio des autres types et valeurs restent inchanges ; les numeros retires ne sont pas reutilises.

Les puissances production/surplus calculees et utilisees par le routeur restent disponibles. Le type Virtual des actionneurs est une fonction distincte et n'est pas concerne par ce nettoyage des capteurs.

Les nouvelles sauvegardes API refusent tout type de capteur hors catalogue. Une ancienne entree conservee sur un ESP est signalee comme non prise en charge et peut etre supprimee avec le bouton de suppression avant sauvegarde. Aucun effacement automatique de configuration utilisateur n'est effectue, aucune conversion implicite en JSY et aucun import automatique de capteur distant. Verifier les regles et graphes qui referencent une ancienne entree avant de les reutiliser.

Validation : tests du catalogue, filtrage radio, numeros de protocole, rendu des formulaires, regression MQTT/PID/securite, compilation ESP32 et extraction de LittleFS. Les essais physiques restent a effectuer : lectures JSY/Linky/DS18B20, import manuel de chacun via ESP-NOW, production MQTT et arret de securite. Installer ensemble firmware et LittleFS via recovery apres sauvegarde des cinq fichiers /config.
