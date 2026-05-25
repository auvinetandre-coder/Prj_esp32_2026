@echo off
rem Detection commune des outils Arduino/ESP32 utilises par les scripts.
rem Les variables definies ici restent disponibles pour le script appelant.

if not defined ARDUINO_CLI (
  set "ARDUINO_CLI=%ProgramFiles%\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

if not exist "%ARDUINO_CLI%" (
  for /f "delims=" %%I in ('where arduino-cli.exe 2^>nul') do (
    if not defined ARDUINO_CLI set "ARDUINO_CLI=%%I"
  )
)

if not exist "%ARDUINO_CLI%" (
  echo ERREUR: arduino-cli introuvable.
  echo Chemin essaye: %ARDUINO_CLI%
  exit /b 1
)

for /f "delims=" %%I in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '%ARDUINO_CLI%' core list 2>$null | Select-String '^esp32:esp32' | ForEach-Object { ($_.Line -split '\s+')[1] }"') do set "ESP32_CORE_VERSION=%%I"

if not defined ESP32_CORE_VERSION (
  set "ESP32_CORE_VERSION=3.3.8"
)

if defined ESP32_CORE_VERSION (
  set "ESP32_CORE_DIR=%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\%ESP32_CORE_VERSION%"
)

if not defined ESP32_CORE_DIR (
  for /f "delims=" %%I in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\hardware\esp32'; if(Test-Path $root){ Get-ChildItem $root -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName }"') do set "ESP32_CORE_DIR=%%I"
)

if not defined ESP32_CORE_DIR (
  echo ERREUR: core ESP32 Arduino introuvable dans %%LOCALAPPDATA%%\Arduino15.
  exit /b 1
)

set "CORE_PARTITIONS=%ESP32_CORE_DIR%\tools\partitions"

for /f "delims=" %%I in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\mklittlefs'; if(Test-Path $root){ Get-ChildItem $root -Recurse -Filter mklittlefs.exe -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName }"') do set "MKLITTLEFS=%%I"

if not defined MKLITTLEFS (
  set "MKLITTLEFS=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\mklittlefs\4.0.2-db0513a\mklittlefs.exe"
)

if not exist "%MKLITTLEFS%" (
  if exist "%LOCALAPPDATA%\Arduino15\packages\esp32\tools\mklittlefs\3.0.0-gnu12-dc7f933\mklittlefs.exe" set "MKLITTLEFS=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\mklittlefs\3.0.0-gnu12-dc7f933\mklittlefs.exe"
)

for /f "delims=" %%I in ('powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$root=Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'; if(Test-Path $root){ Get-ChildItem $root -Recurse -Filter esptool.exe -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName }"') do set "ESPTOOL=%%I"

if not defined ESPTOOL (
  set "ESPTOOL=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\5.2.0\esptool.exe"
)

if not exist "%ESPTOOL%" (
  if exist "%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe" set "ESPTOOL=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
)

exit /b 0
