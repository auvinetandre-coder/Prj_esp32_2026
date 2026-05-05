@echo off
setlocal

cd /d "%~dp0\.."

set PORT=COM3
set DATA_DIR=%CD%\data
set IMAGE=%CD%\RouteurSolaireESP32_LittleFS.bin
set MKLITTLEFS=C:\Users\andre\AppData\Local\Arduino15\packages\esp32\tools\mklittlefs\4.0.2-db0513a\mklittlefs.exe
set ESPTOOL=C:\Users\andre\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe

rem Partition LittleFS reellement montee par le firmware actuel.
rem Verifiee via /api/fs : totalBytes = 131072 = 0x20000.
set FS_OFFSET=0x3D0000
set FS_SIZE=0x20000
set BLOCK_SIZE=4096
set PAGE_SIZE=256

echo ==================================================
echo Televersement LittleFS RouteurSolaireESP32
echo Port       : %PORT%
echo Dossier    : %DATA_DIR%
echo Offset FS  : %FS_OFFSET%
echo Taille FS  : %FS_SIZE%
echo ==================================================
echo.

if not exist "%DATA_DIR%" (
  echo ERREUR: dossier data introuvable.
  pause
  exit /b 1
)

if not exist "%MKLITTLEFS%" (
  echo ERREUR: mklittlefs introuvable:
  echo %MKLITTLEFS%
  pause
  exit /b 1
)

if not exist "%ESPTOOL%" (
  echo ERREUR: esptool introuvable:
  echo %ESPTOOL%
  pause
  exit /b 1
)

echo Creation image LittleFS...
"%MKLITTLEFS%" -c "%DATA_DIR%" -p %PAGE_SIZE% -b %BLOCK_SIZE% -s %FS_SIZE% "%IMAGE%"
if errorlevel 1 (
  echo ERREUR: creation image LittleFS impossible.
  pause
  exit /b 1
)

echo.
echo Televersement image LittleFS sur ESP32...
echo Ferme le moniteur serie Arduino IDE avant de continuer.
echo Si besoin, maintiens BOOT pendant le debut du televersement.
echo.
"%ESPTOOL%" --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z %FS_OFFSET% "%IMAGE%"
if errorlevel 1 (
  echo.
  echo ERREUR: televersement LittleFS echoue.
  echo Verifie que le port %PORT% est correct et que le moniteur serie est ferme.
  pause
  exit /b 1
)

echo.
echo LittleFS televerse avec succes.
echo Redemarrage automatique de l'ESP32 demande.
echo Attends quelques secondes puis ouvre:
echo http://192.168.4.1/app
echo.
pause
