@echo off
call "%~dp0tools\Televerser_LittleFS.bat" COM3 921600
exit /b %errorlevel%
