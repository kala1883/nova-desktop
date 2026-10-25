@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
call "%~dp0openssl.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
call "%~dp0build-ssh.bat"
if errorlevel 1 goto failed
call "%~dp0build-terminal.bat"
if errorlevel 1 goto failed
call "%~dp0build-sync.bat"
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror tests\modules.c src\core\workspace.c src\core\launch_queue.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c -o build\modules-test.exe
if errorlevel 1 goto failed
build\modules-test.exe
if errorlevel 1 goto failed
windres src\nova.rc -O coff -o build\nova-resource.o
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\core.c src\core\workspace.c src\core\launch_queue.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\shell_icons.c src\platform\batch_runner.c src\ui\hold_drag.c src\ui\file_manager.c src\ui\ssh_view.c src\ui\terminal_view.c src\platform\ssh_client.c src\platform\ssh_credentials.c src\ui\batch_tasks.c src\ui\sync_manager.c build\nova-sync.o src\platform\storage.c src\platform\config_json.c build\sqlite3.o build\ssh2.o build\vterm.o build\nova-resource.o -o build\core-test.exe -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -luuid -lshlwapi -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt -luxtheme -lcomdlg32 -ladvapi32
if errorlevel 1 goto failed
build\core-test.exe %*
if errorlevel 1 goto failed

:success
popd
exit /b 0

:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
