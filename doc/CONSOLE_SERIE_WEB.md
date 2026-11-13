# Console série Web — 20261110-01

## Correctif de transport — 20261111-04

Le 11 novembre, la lecture sur l'ESP 192.168.0.90 a retrouve les lignes Wi-Fi, avec 17 lignes perdues pendant la lecture de la console. Les rapports etaient emis ; le petit anneau de transport et la lecture de quatre lignes par seconde ne suivaient pas les bilans periodiques.

- Tampon console ESP32 : 96 lignes au lieu de 32. L'historique Wi-Fi structure reste a 32 evenements et n'est pas modifie.
- Reponse HTTP : jusqu'a huit lignes au lieu de quatre, avec un indicateur optionnel `more`.
- Navigateur : rattrapage a 240 ms quand un lot est plein, puis retour a une seconde au repos. Apres erreur, le repli reste d'au moins deux secondes ; il n'y a pas de boucle de requetes rapide sur erreur.
- Paquet HTTP borne a 4 096 octets, controle avec huit lignes maximales echappees en JSON. Le traitement reste non bloquant pour l'envoi socket, sans WebSocket ni tache cliente ajoutee.

Le test de regression reproduit six entrees Wi-Fi suivies d'un bilan et de logs JSY avant lecture : l'ancien tampon perdait les premieres entrees ; le nouveau les restitue dans l'ordre. Il verifie aussi la cadence de rattrapage, le retour au repos et le repli sur erreur. Un debit de logs durablement excessif ou une longue deconnexion peut toujours depasser un tampon borne ; le compteur de lignes perdues reste affiche.

Les deux images Main et LittleFS doivent etre installees pour beneficier du transport et du rattrapage. Avant remplacement de LittleFS, exporter les cinq JSON de /config et les fichiers /diagnostics/wifi-a.bin et wifi-b.bin si l'historique Wi-Fi doit etre conserve pendant cette mise a jour. La conservation apres coupure ne protege pas contre le remplacement complet de la partition LittleFS.

Dans **Diagnostic → Console série**, le terminal affiche les lignes récentes
et se met à jour environ chaque seconde. **Pause** suspend les lectures.
**Reprendre** continue depuis le dernier curseur reçu. **Effacer l’écran**
agit sur l’affichage du navigateur. Entrée envoie une commande et les
flèches haut et bas parcourent les 32 dernières commandes de la page.
Le terminal conserve au plus 240 lignes côté navigateur.

| Commande | Résultat |
|---|---|
| `help` | Liste des commandes de consultation |
| `status` | Wi-Fi, IP, ESP-NOW, MQTT et état de sécurité |
| `heap` | Mémoire libre, minimum et taille du plus grand bloc |
| `uptime` | Temps depuis le démarrage en millisecondes |
| `version` | Version du firmware Main |

Ces commandes utilisent le même code depuis l’USB et le navigateur. Depuis 20261111-03, les commandes debug et Wi-Fi sont aussi disponibles dans la console Web ; voir [les commandes unifiees](WIFI_DIAGNOSTIC.md). Les réponses aux commandes Web
s’affichent dans le terminal qui les a envoyées.

L’heure affichée est celle de l’événement, rendue dans le fuseau du navigateur
quand l’ESP dispose d’une heure valide. Sinon, le préfixe relatif ressemble à
`[+52.756 s]`. Le rapport USB `debug events` ajoute l’heure UTC enregistrée
au moment de la trace si elle était disponible.

## Intégration

Depuis 20261110-02, le bloc Événements est retiré de Diagnostic. La console série Web regroupe les messages du firmware.
Le retrait du journal historique libère 5 528 octets de RAM globale.
Le Main 20261110-02 utilise 1 523 860 octets de flash et 89 856 octets
de RAM globale. L’image LittleFS contient la page Diagnostic simplifiée.

Le firmware Main commun aux ESP32 classiques conserve son WebServer et sa
boucle coopérative. L’adaptateur `Print` transmet d’abord à l’UART physique,
puis assemble une copie des lignes dans un anneau de 96 entrees depuis 20261111-04. La copie
par écriture est limitée à 256 octets. Une ligne garde au plus 191 octets
et le navigateur signale une troncature. Les sections critiques couvrent
l’assemblage ou la copie bornée des entrées et ne réalisent aucun accès réseau.

Les routes HTTP existantes sont complétées par deux routes locales.

| Route | Comportement |
|---|---|
| `GET /api/serial-console?boot=…&after=…` | Au plus quatre lignes, identifiant de démarrage, nouveau curseur, nombre de lignes perdues |
| `POST /api/serial-console/command` | Corps texte de 47 octets au plus, moteur commun USB/Web, liste fermee de commandes de diagnostic |

