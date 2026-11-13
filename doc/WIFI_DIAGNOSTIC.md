# Diagnostic Wi-Fi et commandes unifiees — livraison 20261111-03

## Lire depuis la console

Dans Diagnostic > Boite noire, saisir une commande puis Envoyer. Sur USB, utiliser 115200 bauds et une fin de ligne. Le meme moteur traite les deux interfaces.

| Commande | Lecture |
|---|---|
| `wifi` ou `debug wifi` | Capture Wi-Fi detaillee et derniere photo connectee |
| `wifi history` ou `debug wifi history` | 32 derniers evenements, y compris ceux restaures depuis LittleFS |
| `debug status` ou `debug now` | Bilan general ponctuel |
| `debug peers` | Pairs ESP-NOW : enregistrement, interface, canal et age RX |
| `debug events` | Historique RAM des transitions et lenteurs generales |
| `debug on` / `debug off` | Activer / arreter le bilan periodique de cinq secondes |
| `help` / `debug help` | Aide courte / aide avancee |
| `status`, `heap`, `uptime`, `version` | Consultations courtes |

Le Web accuse reception, puis les rapports longs apparaissent progressivement dans le journal serie partage. Un seul rapport est actif : une nouvelle demande peut remplacer celui en cours. Le mode periodique est commun aux clients USB/Web et revient sur OFF au reboot.

Les commandes sont limitees a 47 caracteres, insensibles a la casse, avec espaces aux extremites acceptes. Les entrees binaires, longues ou inconnues sont refusees. L'authentification Web, l'en-tete console et les limites de frequence restent actifs. Aucune commande ne pilote une sortie ni ne modifie la configuration ou la politique radio.

## Informations Wi-Fi

La capture distingue association au point d'acces, IP obtenue/perdue et etat de l'AP de secours. Elle donne statut/mode numeriques du pilote, canal, BSSID, RSSI courant, extrema echantillonnes depuis le boot, IP, masque, passerelle, DNS et nombre de clients AP.

Les compteurs et durees comprennent acquisitions d'IP, deconnexions, pertes d'IP, duree avec IP, coupure en cours, derniere coupure terminee et dernier motif pilote. Un motif d'authentification n'est pas une preuve que le mot de passe est faux ; le code numerique reste conserve.

Les evenements proviennent du callback Wi-Fi, meme quand debug est OFF. Les metadonnees radio sont echantillonnees au plus une fois par seconde, sans scan ni tentative de reconnexion ajoutee. Le callback ne fait aucune allocation, journalisation serie ou ecriture flash. Le RSSI de deconnexion et le BSSID sont captures depuis les donnees du pilote.

La derniere photo connectee reste disponible apres la perte de connexion. L'historique conserve un identifiant de demarrage, l'uptime et l'heure absolue si elle etait deja synchronisee. Les entrees d'un ancien boot n'ont pas d'age calcule a partir de l'uptime courant. Une heure absente reste absente.

Dans Diagnostic > Memoire Wi-Fi, le bouton Actualiser Wi-Fi et historique charge une capture figee, la derniere photo connectee, la chronologie et l'etat de sauvegarde. Il n'y a pas de polling supplementaire de l'historique. `GET /api/diagnostic/wifi` fournit les memes donnees apres authentification, dont `previousBoot` pour le dernier etat sauvegarde avant redemarrage.

## Conservation apres reboot et coupure

Deux fichiers alternes `/diagnostics/wifi-a.bin` et `/diagnostics/wifi-b.bin` contiennent les 32 derniers evenements, le dernier etat echantillonne et la derniere photo connectee. `/config` et NVS ne sont pas utilises par cette sauvegarde.

Chaque copie contient une version de format, une taille, une generation et un CRC32. Au boot, la copie valide la plus recente est chargee. L'ecriture cible l'autre copie, puis la relit et la verifie avant de la declarer sauvegardee. Un enregistrement tronque, de mauvaise version ou de CRC incorrect est ignore. Si aucune copie n'est valide, le statut le signale et un nouvel historique commence.

Les sauvegardes commencent apres trois secondes de fonctionnement, avec au maximum une tentative toutes les dix secondes. Une photo stable est actualisee toutes les cinq minutes. Une coupure brutale peut donc perdre les evenements encore en attente, mais la precedente copie valide reste la solution de repli. La validation reelle de coupures electriques reste a faire sur banc.

`SAUVEGARDE_VERIFIEE` indique une ecriture relue et validee ; `HISTORIQUE_RESTAURE` indique une reprise au boot ; `ERREUR_*` ou `LITTLEFS_INDISPONIBLE` signalent une absence de garantie sur les nouvelles donnees. Les evenements nouveaux en attente sont indiques dans l'API et dans le bilan `wifi`.

Le montage LittleFS normal n'effectue plus de formatage automatique en cas d'echec. Le journal ne formate jamais le stockage et ne supprime pas les configurations. Un remplacement complet de l'image LittleFS efface aussi ces fichiers : sauvegarder separement les diagnostics a conserver lors d'une future mise a jour LittleFS.

L'historique Wi-Fi est borne et persistant. L'historique general de `debug events` reste, lui, uniquement en RAM. `debug off` arrete les rapports periodiques et laisse les deux historiques se mettre a jour.

## Validation

Tests logiciels : dispatch commun, USB partiel/CRLF/casse/limites, commandes Web, anneau borne et chronologique, distinction association/IP, durees et debordement millis, stabilite des captures, derniere photo connectee, CRC32 connu, corruption, ecriture tronquee, schema et generation. Les essais sur ESP32 restent necessaires : deconnexion/reconnexion, perte DHCP, AP, canal, plein disque et coupure pendant une sauvegarde.

Validation locale finale : 31 suites JavaScript reussies ; Main compile avec Arduino ESP32 3.3.12 (1 512 753 / 1 966 080 octets, RAM statique 92 732 / 327 680). Images Main et LittleFS 20261111-03, extraction LittleFS conforme sans /config. Manifeste et empreintes SHA-256 dans build/ota/release_20261111-03.json. Aucun flash ni essai de coupure sur ESP effectue.
