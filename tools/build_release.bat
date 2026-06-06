@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

call "%CD%\tools\Detect_Arduino_Tools.bat"
if errorlevel 1 (
  exit /b 1
)

set "PROJECT=RouteurSolaireESP32"
set "GITHUB_OWNER=auvinetandre-coder"
set "GITHUB_REPO=Prj_esp32_2026"
set "PARTITION_NAME=partitions_ota_1m5app_960k_littlefs"
set "PARTITION_FILE=%CD%\%PARTITION_NAME%.csv"
set "MAX_APP_SIZE=1572864"
set "FS_SIZE=0xF0000"
set "BLOCK_SIZE=4096"
set "PAGE_SIZE=256"
set "DATA_DIR=%CD%\data"
set "LITTLEFS_VERSION_FILE=%DATA_DIR%\www\littlefs_version.txt"
set "BUILD_DIR=%CD%\build"
set "ARDUINO_BUILD_DIR=%BUILD_DIR%\esp32.esp32.esp32"
set "OTA_DIR=%BUILD_DIR%\ota"
set "RELEASE_DIR=%BUILD_DIR%\release"
set "FIRMWARE_BIN=%ARDUINO_BUILD_DIR%\RouteurSolaireESP32.ino.bin"
set "OTA_FIRMWARE_BIN=%OTA_DIR%\RouteurSolaireESP32_firmware.bin"
set "OTA_LITTLEFS_BIN=%OTA_DIR%\RouteurSolaireESP32_littlefs.bin"
set "VERSION_FILE=%RELEASE_DIR%\version.json"

if not exist "%PARTITION_FILE%" (
  echo ERREUR: partition custom introuvable:
  echo %PARTITION_FILE%
  exit /b 1
)

if not exist "%DATA_DIR%" (
  echo ERREUR: dossier data introuvable:
  echo %DATA_DIR%
  exit /b 1
)

if not exist "%CORE_PARTITIONS%" (
  echo ERREUR: dossier partitions ESP32 introuvable:
  echo %CORE_PARTITIONS%
  exit /b 1
)

if not exist "%MKLITTLEFS%" (
  echo ERREUR: mklittlefs introuvable:
  echo %MKLITTLEFS%
  exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%OTA_DIR%" mkdir "%OTA_DIR%"
if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"

copy /Y "%PARTITION_FILE%" "%CORE_PARTITIONS%\%PARTITION_NAME%.csv" >nul
if errorlevel 1 (
  echo ERREUR: copie de la partition custom impossible.
  exit /b 1
)

for /f "delims=" %%I in ('%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\Generate_Build_Info.ps1" -ProjectDir "%CD%"') do set "BUILD_VERSION=%%I"
if not defined BUILD_VERSION (
  echo ERREUR: generation version firmware impossible.
  exit /b 1
)

set "OTA_FIRMWARE_VERSIONED_BIN=%OTA_DIR%\RouteurSolaireESP32_firmware_%BUILD_VERSION%.bin"
set "OTA_LITTLEFS_VERSIONED_BIN=%OTA_DIR%\RouteurSolaireESP32_littlefs_%BUILD_VERSION%.bin"
set "FIRMWARE_URL=https://github.com/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest/download/firmware.bin"
set "LITTLEFS_URL=https://github.com/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest/download/littlefs.bin"
set "VERSION_URL=https://github.com/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest/download/version.json"
set "RELEASE_URL=https://github.com/%GITHUB_OWNER%/%GITHUB_REPO%/releases/latest"

echo %BUILD_VERSION%>"%LITTLEFS_VERSION_FILE%"
echo %BUILD_VERSION%>"%OTA_DIR%\latest_littlefs_version.txt"

echo ==================================================
echo Build release GitHub OTA %PROJECT%
echo Version    : %BUILD_VERSION%
echo Firmware   : firmware.bin
echo LittleFS   : littlefs.bin
echo Manifest   : version.json
echo Destination: %RELEASE_DIR%
echo ==================================================
echo.

echo Compilation firmware...
"%ARDUINO_CLI%" compile --fqbn esp32:esp32:esp32:PartitionScheme=custom --build-path "%ARDUINO_BUILD_DIR%" --build-property build.partitions=%PARTITION_NAME% --build-property upload.maximum_size=%MAX_APP_SIZE% .
if errorlevel 1 (
  echo ERREUR: compilation firmware impossible.
  exit /b 1
)

if not exist "%FIRMWARE_BIN%" (
  echo ERREUR: firmware compile introuvable:
  echo %FIRMWARE_BIN%
  exit /b 1
)

copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_BIN%" >nul
copy /Y "%FIRMWARE_BIN%" "%OTA_FIRMWARE_VERSIONED_BIN%" >nul
copy /Y "%FIRMWARE_BIN%" "%RELEASE_DIR%\firmware.bin" >nul
if errorlevel 1 (
  echo ERREUR: copie firmware release impossible.
  exit /b 1
)

echo.
echo Creation image LittleFS...
"%MKLITTLEFS%" -c "%DATA_DIR%" -p %PAGE_SIZE% -b %BLOCK_SIZE% -s %FS_SIZE% "%OTA_LITTLEFS_BIN%"
if errorlevel 1 (
  echo ERREUR: creation image LittleFS impossible.
  exit /b 1
)

copy /Y "%OTA_LITTLEFS_BIN%" "%OTA_LITTLEFS_VERSIONED_BIN%" >nul
copy /Y "%OTA_LITTLEFS_BIN%" "%RELEASE_DIR%\littlefs.bin" >nul
if errorlevel 1 (
  echo ERREUR: copie LittleFS release impossible.
  exit /b 1
)

(
  echo {
  echo   "project": "%PROJECT%",
  echo   "version": "%BUILD_VERSION%",
  echo   "firmwareVersion": "%BUILD_VERSION%",
  echo   "littlefsVersion": "%BUILD_VERSION%",
  echo   "firmwareUrl": "%FIRMWARE_URL%",
  echo   "littlefsUrl": "%LITTLEFS_URL%",
  echo   "versionUrl": "%VERSION_URL%",
  echo   "releaseUrl": "%RELEASE_URL%",
  echo   "mandatory": false,
  echo   "notes": "Release OTA RouteurSolaireESP32"
  echo }
) > "%VERSION_FILE%"
if errorlevel 1 (
  echo ERREUR: creation version.json impossible.
  exit /b 1
)

echo.
echo Release locale prete:
echo %RELEASE_DIR%\firmware.bin
echo %RELEASE_DIR%\littlefs.bin
echo %RELEASE_DIR%\version.json
echo.
echo Aucune publication GitHub effectuee en P1.
exit /b 0
