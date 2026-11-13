# Installation ESP32 par phases

Lanceurs numerotes a la racine, pour ESP32 classique 4 Mo.
Fermer le moniteur serie. Installer sur banc, sorties de puissance deconnectees.
Chaque phase s'arrete en cas d'erreur : corriger avant de passer a la suivante.

| Phase | Lanceur | Utilisation |
|---|---|---|
| 00 | `00_Preparer_Arduino.bat` | Facultatif : ajoute le menu de partitions sur le PC. Ne flashe pas l'ESP. Redemarrer Arduino IDE ensuite. |
| 01 | `01_Effacer_ESP32_OPTIONNEL.bat` | Remise a zero volontaire. Efface TOUT, NVS et `/config` compris. Confirmation `EFFACER COMx`. |
| 02 | `02_Installer_Recovery.bat` | Compile puis installe bootloader, table et recovery. Demande le port et la confirmation `INSTALLER COMx`. Carte neuve/effacee uniquement. |
| 02b | `02b_Mettre_A_Jour_Recovery_Seule.bat` | Maintenance USB : installe uniquement une image Recovery choisie, apres controle de la table et sauvegarde de factory. Ne reinitialise pas la selection OTA. |
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
5. Sur le nouvel ESP, charger `build/ota/RouteurSolaireESP32_littlefs.bin`, puis
   `build/ota/RouteurSolaireESP32_firmware.bin`, separement. Demarrer le firmware principal.
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

Les raccourcis de maintenance COM3/COM4 a la racine sont conserves. Pour la Recovery complete, utiliser la phase 02 avec -Port COM3 ou -Port COM4. L'ancien lanceur d'effacement a la racine appelle la phase 01 avec sa confirmation. Les scripts USB firmware/LittleFS restent
des outils de maintenance directe ; le parcours numerote utilise la recovery.

Si le port est occupe, fermer le moniteur serie et les logiciels qui l'utilisent.
Si `Connecting` bloque, maintenir BOOT pendant la connexion, puis relacher
quand le transfert commence.

## Remplacer uniquement la Recovery sur un ESP deja installe

Utiliser `02b_Mettre_A_Jour_Recovery_Seule.bat`, puis indiquer le port et le chemin du binaire applicatif Recovery. Exemple :

```bat
02b_Mettre_A_Jour_Recovery_Seule.bat -Port COM4 -RecoveryBin "C:\ARDUINO\V2_PCB\RouteurSolaireESP32\build\ota\RouteurSolaireESP32_recovery_YYYYMMDD-NN.bin"
```

Le nom ci-dessus est un modele : choisir une image Recovery effectivement construite. Le lanceur ne compile pas et ne choisit pas silencieusement une ancienne image. Il refuse les images fusionnees, les images d'un autre ESP et celles sans identification Recovery du projet.

Apres confirmation `RECOVERY COMx`, le script lit la table a 0x8000 et exige le schema Recovery/Main courant (types, noms, offsets, tailles et absence de flags). Une table differente bloque l'ecriture ; ne pas utiliser l'installation complete pour contourner ce refus sur une carte configuree.

Il sauvegarde ensuite toute la partition factory dans `build/backups/recovery_<date>_<heure>_<port>/factory_avant.bin`, puis ecrit et verifie uniquement l'image Recovery a 0x10000. Une erreur de lecture, de sauvegarde ou de controle interrompt l'operation. Aucun erase_flash, aucune ecriture du bootloader, de la table, de NVS, d'otadata, du Main ou de LittleFS n'est demande par ce parcours.

La selection de boot n'est pas modifiee. Si le Main etait selectionne, il redemarre normalement ; demander ensuite explicitement le passage en recovery depuis la WebUI pour tester la nouvelle image. Le script ne garantit pas l'etat d'un firmware deja present qui s'execute apres le redemarrage. Une coupure pendant l'ecriture peut necessiter de retenter la maintenance USB.

Conserver `02_Installer_Recovery.bat` pour une carte neuve/effacee : cette installation complete ecrit bootloader, table, otadata et Recovery. Elle est distincte de 02b et n'effectue pas elle-meme d'effacement total ; celui-ci reste la phase 01 optionnelle.

## Lanceurs et code commun

Les fichiers COM3/COM4 de firmware et LittleFS restent uniquement a la racine comme raccourcis de compatibilite. Les doublons de tools ont ete retires. La logique de televersement USB est centralisee dans :

- `tools/Televerser_Firmware.bat COM4` ;
- `tools/Televerser_LittleFS.bat COM4` ;
- `tools/Installation_ESP32.ps1` pour effacement et installation Recovery complete ;
- `tools/Recovery_Seule.ps1` pour la maintenance de factory seule.

Les anciens raccourcis Recovery COM3/COM4 de tools sont remplaces par `02_Installer_Recovery.bat -Port COM3` ou `-Port COM4` pour l'installation complete. Les mises a jour utilisateur du Main et de LittleFS passent toujours par les phases 03/04 et la factory recovery.
