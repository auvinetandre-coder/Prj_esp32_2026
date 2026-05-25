@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

set "PORT=COM4"
call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  pause
  exit /b 1
)
set "DATA_DIR=%CD%\data"
set "LITTLEFS_VERSION_FILE=%DATA_DIR%\www\littlefs_version.txt"
set "BUILD_DIR=%CD%\build"
set "OTA_DIR=%BUILD_DIR%\ota"
set "IMAGE=%OTA_DIR%\RouteurSolaireESP32_littlefs.bin"
set "VERSION_FILE=%OTA_DIR%\latest_littlefs_version.txt"

rem Partition custom RouteurSolaireESP32:
rem app0   : offset 0x10000, taille 0x180000
rem app1   : offset 0x190000, taille 0x180000
rem spiffs : offset 0x310000, taille 0xF0000
set "FS_OFFSET=0x310000"
set "FS_SIZE=0xF0000"
set "BLOCK_SIZE=4096"
set "PAGE_SIZE=256"

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

if not exist "%BUILD_DIR%" (
  mkdir "%BUILD_DIR%"
)

if not exist "%OTA_DIR%" (
  mkdir "%OTA_DIR%"
)

for /f "delims=" %%I in ('%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$date=Get-Date -Format yyyyMMdd; $ota='%OTA_DIR%'; $max=0; if(Test-Path $ota){ Get-ChildItem $ota -Filter ('RouteurSolaireESP32_littlefs_'+$date+'-*.bin') | ForEach-Object { if($_.BaseName -match '-(\d{2})$'){ $n=[int]$Matches[1]; if($n -gt $max){$max=$n} } } }; '{0}-{1:00}' -f $date, ($max+1)"') do set "BUILD_VERSION=%%I"
echo %BUILD_VERSION%>"%VERSION_FILE%"
set "VERSIONED_IMAGE=%OTA_DIR%\RouteurSolaireESP32_littlefs_%BUILD_VERSION%.bin"

if not exist "%MKLITTLEFS%" (
  echo ERREUR: mklittlefs introuvable:
  echo %MKLITTLEFS%
  pause
  exit /b 1
)

echo %BUILD_VERSION%>"%LITTLEFS_VERSION_FILE%"

echo Creation image LittleFS...
"%MKLITTLEFS%" -c "%DATA_DIR%" -p %PAGE_SIZE% -b %BLOCK_SIZE% -s %FS_SIZE% "%IMAGE%"
if errorlevel 1 (
  echo ERREUR: creation image LittleFS impossible.
  pause
  exit /b 1
)
copy /Y "%IMAGE%" "%VERSIONED_IMAGE%" >nul

echo.
echo Image LittleFS generee avec succes:
echo %IMAGE%
echo Image LittleFS horodatee:
echo %VERSIONED_IMAGE%
echo.
echo Tu peux utiliser ce fichier pour l'OTA LittleFS depuis la page Secours.
echo.
choice /C ON /N /M "Veux-tu aussi televerser maintenant sur %PORT% ? [O/N] "
if errorlevel 2 (
  echo.
  echo Televersement annule. Le fichier .bin est disponible pour l'OTA.
  echo.
  pause
  exit /b 0
)

if not exist "%ESPTOOL%" (
  echo ERREUR: esptool introuvable:
  echo %ESPTOOL%
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
