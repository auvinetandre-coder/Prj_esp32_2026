@echo off
call "%~dp0tools\build_release.bat"
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
