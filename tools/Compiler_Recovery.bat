@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."

call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  pause
  exit /b 1
)

set "PARTITION_NAME=partitions_recovery_main_4MB"
set "PARTITION_FILE=%CD%\%PARTITION_NAME%.csv"
set "MAX_FACTORY_SIZE=1114112"
set "BUILD_DIR=%CD%\build\recovery"
set "RECOVERY_SKETCH=%CD%\recovery\RouteurSolaireRecovery"
set "RECOVERY_BIN=%BUILD_DIR%\RouteurSolaireRecovery.ino.bin"
set "OTA_DIR=%CD%\build\ota"

if not exist "%PARTITION_FILE%" (
  echo Partition custom introuvable:
  echo %PARTITION_FILE%
  pause
  exit /b 1
)

if not exist "%RECOVERY_SKETCH%\RouteurSolaireRecovery.ino" (
  echo Sketch recovery introuvable:
  echo %RECOVERY_SKETCH%
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

if not exist "%OTA_DIR%" mkdir "%OTA_DIR%"

echo ==================================================
echo Compilation recovery factory RouteurSolaireESP32
echo Partition : %PARTITION_NAME%
echo Factory max: %MAX_FACTORY_SIZE% octets
echo ==================================================
echo.

"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_FACTORY_SIZE% "%RECOVERY_SKETCH%"
if errorlevel 1 (
  echo.
  echo ERREUR: compilation recovery echouee.
  pause
  exit /b 1
)

if exist "%RECOVERY_BIN%" (
  copy /Y "%RECOVERY_BIN%" "%OTA_DIR%\RouteurSolaireESP32_recovery_factory.bin" >nul
  echo Recovery factory copie ici:
  echo %OTA_DIR%\RouteurSolaireESP32_recovery_factory.bin
) else (
  echo ATTENTION: binaire recovery introuvable:
  echo %RECOVERY_BIN%
)

pause
