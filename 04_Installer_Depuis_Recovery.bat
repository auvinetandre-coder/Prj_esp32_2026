@echo off
if not exist "%~dp0build\release\firmware.bin" goto :missing
if not exist "%~dp0build\release\littlefs.bin" goto :missing
echo 1. Ouvrir la recovery de l'ESP cible, via son IP locale ou son AP RSrecoverymode_XXXX.
echo    Via l'AP : http://192.168.4.1/recovery
echo    Sur un ESP deja configure : demander explicitement le passage recovery depuis la WebUI.
echo 2. Sauvegarder /config avant toute mise a jour d'un ESP deja configure.
echo    La conservation automatique de /config pendant l'upload LittleFS reste a verifier.
echo 3. Sur le nouvel ESP : charger littlefs.bin, puis firmware.bin, separement.
echo 4. Demarrer le firmware principal et verifier /app et les versions dans Systeme.
echo    Verifier qu'un redemarrage normal reste sur ota_0.
echo Fichiers generes par la phase 03 :
echo %~dp0build\release\littlefs.bin
echo %~dp0build\release\firmware.bin
echo Cette phase n'effectue aucun flash automatique.
pause
exit /b 0
:missing
echo ERREUR: fichiers absents. Executer la phase 03 et verifier sa reussite.
pause
exit /b 1
