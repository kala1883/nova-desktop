@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
call "%~dp0build-ssh.bat"
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\file_manager.c src\ui\ssh_view.c src\platform\ssh_client.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c build\sqlite3.o build\ssh2.o -o build\files-test.exe -lcomctl32 -lshell32 -lole32 -luuid -lshlwapi -ldwmapi -lgdi32 -luser32 -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt -luxtheme -lcomdlg32 -ladvapi32
if errorlevel 1 goto failed
build\files-test.exe
if errorlevel 1 goto failed

:success
popd
exit /b 0

:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
