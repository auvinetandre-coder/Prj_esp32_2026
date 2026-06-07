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
set "MAX_FACTORY_SIZE=1114112"
set "BUILD_DIR=%CD%\build\recovery"
set "RECOVERY_SKETCH=%CD%\recovery\RouteurSolaireRecovery"
set "RECOVERY_BIN=%BUILD_DIR%\RouteurSolaireRecovery.ino.bin"
set "PARTITIONS_BIN=%BUILD_DIR%\RouteurSolaireRecovery.ino.partitions.bin"

if not exist "%PARTITION_FILE%" (
  echo Partition custom introuvable:
  echo %PARTITION_FILE%
  pause
  exit /b 1
)

copy /Y "%PARTITION_FILE%" "%CORE_PARTITIONS%\%PARTITION_NAME%.csv" >nul
if errorlevel 1 (
  echo ERREUR: copie partition core impossible.
  pause
  exit /b 1
)

"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_FACTORY_SIZE% "%RECOVERY_SKETCH%"
if errorlevel 1 (
  echo ERREUR: compilation recovery echouee.
  pause
  exit /b 1
)

if not exist "%ESPTOOL%" (
  echo ERREUR: esptool introuvable:
  echo %ESPTOOL%
  pause
  exit /b 1
)

echo Ecriture partition table et recovery factory sur %PORT%...
"%ESPTOOL%" --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z 0x8000 "%PARTITIONS_BIN%" 0x10000 "%RECOVERY_BIN%"
if errorlevel 1 (
  echo ERREUR: televersement recovery echoue.
  pause
  exit /b 1
)

echo Recovery factory televerse avec succes.
pause
