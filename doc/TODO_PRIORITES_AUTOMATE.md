# TODO_PRIORITES.md

## P1 - Validation Recovery/Main

- Valider le CSV `partitions_recovery_main_4MB.csv`.
- Verifier le CSV reellement utilise dans `build/esp32.esp32.esp32/partitions.csv`.
- Verifier `factory = 0x110000`, `ota_0 = 0x1E0000`, LittleFS = `0x100000`.
- Compiler recovery et main.
- Confirmer que le recovery et le main tiennent dans leurs partitions.
- Tester boot recovery puis boot main.
- Tester reboot normal du main sur `ota_0` et passage recovery explicite depuis le main.

## P2 - BootManager

- Verifier les informations de partition affichees.
- Verifier `setNextBootMain()`.
- Verifier `setNextBootFactory()`.
- Verifier le bouton ou endpoint de passage en recovery.

## P3 - Recovery minimal

- Tester AP `RouteurSolaire_Recovery`.
- Tester Wi-Fi local si identifiants disponibles.
- Tester upload local `firmware.bin` vers `ota_0`.
- Verifier que `factory`, NVS et `/config` restent intacts.
- Surveiller la taille du recovery.

## P4 - AutomateManager

- Lire `AUTOMATE_RULEENGINE.md` avant de coder.
- Creer `AutomateManager` uniquement dans le firmware principal.
- Documenter son role exact cote main.
- Definir les etats `BOOT`, `INIT`, `IDLE`, `AUTO_ROUTING`, `MANUAL_FORCE`, `SCHEDULE_FORCE`, `SAFETY`, `ERROR`, `RECOVERY_REQUEST`.
- Centraliser les transitions de base.
- Centraliser les priorites : `SAFETY`, `ERROR`, `RECOVERY_REQUEST`, `MANUAL_FORCE`, `SCHEDULE_FORCE`, `RULE_ENGINE`, `AUTO_ROUTING`, `IDLE`.
- Garantir que `SafetyManager` garde la priorite absolue.
- Produire une commande finale normalisee vers `ActuatorManager`.
- Interdire le pilotage direct des actionneurs depuis `RuleEngine`, `PIDController`, `SolarRouter` ou `WebUi`.
- Publier l'etat automate dans `RuntimeState`.
- Ajouter l'etat automate dans `/api/state` si cela reste simple et non intrusif.
- Interdire toute dependance recovery vers `AutomateManager`.

## P5 - RuleEngine configurable

- Lire `AUTOMATE_RULEENGINE.md` avant de coder.
- Ne pas coder un moteur complet avant stabilisation d'AutomateManager.
- Preparer le point d'accroche pour une decision RuleEngine.
- Prevoir une decision simple : `ruleActive`, `requestedMode`, `requestedCommandPercent`, `priority`, `reason`.
- Prevoir a terme les regles `SI / ALORS / SINON`.
- Prevoir les groupes `ET / OU`.
- Prevoir l'imbrication de `SI` limitee a 2 niveaux.
- Prevoir les conditions capteurs.
- Prevoir les conditions systeme.
- Prevoir les conditions horaires et calendrier.
- Prevoir les variables virtuelles utilisateur.
- Prevoir la simulation des regles depuis WebUI.
- Garantir qu'aucune regle ne contourne `SafetyManager`.

## P6 - Alleger `webUi.loop`

- Supprimer les traitements bloquants.
- Limiter les recalculs inutiles.
- Garder `/api/state` rapide.
- Verifier heap et duree de loop.

## P7 - Dashboard cockpit energie

- Afficher tous les capteurs actifs.
- Afficher achat/injection, routage, PID et temperature.
- Afficher l'etat AutomateManager quand disponible.
- Corriger les cas JSY/Linky/DS18B20 ESP-NOW.
- Garder le dashboard leger.

## P8 - ESP-NOW

- Stabiliser discovery et heartbeat.
- Ajouter ou verifier versions firmware/LittleFS.
- Garder l'import manuel.
- Ne jamais transporter de `.bin`.

## P9 - MASTER/BACKUP

- Clarifier les roles.
- Detecter double MASTER.
- Definir la strategie de bascule.
- Remonter les alertes de redondance vers SafetyManager si critique.

## P10 - PID et routage

- Parametres PID propres.
- Affichage erreur/sortie.
- Anti-oscillation.
- Rampes.
- Deadband.
- Le PID doit proposer une commande, pas appliquer directement les sorties.
- AutomateManager doit arbitrer avant ActuatorManager.

## P11 - Audit taille firmware

- Mesurer main, recovery et LittleFS.
- Lister les modules optionnels.
- Ne rien supprimer sans validation.

## P12 - Migration ESP32-S3

- Etudier seulement apres validation Recovery/Main ESP32 4 MB.
