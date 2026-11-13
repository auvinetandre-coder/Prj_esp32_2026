@echo off
call "%~dp0tools\Televerser_LittleFS.bat" COM4 115200
exit /b %errorlevel%
