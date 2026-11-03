@echo off
call "%~dp0Installation_ESP32.bat" -Phase Recovery -Port COM3
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
