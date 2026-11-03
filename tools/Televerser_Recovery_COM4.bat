@echo off
call "%~dp0Installation_ESP32.bat" -Phase Recovery -Port COM4
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
