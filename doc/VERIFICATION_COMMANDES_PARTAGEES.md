# Verification des commandes partagees — 26 octobre 2026

Version : firmware et LittleFS `20261026-03`.

## Constat

Le retour de puissance fonctionnait, mais AUTO sur le SSR local n'annulait
pas le forçage persistant du pilote. Le pilote renouvelait donc sa consigne.
La correction distingue l'intention utilisateur, le renouvellement de session
et l'etat annonce par le proprietaire physique. Les mises a jour visuelles
n'emettent pas de commande.

## Validation locale

`tools/tests/test_shared_actuator_control.js` execute les corps de production de
`requestSharedControl`, `handleEspNowActuatorCommand`, `observeSharedControl`
et l'expiration dans deux contextes representant le pilote et le SSR physique.
Les adaptateurs Arduino/JSON et la livraison radio sont simules. Le controleur
`SharedActuatorControl` est traduit mecaniquement depuis son en-tete C++.

| Scenario | Resultat local |
|---|---|
| Pilote MANUEL 49 %, AUTO sur le SSR physique | Mode AUTO, ancienne session annulee sur le pilote |
| 20 anciens renouvellements 49 % apres cet AUTO | Tous refuses, aucune reactivation |
| SSR physique MANUEL 40 %, AUTO sur le pilote | Manuel physique annule, sortie relachee |
| OFF physique apres MANUEL du pilote | Ancienne session refusee, retour OFF |
| OFF du pilote apres MANUEL physique | Ancien manuel physique annule, retour OFF |
| Nouveau geste MANUEL apres OFF | Nouvelle session distincte acceptee hors Safety |
| Jeton correct avec une autre MAC | Refuse |
| Changement de mode pour une autre sortie | Son manuel reste preserve |
| Safety active sur le SSR | Nouvelle puissance refusee |
| Simulation sur pilote ou recepteur | Aucun pilotage reel issu de la simulation |
| Perte des renouvellements et rebouclage d'horloge | Coupure a l'expiration, session annulee |
| Reconnexion avec ancien jeton | Reprise manuelle refusee |
| Intention MANUEL perdue sur la radio | Renouvellements refuses, pilote annule sans activation |

`tools/tests/test_espnow_actuator_state.js` verifie le mapping MAC/ID, la reception
d'etat, son expiration, la projection API, le diagnostic actualise et les
curseurs des panneaux. `tools/tests/test_manual_actuator.js` couvre le maintien
manuel, l'annulation des anciennes requetes et OFF/AUTO pendant une reponse lente.

## Essais materiels a realiser

La radio, le SSR physique et la charge ne sont pas valides par ces tests.
Utiliser un banc instrumente avec protections et isolement adaptes. Aucune
commande ni aucun flash materiel n'a ete realise par Codex.

1. Installer la meme version sur les deux ESP, sauvegarder puis verifier les
   configurations, les peers mutuels, l'export et l'import explicites du SSR.
2. Ouvrir Actionneurs et Diagnostic sur les deux interfaces. Depuis le pilote,
   demander 49 %, puis AUTO sur le SSR physique : verifier mode AUTO sur les
   deux, disparition du forçage du pilote, absence de reprise a 49 %.
3. Faire l'inverse : 40 % sur le SSR physique, puis AUTO sur le pilote.
4. Refaire les deux sens avec OFF, puis avec une nouvelle consigne explicite.
5. Verifier qu'AUTO autorise les regles et que son retour peut donc etre non nul.
6. Couper la radio : verifier expiration de commande, indisponibilite d'etat,
   puis absence de reprise de l'ancien manuel au retour du lien.
7. Tester les redemarrages separes, la simulation et Safety sur sorties de test.

Conserver les images precedentes et les sauvegardes propres a chaque ESP pour
un retour arriere par recovery. Une paire de versions mixtes ne valide pas
la coordination globale MANUEL/OFF/AUTO.