L’authentification Web doit être activée et validée. Le POST exige
`X-Serial-Console: 1`. Le serveur limite les lectures à une toutes les
160 ms et les commandes à une toutes les 400 ms, tous clients confondus.
Le terminal cesse de sonder hors de la page Diagnostic, en arrière-plan
et en pause. Après une coupure, le délai de reconnexion augmente jusqu’à
environ huit secondes, avec un décalage entre navigateurs. Un changement
d’identifiant de démarrage est signalé.

Le transport réutilise HTTP avec des réponses courtes. Chaque réponse est
tentée une fois avec `MSG_DONTWAIT`. Une réponse incomplète est reprise
depuis le dernier curseur reçu. Aucun socket ni tampon n’est réservé à
un navigateur entre les requêtes. Les requêtes passent par l’analyseur
synchrone de WebServer. Sa durée sous charge et lors de l’arrivée de
clients lents reste à mesurer sur carte.

## Capture et mémoire

La capture concerne les appels `Serial` du Main raccordés à l’adaptateur,
y compris les sorties de `RuntimeState` et de `SerialDebug`. Les sorties
ROM, bootloader, crash bas niveau, bibliothèques écrivant directement sur
l’UART, firmware recovery et exemple ESP-NOW autonome restent accessibles
par leurs moyens habituels. Des impressions en fragments depuis plusieurs
tâches peuvent s’entremêler. L’anneau ordonne les lignes assemblées et
indique le dépassement de sa capacité. Les journaux résident en RAM.

La RAM globale supplémentaire mesurée à la compilation est de 9 592 octets.
Elle comprend le tampon de 32 lignes, un paquet HTTP de 2 112 octets et les
horodatages de l’historique de traces. Une requête ajoute une copie bornée
de quatre entrées sur la pile et la construction du petit document JSON.

## Fichiers modifiés

- `src/diagnostics/console_serial.{h,cpp}` et `serial_capture.h` ajoutent le tampon et l’adaptateur.
- `src/diagnostics/serial_debug.{h,cpp}` partagent les commandes et horodatent les traces.
- `src/web/serial_console_http.cpp` et `web_ui.{h,cpp}` raccordent les routes et le script.
- `RouteurSolaireESP32.ino` et les unités Main de `actuators`, `communication`, `config`, `display`, `logic`, `network`, `runtime`, `safety` et `sensors` raccordent leurs impressions à l’adaptateur.
- `data/www/serial_console.js`, `app.js`, `index.html` et `style.css` intègrent le terminal.
- `src/build_info.h` et `data/www/littlefs_version.txt` portent la version de livraison.
- `tools/tests/test_web_serial_console.js`, `test_serial_debug.js` et `test_serial_trace.js` couvrent les nouveaux chemins et les régressions USB.

## Vérifications logicielles

Compilation ESP32 classique avec Arduino ESP32 3.3.12 et la table du projet.
Le programme utilise 1 530 808 / 1 966 080 octets (77 %) et les variables
globales 95 384 / 327 680 octets (29 %). L’image LittleFS générée fait
1 048 576 octets et contient la WebUI ainsi que son marqueur de version.
27 tests sur 28 passent. Le test de routage générique conserve l’échec
préexistant sur le mot `tank` dans `app.js`.

## Essais sur matériel

1. Sur banc avec sorties de puissance sécurisées, vérifier la page Diagnostic
   sur ordinateur et téléphone puis comparer un même message Web et USB.
2. Tester les cinq commandes, une commande inconnue et l’accès sans identifiants.
3. Tester Pause, Reprendre, Effacer, les flèches et le passage sur un autre onglet.
4. Ouvrir plusieurs clients, interrompre le Wi-Fi, reconnecter et provoquer un
   volume de logs supérieur à l’anneau. Vérifier les lignes perdues et le heap.
5. Comparer les durées de boucle et la cadence capteurs, ESP-NOW et SSR avant
   puis pendant les lectures. La compilation valide l’intégration logicielle.

La livraison comprend deux images séparées pour la recovery. Sur un ESP
configuré, exporter et vérifier la sauvegarde des cinq fichiers de
`/config` avant de remplacer LittleFS, puis vérifier leur restauration.

Validation du correctif 20261111-04 : 31 suites JavaScript reussies ; compilation Arduino ESP32 3.3.12, programme 1 512 873 / 1 966 080 octets, RAM statique 108 028 / 327 680 (15 296 octets de plus que -03). Les sources compilees et le CSV sont identiques au projet, LittleFS extrait correspond a data/www, sans /config ni /diagnostics embarques. Aucun flash et aucune validation du correctif sur ESP effectues.
