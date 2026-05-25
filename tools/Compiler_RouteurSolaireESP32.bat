@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  pause
  exit /b 1
)
set "PARTITION_NAME=partitions_ota_1m5app_960k_littlefs"
set "PARTITION_FILE=%CD%\%PARTITION_NAME%.csv"
set "MAX_APP_SIZE=1572864"
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
set "OTA_FIRMWARE_VERSIONED_BIN=%OTA_DIR%\RouteurSolaireESP32_firmware_%BUILD_VERSION%.bin"

echo Compilation RouteurSolaireESP32...
"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_APP_SIZE% .
echo.
if errorlevel 1 (
  echo Compilation en erreur.
) else (
  echo Compilation OK.
  if exist "%FIRMWARE_BIN%" (
    copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_BIN%" >nul
    copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_VERSIONED_BIN%" >nul
    echo Firmware OTA copie ici:
    echo %OTA_FIRMWARE_BIN%
    echo Firmware OTA horodate:
    echo %OTA_FIRMWARE_VERSIONED_BIN%
  ) else (
    echo ATTENTION: firmware .bin introuvable:
    echo %FIRMWARE_BIN%
  )
)
pause
