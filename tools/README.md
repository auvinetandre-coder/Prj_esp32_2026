# Outils du projet

Le dossier tools contient les moteurs de compilation, de livraison et de maintenance. Les lanceurs utilisateur numerotes restent a la racine du projet.

## Installation et maintenance

| Besoin | Lanceur a la racine | Moteur |
|---|---|---|
| Preparer Arduino IDE | `00_Preparer_Arduino.bat` | [Installer_Partition_Arduino.bat](Installer_Partition_Arduino.bat) |
| Effacement total volontaire | `01_Effacer_ESP32_OPTIONNEL.bat` | [Installation_ESP32.ps1](Installation_ESP32.ps1), phase Effacer |
| Installer sur carte neuve/effacee | `02_Installer_Recovery.bat` | Installation_ESP32.ps1, phase Recovery |
| Remplacer uniquement la Recovery | `02b_Mettre_A_Jour_Recovery_Seule.bat` | [Recovery_Seule.ps1](Recovery_Seule.ps1), via Installation_ESP32 |
| Generer Main et LittleFS | `03_Generer_Firmware_et_LittleFS.bat` | [build_release.bat](build_release.bat) |
| Installer Main et LittleFS depuis factory | `04_Installer_Depuis_Recovery.bat` | Instructions pour upload manuel |

La maintenance Recovery seule exige un fichier applicatif Recovery choisi explicitement, controle les partitions et sauvegarde factory avant remplacement. L'installation complete reste reservee a une carte neuve/effacee. Voir [la procedure](../doc/INSTALLATION_PAR_PHASES.md).

## Compilation et autres outils conserves

- [Compiler_RouteurSolaireESP32.bat](Compiler_RouteurSolaireESP32.bat) : compilation Main seule.
- [Compiler_Recovery.bat](Compiler_Recovery.bat) : compilation Recovery seule.
- [Detect_Arduino_Tools.bat](Detect_Arduino_Tools.bat) et [Generate_Build_Info.ps1](Generate_Build_Info.ps1) : dependances partagees.
- [Installation_ESP32.bat](Installation_ESP32.bat) : detection des outils et appel du moteur PowerShell.
- [publish_release.bat](publish_release.bat) : publication manuelle, a ne pas lancer pour un simple nettoyage ou test.
- [analyse_jsy_capture.js](analyse_jsy_capture.js) : analyse hors ligne des captures.
- [preview_automate_server.js](preview_automate_server.js) : apercu local de l'editeur, sans commande d'ESP.

Les images livrees restent dans `build/ota`. Ce rangement ne modifie pas la logique de compilation, de versionnement ou de publication des moteurs conserves.

## USB direct historique

[Televerser_Firmware.bat](Televerser_Firmware.bat) et [Televerser_LittleFS.bat](Televerser_LittleFS.bat) sont les moteurs communs parametres par port. Les raccourcis COM3/COM4 restent uniquement a la racine. Les sept raccourcis redondants de tools (quatre Main/LittleFS, deux Recovery et un effacement) ont ete retires le 11 novembre 2026 ; leurs usages ont ete rediriges vers les lanceurs conserves.

Les mises a jour utilisateur Main/LittleFS passent par la factory recovery. Les outils USB directs restent des outils de maintenance ; LittleFS remplace toute sa partition et exige une sauvegarde independante de `/config`.

## Tests hors materiel

Les scripts JavaScript, le test PowerShell Recovery et le support PID sont regroupes dans [tests/](tests/). Aucun test n'a ete supprime. Depuis la racine :

```powershell
Get-ChildItem tools/tests/test_*.js | ForEach-Object {
  node $_.FullName
  if ($LASTEXITCODE -ne 0) { Write-Warning "Echec : $($_.Name)" }
}
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/tests/test_recovery_seule.ps1
```

Les audits historiques citant `tools/test_*.js` decrivent l'ancien emplacement. Les commandes courantes sont sous `tools/tests/`.

Avant rangement, le 11 novembre 2026 : 27 tests JavaScript reussis sur 28 ; le test generique echouait sur une assertion trop large. Le lot Systeme/Diagnostic du 11 novembre corrige cette assertion pour autoriser les icones de charges, ajoute un test des pages et remplace le test de transition de simulation par un test de retrait. Les tests logiciels ne valident pas les sorties electriques, la radio ou les mises a jour reelles sur ESP32.
