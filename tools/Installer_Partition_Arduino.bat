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
set "BOARDS_LOCAL=%ESP32_CORE_DIR%\boards.local.txt"
set "CORE_PARTITION_FILE=%CORE_PARTITIONS%\%PARTITION_NAME%.csv"

if not exist "%PARTITION_FILE%" (
  echo ERREUR: partition introuvable:
  echo %PARTITION_FILE%
  pause
  exit /b 1
)

if not exist "%CORE_PARTITIONS%" (
  echo ERREUR: dossier partitions ESP32 introuvable:
  echo %CORE_PARTITIONS%
  pause
  exit /b 1
)

copy /Y "%PARTITION_FILE%" "%CORE_PARTITION_FILE%" >nul
if errorlevel 1 (
  echo ERREUR: copie du CSV impossible.
  pause
  exit /b 1
)

%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$boards='%BOARDS_LOCAL%'; $content=if(Test-Path $boards){Get-Content $boards -Raw}else{''}; $content=[regex]::Replace($content,'(?ms)^# Partition custom RouteurSolaireESP32 recovery/main\r?\n.*?(?=\r?\n# |\z)',''); $lines=@('# Partition custom RouteurSolaireESP32 recovery/main','esp32.menu.PartitionScheme.routeursolairerecovery=RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)','esp32.menu.PartitionScheme.routeursolairerecovery.build.partitions=partitions_recovery_main_4MB','esp32.menu.PartitionScheme.routeursolairerecovery.upload.maximum_size=1966080','esp32doit-devkit-v1.menu.PartitionScheme.routeursolairerecovery=RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)','esp32doit-devkit-v1.menu.PartitionScheme.routeursolairerecovery.build.partitions=partitions_recovery_main_4MB','esp32doit-devkit-v1.menu.PartitionScheme.routeursolairerecovery.upload.maximum_size=1966080','nodemcu-32s.menu.PartitionScheme.routeursolairerecovery=RouteurSolaire Recovery/Main (factory 1.06MB / main 1.88MB / LittleFS 1MB)','nodemcu-32s.menu.PartitionScheme.routeursolairerecovery.build.partitions=partitions_recovery_main_4MB','nodemcu-32s.menu.PartitionScheme.routeursolairerecovery.upload.maximum_size=1966080') -join [Environment]::NewLine; $content=$content.TrimStart(); Set-Content -LiteralPath $boards -Value ($lines + [Environment]::NewLine + [Environment]::NewLine + $content) -Encoding ASCII"
if errorlevel 1 (
  echo ERREUR: mise a jour boards.local.txt impossible.
  pause
  exit /b 1
)

echo Partition Arduino installee:
echo %CORE_PARTITION_FILE%
echo Menu ajoute/verifie dans:
echo %BOARDS_LOCAL%
echo.
echo Redemarre Arduino IDE si le menu etait deja ouvert.
pause
