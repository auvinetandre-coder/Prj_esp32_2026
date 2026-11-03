@echo off
setlocal EnableExtensions
call "%~dp0Detect_Arduino_Tools.bat"
if errorlevel 1 exit /b 1
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Installation_ESP32.ps1" %*
exit /b %errorlevel%
