# Suppression du forcage horaire — 21 octobre 2026

Suppression demandee par l'utilisateur, dans l'interface et dans le firmware.

- WebUI : panneau quotidien, chargement, formulaire, validation et sauvegarde retires.
- Automate : parseur d'heure, execution de la plage, suivi de sa sortie, etat et priorite retires.
- Configuration : aucune lecture, execution ni migration de l'ancien reglage. Sur demande utilisateur, la ligne de nettoyage a egalement ete supprimee. Un ancien fichier peut encore contenir ce champ inerte ; il faut alors le retirer manuellement.
- Tests : remplacement du test de fonctionnement horaire par un test de suppression, d'absence de migration et de rendu de la page Automate.

Le forçage manuel, les regles, les protections et NTP pour l'heure du systeme sont conserves. Les conditions temporelles du moteur de regles sont une fonction distincte de ce panneau quotidien et ne sont pas modifiees.

Aucune reference a l'ancien champ ne subsiste dans le code de production. Les audits dates et anciens binaires restent des archives.

Validation : neuf suites Node, syntaxe JavaScript, compilation ESP32 et extraction/comparaison de LittleFS. Aucun flash effectue. Sur carte, apres sauvegarde de /config et installation explicite du firmware et de LittleFS via recovery : verifier l'absence du panneau,  le fonctionnement des regles et du manuel, et l'absence de declenchement de l'ancienne plage apres redemarrage.
