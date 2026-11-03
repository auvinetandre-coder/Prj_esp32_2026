@echo off
call "%~dp0Installation_ESP32.bat" -Phase Effacer %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
