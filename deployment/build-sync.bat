@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
call "%~dp0openssl.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
gcc -std=c11 -O2 -Wall -Wextra -Werror -nostdlib -r src\platform\app_log.c src\core\sync_model.c src\platform\sync_settings.c src\platform\sync_json.c src\platform\sync_wire.c src\platform\sync_files.c src\platform\sync_client.c src\platform\sync_server.c -o build\nova-sync.o
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
