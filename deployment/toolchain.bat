@echo off
setlocal
set "nova_toolchain_bin="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0toolchain.ps1"`) do set "nova_toolchain_bin=%%I"
if not defined nova_toolchain_bin exit /b 1
endlocal & set "PATH=%nova_toolchain_bin%;%PATH%"
exit /b 0
