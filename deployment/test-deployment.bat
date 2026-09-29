@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\deployment.ps1"
exit /b %errorlevel%
