@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0resolve_config.ps1" %*
exit /b %errorlevel%
