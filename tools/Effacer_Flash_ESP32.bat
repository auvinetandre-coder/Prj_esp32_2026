@echo off
setlocal EnableExtensions
cd /d "%~dp0\.."
call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  pause
  exit /b 1
)

if not exist "%ESPTOOL%" (
  echo esptool introuvable.
  pause
  exit /b 1
)

echo Ports detectes:
"%ARDUINO_CLI%" board list
echo.
set /p PORT=Entrer le port ESP32, exemple COM3: 

if "%PORT%"=="" (
  echo Port vide.
  pause
  exit /b 1
)

echo.
echo Effacement complet de la flash sur %PORT%...
"%ESPTOOL%" --chip esp32 --port %PORT% --baud 921600 erase-flash

echo.
if errorlevel 1 (
  echo Effacement en erreur. Essayer en maintenant BOOT sur l'ESP32 pendant le debut de la commande.
) else (
  echo Flash effacee. Televerser maintenant le sketch depuis Arduino IDE ou avec Compiler_RouteurSolaireESP32.bat.
)
echo.
echo Alternative Arduino IDE:
echo Outils ^> Erase All Flash Before Sketch Upload ^> Enabled
echo puis Televerser le sketch.
pause
