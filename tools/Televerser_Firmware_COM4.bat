@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

set "PORT=COM4"
set "ARDUINO_CLI=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
set "PARTITION_NAME=partitions_ota_1m5app_960k_littlefs"
set "PARTITION_FILE=%CD%\%PARTITION_NAME%.csv"
set "CORE_PARTITIONS=%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\3.3.8\tools\partitions"
set "MAX_APP_SIZE=1572864"
set "BUILD_DIR=%CD%\build\esp32.esp32.esp32"
set "OTA_DIR=%CD%\build\ota"
set "FIRMWARE_BIN=%BUILD_DIR%\RouteurSolaireESP32.ino.bin"
set "OTA_FIRMWARE_BIN=%OTA_DIR%\RouteurSolaireESP32_firmware.bin"

if not exist "%ARDUINO_CLI%" (
  echo arduino-cli introuvable: %ARDUINO_CLI%
  pause
  exit /b 1
)

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
echo Compilation et televersement firmware OTA custom
echo Port      : %PORT%
echo Partition : %PARTITION_NAME%
echo App max   : %MAX_APP_SIZE% octets
echo Version   : %BUILD_VERSION%
echo ==================================================
echo Ferme le moniteur serie Arduino IDE avant de continuer.
echo.

"%ARDUINO_CLI%" compile --upload -p %PORT% --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_APP_SIZE% .
if errorlevel 1 (
  echo.
  echo ERREUR: compilation ou televersement firmware echoue.
  pause
  exit /b 1
)

echo.
echo Firmware televerse avec succes.
if exist "%FIRMWARE_BIN%" (
  copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_BIN%" >nul
  copy /Y "%FIRMWARE_BIN%" "%OTA_DIR%\RouteurSolaireESP32_firmware_%BUILD_VERSION%.bin" >nul
  echo Firmware OTA copie dans build\ota.
)
echo Pense maintenant a televerser LittleFS avec le script COM4.
echo.
pause
