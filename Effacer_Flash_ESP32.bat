@echo off
rem Raccourci historique vers la phase explicite avec confirmation.
call "%~dp001_Effacer_ESP32_OPTIONNEL.bat" %*
exit /b %errorlevel%
