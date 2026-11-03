@echo off
call "%~dp0tools\Installation_ESP32.bat" -Phase Recovery %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
