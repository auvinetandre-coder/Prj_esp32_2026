# Routage generique — 21 octobre 2026

Le routeur ne suppose plus une charge de type particulier. La WebUI et le firmware utilisent les memes noms generiques. Cette evolution de l'API est volontaire, conformement a la demande de suppression des anciens champs : firmware et LittleFS doivent etre mis a jour ensemble.

## Configuration et API

- Puissance : `outputMaxPowerW` dans router, `maxOutputPowerW` dans les actions, `outputPowerW` et `outputPercent` dans les mesures d'energie.
- Temperatures : `temperature1C`, `temperature2C`, `temperature3C`, `primaryTemperatureC` ; usages des sondes `temperature_1`, `temperature_2`, `temperature_3`.
- Limites : `temperatureMaxC` et `tempSafetyMaxC`. La coupure sur depassement et la surveillance des sondes critiques restent actives selon la configuration Safety.
- La sortie principale se choisit dans Parametres avec `router.actuatorId`. Les regles et commandes manuelles gardent leur cible explicite. Une cible vide ne declenche aucune sortie par defaut.
- L'usage d'actionneur est `routing`. Les nouveaux exemples utilisent `ssr1_output`.

## Migration

La migration est appliquee au chargement, a l'import API et a la sauvegarde. Les anciennes cles sont converties puis supprimees. Si une ancienne et une nouvelle cle coexistent, la nouvelle valeur est conservee. Les seuils, GPIO, adresses, options critiques, noms personnalises et identifiants d'equipement restent preserves. Garder les identifiants existants evite de casser les regles, les commandes MQTT et les liens ESP-NOW externes ; ils peuvent donc encore contenir un ancien nom dans une installation existante.

Les noms historiques ne subsistent intentionnellement que dans l'adaptateur d'import `src/config/legacy_config_migration.h`, les tests de migration et les archives d'audit. Les anciennes images binaires versionnees restent des archives. Une image courante inclut donc necessairement les chaines utilisees pour reconnaitre une ancienne configuration.

La migration de chaque fichier au demarrage ecrit un fichier temporaire, verifie sa taille, puis le renomme. Ce mecanisme ne constitue pas une transaction atomique entre les cinq fichiers. Les identifiants ne sont pas renommes entre fichiers. La normalisation ne reaffecte plus automatiquement les GPIO en fonction du nom d'un actionneur.

Les historiques temporaires du navigateur utilisent le schema v5 : ils redemarrent avec les nouveaux noms de mesures. Les graphes enregistres dans system.json sont migres.

## Livraison et verification

- Compiler le main ESP32 et executer tous les `tools/tests/test_*.js`, puis `node --check data/www/app.js`.
- Construire LittleFS depuis `data`, sans injecter de dossier config ; extraire l'image et comparer chaque fichier.
- Exporter les cinq fichiers de configuration avant toute mise a jour LittleFS. Celle-ci remplace la partition complete ; la migration applicative ne remplace pas une sauvegarde.
- Utiliser uniquement le passage explicite en factory recovery pour installer le firmware et LittleFS. Aucun flash n'est effectue par ce travail.
- Sur carte, verifier la conservation des reglages apres redemarrage, une sortie avec un identifiant personnalise, le forcage, les regles, la perte d'une sonde critique, la surtemperature et l'arret de securite. Verifier aussi les imports manuels ESP-NOW et les consommateurs MQTT de l'API.

Les tests scripts executent les fonctions de production adaptees a un environnement JavaScript et des rendus WebUI simules ; ils ne remplacent pas les essais electriques.
