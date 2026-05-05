@echo off
setlocal
set ARDUINO_CLI=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
set ESPTOOL=C:\Users\andre\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe

if not exist "%ARDUINO_CLI%" (
  echo arduino-cli introuvable: %ARDUINO_CLI%
  pause
  exit /b 1
)

if not exist "%ESPTOOL%" (
  set ESPTOOL=C:\Users\andre\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe
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
