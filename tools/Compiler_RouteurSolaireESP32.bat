@echo off
setlocal
cd /d "%~dp0\.."
set ARDUINO_CLI=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe

if not exist "%ARDUINO_CLI%" (
  echo arduino-cli introuvable: %ARDUINO_CLI%
  pause
  exit /b 1
)

echo Compilation RouteurSolaireESP32...
"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=no_ota .
echo.
if errorlevel 1 (
  echo Compilation en erreur.
) else (
  echo Compilation OK.
)
pause
