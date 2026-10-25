@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\toolchain.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\openssl.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\publish_latest.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\config_merge.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\config_sync.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\tests\deployment.ps1"
exit /b %errorlevel%
