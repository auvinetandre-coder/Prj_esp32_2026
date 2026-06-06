@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

set "PROJECT=RouteurSolaireESP32"
set "GITHUB_OWNER=auvinetandre-coder"
set "GITHUB_REPO=Prj_esp32_2026"
set "REPO=%GITHUB_OWNER%/%GITHUB_REPO%"
set "RELEASE_DIR=%CD%\build\release"
set "FIRMWARE_BIN=%RELEASE_DIR%\firmware.bin"
set "LITTLEFS_BIN=%RELEASE_DIR%\littlefs.bin"
set "VERSION_JSON=%RELEASE_DIR%\version.json"

for /f "delims=" %%I in ('where gh 2^>nul') do (
  if not defined GH set "GH=%%I"
)

if not defined GH (
  echo ERREUR: GitHub CLI introuvable.
  echo Installe gh puis lance: gh auth login
  exit /b 1
)

if not exist "%FIRMWARE_BIN%" (
  echo ERREUR: firmware.bin introuvable.
  echo Lance d'abord tools\build_release.bat
  exit /b 1
)

if not exist "%LITTLEFS_BIN%" (
  echo ERREUR: littlefs.bin introuvable.
  echo Lance d'abord tools\build_release.bat
  exit /b 1
)

if not exist "%VERSION_JSON%" (
  echo ERREUR: version.json introuvable.
  echo Lance d'abord tools\build_release.bat
  exit /b 1
)

for /f "delims=" %%I in ('%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$json=Get-Content '%VERSION_JSON%' -Raw | ConvertFrom-Json; $json.version"') do set "RELEASE_VERSION=%%I"
if not defined RELEASE_VERSION (
  echo ERREUR: version absente ou invalide dans:
  echo %VERSION_JSON%
  exit /b 1
)

set "RELEASE_TAG=v%RELEASE_VERSION%"
set "RELEASE_TITLE=%PROJECT% %RELEASE_VERSION%"

"%GH%" auth status >nul 2>nul
if errorlevel 1 (
  echo ERREUR: GitHub CLI non authentifie.
  echo Lance d'abord: gh auth login
  exit /b 1
)

echo ==================================================
echo Publication GitHub Release %PROJECT%
echo Depot      : %REPO%
echo Tag        : %RELEASE_TAG%
echo Titre      : %RELEASE_TITLE%
echo Assets     :
echo   %FIRMWARE_BIN%
echo   %LITTLEFS_BIN%
echo   %VERSION_JSON%
echo ==================================================
echo.
echo Aucun fichier .bin ne sera committe. Les fichiers seront envoyes en assets de Release.
echo.
choice /C ON /N /M "Publier cette nouvelle Release GitHub ? [O/N] "
if errorlevel 2 (
  echo Publication annulee.
  exit /b 0
)

"%GH%" release view "%RELEASE_TAG%" --repo "%REPO%" >nul 2>nul
if not errorlevel 1 (
  echo ERREUR: la Release ou le tag existe deja:
  echo %RELEASE_TAG%
  echo Choisis une nouvelle version avec tools\build_release.bat avant de publier.
  exit /b 1
)

"%GH%" release create "%RELEASE_TAG%" "%FIRMWARE_BIN%" "%LITTLEFS_BIN%" "%VERSION_JSON%" --repo "%REPO%" --title "%RELEASE_TITLE%" --notes "Release OTA %PROJECT% %RELEASE_VERSION%"
if errorlevel 1 (
  echo ERREUR: publication GitHub Release echouee.
  exit /b 1
)

echo.
echo Release publiee:
echo https://github.com/%REPO%/releases/tag/%RELEASE_TAG%
exit /b 0
