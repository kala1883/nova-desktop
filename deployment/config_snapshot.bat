@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0config_snapshot.ps1" %*
exit /b %errorlevel%
