@echo off
rem Facultatif pour les phases numerotees : ajoute le menu de partitions dans Arduino IDE.
call "%~dp0tools\Installer_Partition_Arduino.bat"
exit /b %errorlevel%
