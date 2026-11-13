@echo off
call "%~dp0tools\Televerser_Firmware.bat" COM3 921600
exit /b %errorlevel%
