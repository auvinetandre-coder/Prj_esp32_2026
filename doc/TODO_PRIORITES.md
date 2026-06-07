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

- Documenter son role exact cote main.
- Identifier les automatismes existants a rattacher.
- Garantir que SafetyManager garde la priorite.
- Interdire toute dependance recovery vers AutomateManager.

## P5 - Alleger `webUi.loop`

- Supprimer les traitements bloquants.
- Limiter les recalculs inutiles.
- Garder `/api/state` rapide.
- Verifier heap et duree de loop.

## P6 - Dashboard cockpit energie

- Afficher tous les capteurs actifs.
- Afficher achat/injection, routage, PID et temperature.
- Corriger les cas JSY/Linky/DS18B20 ESP-NOW.
- Garder le dashboard leger.

## P7 - ESP-NOW

- Stabiliser discovery et heartbeat.
- Ajouter ou verifier versions firmware/LittleFS.
- Garder l'import manuel.
- Ne jamais transporter de `.bin`.

## P8 - MASTER/BACKUP

- Clarifier les roles.
- Detecter double MASTER.
- Definir la strategie de bascule.

## P9 - Audit taille firmware

- Mesurer main, recovery et LittleFS.
- Lister les modules optionnels.
- Ne rien supprimer sans validation.

## P10 - Migration ESP32-S3

- Etudier seulement apres validation Recovery/Main ESP32 4 MB.
