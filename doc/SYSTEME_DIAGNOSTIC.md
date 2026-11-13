# Systeme et Diagnostic — 11 novembre 2026

## Perimetre

Demande utilisateur : supprimer la simulation actuelle, enlever les redondances et proposer une ergonomie plus agreable. Mise en oeuvre en HTML/CSS/JavaScript natifs, sans dependance externe.

| Page Systeme — identifier et maintenir | Page Diagnostic — comprendre un probleme |
|---|---|
| Carte d'identite : nom, MAC, uptime, cause du reset | Bilan de sante : securite, motif, automate et decision |
| Versions : firmware, LittleFS et compilation | Capteurs unifies : origine, etat, age et mesures |
| Carnet d'adresses : mode reseau, IP et heure | Chaine de commande : demande retenue et sortie logicielle |
| Stockage : partition active, prochain boot, marge et LittleFS | Sous le capot : memoire, boucle, temps de construction API |
| Fiche technique depliable : partitions et GPIO | Boite noire : console serie existante |
| Atelier maintenance : LED, redemarrage, passage Recovery | Etat des liaisons Wi-Fi, ESP-NOW et MQTT |

La presentation utilise des pictogrammes et des titres courts. Les couleurs accompagnent des libelles explicites. Les actions de maintenance sont regroupees dans Systeme ; le bouton de redemarrage n'apparait qu'une fois. Les valeurs absentes restent indiquees comme indisponibles ; zero reste une valeur valide. Des versions firmware et LittleFS differentes ne constituent pas automatiquement une erreur.

La page Diagnostic lit le meme etat unifie que le dashboard via `/api/status-lite`. L'API `/api/diagnostic` reste accessible pour le diagnostic brut. Les mesures des capteurs sont affichees avec leur origine ; aucune logique metier speciale ESP-NOW n'est ajoutee au dashboard.

Les pourcentages restent des etats logiciels. Une commande, un retour radio ou un GPIO ne prouve pas la puissance electrique appliquee. La console conserve ses commandes existantes : leur envoi reste une action utilisateur.

## Suppression de la simulation

- Retrait de SimulationManager, de ses appels dans le Main et de son instrumentation.
- Retrait des injections de mesures, des modes et scenarios, des routes `/api/simulation*`, de l'interface principale et de la page `/lite`.
- Retrait des champs runtime et de la publication MQTT de simulation.
- Les anciennes cles systeme `simulationMode` et `simulation` passent par la migration ciblee existante, sans reset de la configuration ni effacement NVS.
- Les regles utilisant `Systeme.simulationMode` ne sont plus valides. Le moteur ignore toute la regle avant ALORS/SINON ; ne pas supprimer automatiquement sa condition pour la rendre vraie. Corriger explicitement les regles concernees dans l'editeur.
- Les anciens points de graphe marques synthetiques restent exclus des archives. Ce filtrage historique ne peut pas activer de simulation.
- Le bit radio historique d'inhibition reste decode pour ne pas presenter la sortie d'un ancien pair comme reelle. Il n'est plus emis ; la taille et les offsets des trames sont conserves. Le diagnostic expose `remoteOutputInhibited`.

Ce retrait est un changement de contrat explicitement demande : installer le Main et LittleFS de cette livraison ensemble. Les anciennes pages et clients de simulation doivent etre adaptes. La nouvelle simulation sera un chantier distinct.

## Securite et validation

Le demarrage conserve `allOff()`. SafetyManager, les validations de mesure, les expirations des commandes, le bail radio et les gardes du SSR restent actifs. Le retrait des branches de simulation signifie que les commandes acceptees concernent les sorties reelles ; aucune execution de commande ou mise a jour sur l'ESP n'a ete realisee pendant ce travail.

Tests logiciels : retrait de la simulation, rejet des anciennes conditions, preservation de l'inhibition radio historique, pages avec donnees absentes/degradees/critiques, echappement HTML et commandes de maintenance uniques. Le test generique a ete resserre sur les anciens champs metier, car les icones de charges ne constituent pas une regression du routage generique.

Compilation Main avec Arduino ESP32 3.3.12 : 1 499 105 octets sur 1 966 080 ; RAM statique 89 644 octets sur 327 680. La compilation a necessite l'execution hors environnement isole en raison d'un echec de chargement de DLL Xtensa dans cet environnement. Aucun flash.

A faire sur banc : verifier l'etat OFF au boot, l'arret systeme OFF, la perte/expiration et le retour des mesures, les commandes locales/distantes, les anciennes regles de simulation, les pages sur mobile, puis le reboot normal en ota_0 et le passage explicite en recovery. Avant LittleFS, exporter les cinq JSON et controler leur restauration.

Un apercu autonome avec donnees fictives est genere dans `build/validation_simulation/preview.html`. Le navigateur integre n'a pas pu joindre son serveur local ; la validation visuelle sur navigateur reste a effectuer. Les tests de generation HTML passent.

Livraison produite : Main et LittleFS `20261111-01`, dans `build/ota`, avec manifeste `release_20261111-01.json`. Les 29 suites JavaScript et le test PowerShell Recovery passent. LittleFS extrait est identique aux fichiers de data/www et ne contient aucun /config.

## Correctif 20261111-02

Diagnostic utilise maintenant systemHealth.espnow lorsque le champ plat espNowReady est absent ; un etat absent est indique inconnu. Initialisation du service radio et reception effective des pairs restent deux informations distinctes. L’aide de la console Web rappelle les cinq commandes autorisees. LittleFS seul 20261111-02, firmware 20261111-01 conserve ; 29 tests passent, aucun flash.
