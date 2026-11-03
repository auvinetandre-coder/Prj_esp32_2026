# Installation ESP32 par phases

Lanceurs numerotes a la racine, pour ESP32 classique 4 Mo.
Fermer le moniteur serie. Installer sur banc, sorties de puissance deconnectees.
Chaque phase s'arrete en cas d'erreur : corriger avant de passer a la suivante.

| Phase | Lanceur | Utilisation |
|---|---|---|
| 00 | `00_Preparer_Arduino.bat` | Facultatif : ajoute le menu de partitions sur le PC. Ne flashe pas l'ESP. Redemarrer Arduino IDE ensuite. |
| 01 | `01_Effacer_ESP32_OPTIONNEL.bat` | Remise a zero volontaire. Efface TOUT, NVS et `/config` compris. Confirmation `EFFACER COMx`. |
| 02 | `02_Installer_Recovery.bat` | Compile puis installe bootloader, table et recovery. Demande le port et la confirmation `INSTALLER COMx`. Carte neuve/effacee uniquement. |
| 03 | `03_Generer_Firmware_et_LittleFS.bat` | Genere les deux images avec une nouvelle version commune. Aucun flash ni publication. |
| 04 | `04_Installer_Depuis_Recovery.bat` | Affiche les fichiers et les instructions pour les charger manuellement depuis la recovery. |

Pour preciser le port : `02_Installer_Recovery.bat -Port COM4`.
La phase 02 compile une copie du sketch et du CSV dans `build/installation` ;
elle ne modifie pas le core Arduino installe. La phase 00 est donc facultative.

## Premiere installation

1. Phase 01 seulement si un effacement total est souhaite.
2. Phase 02. Verification des fichiers avant ecriture USB a 115200 bauds.
3. Verifier les logs serie a 115200 bauds et le reseau `RSrecoverymode_XXXX`.
4. Phase 03, puis 04 : ouvrir `http://192.168.4.1/recovery` via cet AP,
   ou utiliser l'IP locale affichee dans les logs.
5. Sur le nouvel ESP, charger `build/release/littlefs.bin`, puis
   `build/release/firmware.bin`, separement. Demarrer le firmware principal.
6. Verifier `/app`, les versions, LittleFS et le retour sur `ota_0` apres
   redemarrage normal. Verifier ensuite le passage explicite en recovery.

La phase 02 ecrit le bootloader a `0x1000`, la table a `0x8000`, une selection
OTA vierge a `0xE000` et la recovery a `0x10000`. La selection vierge choisit
factory au prochain boot. Ni application `ota_0`, ni LittleFS ne sont ecrits.
Un changement de table peut rendre une ancienne configuration inaccessible :
ne pas utiliser cette phase sur un appareil deja configure.

## Mises a jour

Utiliser uniquement les phases 03/04 et demander explicitement le passage
recovery depuis la WebUI. Sauvegarder les cinq fichiers `/config/*.json`
avant LittleFS : la conservation automatique reste non garantie, voir `OTA.md`.

Les anciens scripts sont conserves. Les scripts recovery COM3/COM4 utilisent
maintenant la phase 02 complete avec leur port fixe. L'ancien script d'effacement
utilise la confirmation de la phase 01. Les scripts USB firmware/LittleFS restent
des outils de maintenance directe ; le parcours numerote utilise la recovery.

Si le port est occupe, fermer le moniteur serie et les logiciels qui l'utilisent.
Si `Connecting` bloque, maintenir BOOT pendant la connexion, puis relacher
quand le transfert commence.
