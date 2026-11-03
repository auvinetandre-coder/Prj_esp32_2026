@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

set "PORT=COM4"
call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  pause
  exit /b 1
)
set "PARTITION_NAME=partitions_recovery_main_4MB"
set "PARTITION_FILE=%CD%\%PARTITION_NAME%.csv"
set "MAX_APP_SIZE=1966080"
set "MAIN_APP_OFFSET=0x120000"
set "BUILD_DIR=%CD%\build\esp32.esp32.esp32"
set "OTA_DIR=%CD%\build\ota"
set "FIRMWARE_BIN=%BUILD_DIR%\RouteurSolaireESP32.ino.bin"
set "OTA_FIRMWARE_BIN=%OTA_DIR%\RouteurSolaireESP32_firmware.bin"

if not exist "%PARTITION_FILE%" (
  echo Partition custom introuvable:
  echo %PARTITION_FILE%
  pause
  exit /b 1
)

if exist "%CORE_PARTITIONS%" (
  copy /Y "%PARTITION_FILE%" "%CORE_PARTITIONS%\%PARTITION_NAME%.csv" >nul
) else (
  echo Dossier partitions ESP32 introuvable:
  echo %CORE_PARTITIONS%
  pause
  exit /b 1
)

if not exist "%OTA_DIR%" (
  mkdir "%OTA_DIR%"
)

for /f "delims=" %%I in ('%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\Generate_Build_Info.ps1" -ProjectDir "%CD%"') do set "BUILD_VERSION=%%I"
if not defined BUILD_VERSION (
  echo ERREUR: generation version firmware impossible.
  pause
  exit /b 1
)

echo ==================================================
echo Compilation et televersement firmware principal ota_0
echo Port      : %PORT%
echo Partition : %PARTITION_NAME%
echo App max   : %MAX_APP_SIZE% octets
echo Version   : %BUILD_VERSION%
echo ==================================================
echo Ferme le moniteur serie Arduino IDE avant de continuer.
echo.

"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_APP_SIZE% .
if errorlevel 1 (
  echo.
  echo ERREUR: compilation firmware echouee.
  pause
  exit /b 1
)

if not exist "%FIRMWARE_BIN%" (
  echo ERREUR: firmware compile introuvable:
  echo %FIRMWARE_BIN%
  pause
  exit /b 1
)

if not exist "%ESPTOOL%" (
  echo ERREUR: esptool introuvable:
  echo %ESPTOOL%
  pause
  exit /b 1
)

echo.
echo Ecriture firmware principal dans ota_0 uniquement...
echo Offset ota_0 : %MAIN_APP_OFFSET%
"%ESPTOOL%" --chip esp32 --port %PORT% --baud 115200 --before default_reset --after hard_reset write_flash -z %MAIN_APP_OFFSET% "%FIRMWARE_BIN%"
if errorlevel 1 (
  echo.
  echo ERREUR: televersement firmware ota_0 echoue.
  pause
  exit /b 1
)

echo.
echo Firmware principal ota_0 televerse avec succes.
if exist "%FIRMWARE_BIN%" (
  copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_BIN%" >nul
  copy /Y "%FIRMWARE_BIN%" "%OTA_DIR%\RouteurSolaireESP32_firmware_%BUILD_VERSION%.bin" >nul
  echo Firmware OTA copie dans build\ota.
)
echo Pense maintenant a televerser LittleFS avec le script COM4.
echo.
pause
