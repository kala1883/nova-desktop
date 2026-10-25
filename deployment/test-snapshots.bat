@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
call "%~dp0build.bat" build\nova-data-validation.exe
if errorlevel 1 goto failed
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tests\config_snapshot.ps1 -Executable "%CD%\build\nova-data-validation.exe"
if errorlevel 1 goto failed
popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
