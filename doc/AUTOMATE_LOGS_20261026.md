# Logs de scenarios : repetition corrigee — 26 octobre 2026

## Constat et limites du diagnostic

Le code executait logEvent et le log de setSafetyWarning a chaque evaluation
de leur branche, normalement toutes les 250 ms. Plusieurs lignes/scenarios
pouvaient multiplier les messages. RuntimeState conserve seulement 24 logs
simples et 32 evenements ; il n'y a donc pas d'accumulation illimitee d'entrees.
En revanche, les constructions de String, rotations des buffers et ecritures
Serial repetees polluent l'historique et peuvent augmenter la duree de boucle.
Ce constat de code ne confirme pas la cause d'un blocage/reboot sur un ESP reel.

## Correctif minimal

- Les lignes logEvent et les logs d'alerte s'executent a l'entree dans ALORS/SINON,
  sans repetition periodique si la branche reste active.
- Tous les logs effectivement atteints avant la premiere commande de sortie
  sont conserves dans ce groupe. Les actions apres cette commande restent
  ignorees selon le fonctionnement historique du moteur.
- Un scenario peut produire un nouveau groupe de logs au plus toutes les 5 s.
  En cas d'oscillation, seule la derniere branche observee reste en attente.
  Les transitions intermediaires peuvent donc ne pas etre journalisees.
- Aucun delay ni fichier supplementaire en flash : caches fixes de 24 regles,
  168 octets de champs hors alignement, plus 24 booleens temporaires par evaluation.
- Les regles sont independantes, meme si leurs noms sont identiques.
- Une regle desactivee, invalide, non atteinte a cause d'une priorite superieure
  ou interrompue par Safety est rearmee pour sa prochaine activation, en conservant
  sa limitation temporelle. La sauvegarde explicite des regles remet les caches
  a zero pour ne pas attribuer les anciens etats aux nouvelles lignes/positions.
- L'etat WARNING de setSafetyWarning reste reapplique a chaque cycle. Seul son
  log est limite. Les commandes des sorties, PID et securites restent cycliques.
- Les autres logs du firmware et les buffers existants sont conserves.
- L'appel interne explicite executeAction garde son comportement immediat ;
  la protection ci-dessus concerne l'evaluation cyclique des scenarios.

## Fichiers modifies

src/logic/rule_engine.h et .cpp : suivi de branche et limitation des logs seulement.
src/web/web_ui.cpp : reinitialisation apres sauvegarde reussie des regles.
data/www/app.js : aide des deux commandes mise a jour.
data/www/index.html : cache v51. Marqueurs et images version 20261026-04.
tools/tests/test_scenario_logs.js : tests de regression executant les corps C++ traduits
en JavaScript, sans pretendre executer le firmware sur materiel.

## Validation et essais a faire

Le banc couvre branche stable sur 10000 appels de log, transitions, oscillations,
derniere branche en attente, regles independantes, reprise, limites du tableau,
debordement de millis, alerte maintenue et priorite Safety. Les 17 scripts
test_*.js passent ; compilation et empreintes dans build/ota/release_20261026-04.json.

Installer firmware/LittleFS ensemble via recovery, apres sauvegarde des cinq
fichiers /config ; aucun appareil n'a ete modifie automatiquement.
Sur l'ESP reel, verifier :

1. Un scenario avec log avant sa commande de sortie : un groupe a l'activation,
   aucun nouvel exemplaire tant que la condition reste stable.
2. Un changement ALORS/SINON : nouveau groupe, immediat si 5 s se sont ecoulees,
   sinon groupe de la branche courante lorsque le delai expire.
3. Deux logs distincts dans la meme branche : les deux sont conserves.
4. setSafetyWarning : alerte toujours reevaluee, sans repetition du log stable.
5. Desactiver/reactiver puis modifier/sauvegarder le scenario : rearmement correct.
6. Comparer uptime, heapFree/heapMin, duree de loop et reactivite Web avant/apres.

Si l'ESP reste lent, se deconnecte ou redemarre, relever la version installee,
les raisons de reset et la telemetrie : ce correctif ne prouve pas l'absence
d'une autre cause. Eviter les sondages rapides sur un appareil deja en difficulte.
