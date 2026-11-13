@echo off
call "%~dp0tools\Televerser_Firmware.bat" COM4 115200
exit /b %errorlevel%
