@echo off
call "%~dp0tools\Installation_ESP32.bat" -Phase Effacer %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
