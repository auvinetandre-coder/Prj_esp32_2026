# Editeur compact de scenarios — 24 octobre 2026

Premiere etape visuelle inspiree de l'organisation Jeedom, avec la palette ESP
existante : fond sombre, jaune pour SI, vert pour ALORS, orange pour SINON.
La page Automate seule utilise une typographie compacte (contenu 13 px,
champs/boutons 12 px). Les autres pages gardent leur presentation.

## Disponible

- Cartes de scenarios : nom, activation, priorite et nombres de lignes.
- Clic sur le titre/carte pour ouvrir ; nouveau clic sur le meme scenario pour
  fermer. Le bouton Ouvrir est supprime. La carte ouverte est surlignee ; les
  modifications sont conservees dans le cache avant fermeture ou changement.
- Conditions actuelles conservees (source, mesure, comparaison, valeur, ET/OU).
- Branches ALORS et SINON repliables. Les actions `elseActions` sont desormais
  editables ; les anciens alias `then` et `else` sont normalises a la sauvegarde.
- Actionneur, commande et parametres cote a cote ; unites visibles. Les deux
  commandes API de pourcentage restent supportees sans doublon dans le menu.
- Fleches monter/descendre et suppression de lignes dans chaque branche.
- Les modifications d'un formulaire sont conservees dans le cache local lors
  du changement de scenario. Rien n'est envoye a l'ESP avant Sauvegarder.
- Les champs supplementaires de regle, condition et action sont preserves lors
  de l'edition. La suppression de toutes les lignes SINON produit un tableau
  vide explicite et ne ressuscite pas l'ancien alias.
- Controle des pourcentages fixes (0–100) et puissances maximales positives.

## Limites explicites de cette etape

Le moteur C++ n'a pas ete modifie. Il reste limite a la premiere commande
actionneur de la branche prioritaire ; il s'arrete a cette commande et ignore
les lignes suivantes. Un avertissement visible signale cet ordre dans l'editeur.
Les evenements/avertissements places avant la commande gardent le comportement
existant. Pas de SI imbrique, de deplacement de blocs par glisser-deposer,
de nouveau planificateur ou de PID independant par sortie dans cette etape.
Les parametres PID restent ceux du systeme : aucune consigne locale fictive.

Le bouton Valider utilise toujours l'endpoint existant, qui valide les regles
deja enregistrees sur l'ESP. Les controles locaux s'appliquent a la sauvegarde.

## Apercu isole et fichiers

Lancer `node tools/preview_automate_server.js`, puis ouvrir
`http://127.0.0.1:8766/preview_automate.html`.
Le serveur ecoute uniquement sur loopback et sert une liste fermee de fichiers.
Il n'a aucun proxy ESP ni endpoint d'ecriture. Le script de production est
utilise sans le demarrage du polling API. Les donnees sont des exemples ;
Sauvegarder affiche le JSON localement. Aucun scenario Jeedom n'a ete modifie.

Fichiers de production modifies : `data/www/app.js`, `data/www/style.css`,
`data/www/index.html` (cache v48). Apercu : `preview_automate.html` et
`tools/preview_automate_server.js`. Tests : `tools/test_scenario_editor.js`,
adaptations des bancs `test_linky_routing.js` et `test_schedule_removal.js`.

## Verification et installation

Syntaxe JavaScript et tous les `tools/test_*.js`. Le nouveau test verifie SINON,
les types de valeurs, les champs supplementaires, les alias, les valeurs hors
limites et l'avertissement d'ordre d'execution. Verification navigateur de
l'ouverture d'un scenario, des parametres de commande et d'une sauvegarde
locale avec SINON. Verifier aussi sur telephone avant installation finale.

Cette etape change uniquement la WebUI : pas de changement de logique C++.
L'utilisateur a valide l'apercu puis demande la compilation le 25 octobre.
Les deux images de livraison portent la version `20261025-01` :

- `build/ota/RouteurSolaireESP32_firmware_20261025-01.bin` : compilation du
  firmware courant avec marqueur de version regenere dans `src/build_info.h`.
- `build/ota/RouteurSolaireESP32_littlefs_20261025-01.bin` : editeur compact
  dans l'interface de production, version dans `data/www/littlefs_version.txt`.

Les noms sans version restent les copies de livraison courantes. Les apercus
et leurs donnees d'exemple ne sont pas inclus dans l'image LittleFS. Celle-ci
contient uniquement les cinq fichiers de `data`, compares apres extraction,
et aucun repertoire `/config`.

