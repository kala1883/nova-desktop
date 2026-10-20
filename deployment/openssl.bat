@echo off
setlocal
set "nova_resolved_openssl="
for /f "usebackq delims=" %%I in (`powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0openssl.ps1"`) do set "nova_resolved_openssl=%%I"
if not defined nova_resolved_openssl exit /b 1
set "nova_include=%nova_resolved_openssl%\include"
set "nova_library=%nova_resolved_openssl%\lib"
if defined CPATH set "nova_include=%nova_include%;%CPATH%"
if defined LIBRARY_PATH set "nova_library=%nova_library%;%LIBRARY_PATH%"
endlocal & set "CPATH=%nova_include%" & set "LIBRARY_PATH=%nova_library%" & set "NOVA_OPENSSL_SELECTED_ROOT=%nova_resolved_openssl%"
exit /b 0