Livraison verifiee : compilation ESP32 reussie (1455608 octets de programme,
71280 octets de variables globales), binaire firmware de 1455760 octets et
image LittleFS de 1048576 octets. Les onze scripts de regression passent.
Le clic fermer/rouvrir a aussi ete verifie dans le navigateur avec conservation
d'une puissance modifiee. Les empreintes SHA-256 sont dans
`build/ota/release_20261025-01.json` et les consignes d'installation dans
`build/ota/INSTALLATION_20261025-01.txt`. Aucun essai electrique ni flash effectue.

L'installation demande une sauvegarde des cinq fichiers `/config` et le
passage explicite en factory recovery. Aucune mise a jour automatique ni
ecriture sur l'ESP n'est effectuee par la preparation de cette livraison.
Sur un firmware deja compatible avec le schema courant, seule LittleFS est
necessaire pour cette modification visuelle. La version installee n'a pas
ete verifiee lors de la compilation ; un ancien firmware precedent les
migrations generiques du 21 octobre demande les deux images compatibles.

## Commandes SSR simplifiees — livraison 20261025-02

Les SSR proposent six commandes : Arreter, Pleine puissance (100 %), Puissance
fixe (%), Puissance demandee (W), Regulation du surplus et Regulation Linky
objectif 100 W. Les relais/sorties numeriques ne proposent que les commandes
d'arret et de marche (la marche correspond a 100 %).
La regulation du surplus conserve le PID/proportionnel et la source reseau
du systeme ; pour le JSY, choisir explicitement JSY dans ces parametres.

Les journaux et avertissements sont separes : cible `Actions systeme`, bouton
`+ Action systeme`, message a cote de la commande, aucun actionneur requis.
Une alerte reste un avertissement logiciel et ne coupe pas la sortie. Les
actions systeme gardent leur execution cyclique existante ; ce changement
n'ajoute pas un declenchement sur front. L'ordre avant/apres la premiere
commande de sortie garde la limite et l'avertissement de la version -01.

Compatibilite : `off`, `safetyShutdown` et `setPower` restent sauvegardes avec
leur ancienne syntaxe mais apparaissent sous un seul libelle correspondant.
Les anciennes commandes non proposees (`setMode`, `toggle`, etc.) restent
visibles dans un groupe explicite lorsqu'une regle existante les contient,
sans suppression ni remplacement automatique. `toggle` reste non implemente.
Les boutons et champs SI / ALORS / SINON ainsi que le clic ouvrir/fermer sont
conserves ; aucun fichier de configuration n'est ecrit par le build.

La conversion watts/pourcentage est faite dans ActuatorManager avant le
passage par les champs pourcentage de RuleEngine et AutomateManager :
1200 W sur une charge nominale de 2000 W donnent 60 %, puis une commande
`setPower`, sans seconde conversion. Le reglage nominal de l'action a priorite
sur `maxPowerW` de l'actionneur. Pour les API/regles historiques sans valeur
nominale, le repli historique d'ActuatorManager (1000 W) est conserve.
L'editeur propose la valeur configuree et demande une valeur positive si elle
est inconnue ; il ne presente pas les watts estimes comme une mesure reelle.
Les demandes negatives sont bornees a zero, les depassements a 100 %, et les
valeurs non finies donnent zero. Les commandes on/off publient aussi leur
pourcentage effectif (100/0) dans la decision, meme si leur ancien champ value
etait nul ou different. Toutes les commandes passent encore par l'arbitrage
et ActuatorManager, avec les gardes de securite existantes.

Fichiers : `data/www/app.js`, `data/www/index.html` (cache v49),
`src/actuators/actuator_manager.cpp`, `src/actuators/actuator_manager.h`,
`src/logic/rule_engine.cpp`, les marqueurs de version et ces notes.
Tests : nouveau `tools/test_ssr_commands.js` (menus par cible, alias, conversion
complete jusqu'a FinalCommand, bornes, valeurs invalides et on/off), adaptation
des bancs d'editeur et Linky. Douze scripts passent. Le navigateur local a
verifie les six commandes SSR, le passage a Actions systeme et une sauvegarde
avec message, watts demandes et puissance nominale dans SINON.

Cette livraison demande firmware et LittleFS ensemble, tous deux versionnes
`20261025-02`. Installer les images depuis la factory recovery apres sauvegarde
des cinq fichiers `/config`. Aucun televersement effectue ; essais electriques
et verification sur l'ESP apres installation restent a realiser.

Build -02 verifie : 1455960 octets de programme (74 %), 71280 octets de
variables globales (21 %), image firmware de 1456112 octets. Image LittleFS
de 1048576 octets, ses cinq fichiers extraits sont identiques aux sources.
Empreintes et consignes : `build/ota/release_20261025-02.json` et
`build/ota/INSTALLATION_20261025-02.txt`. La livraison -01 est conservee.
