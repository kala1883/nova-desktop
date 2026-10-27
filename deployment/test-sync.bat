@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
call "%~dp0openssl.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
call "%~dp0build-ssh.bat"
if errorlevel 1 goto failed
call "%~dp0build-sync.bat"
if errorlevel 1 goto failed
windres src\nova.rc -O coff -o build\sync-resource.o
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\sync.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c src\platform\ssh_client.c build\nova-sync.o build\sqlite3.o build\ssh2.o -o build\sync-test.exe -lshell32 -lshlwapi -lole32 -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt
if errorlevel 1 goto failed
build\sync-test.exe
if errorlevel 1 goto failed

gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\diagnostics.c src\platform\batch_runner.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c src\platform\ssh_client.c build\nova-sync.o build\sqlite3.o build\ssh2.o -o build\diagnostics-test.exe -lshell32 -lshlwapi -lole32 -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt
if errorlevel 1 goto failed
build\diagnostics-test.exe
if errorlevel 1 goto failed

gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\sync_ui.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c src\platform\ssh_client.c src\platform\ssh_credentials.c build\nova-sync.o build\sqlite3.o build\ssh2.o -o build\sync-ui-test.exe -ldwmapi -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -luxtheme -lshell32 -lole32 -luuid -lshlwapi -ladvapi32 -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt
if errorlevel 1 goto failed
build\sync-ui-test.exe --capture
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\sync_client.c src\platform\ssh_client.c src\core\server_profile.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\platform\storage.c src\platform\config_json.c build\nova-sync.o build\sqlite3.o build\ssh2.o -o build\sync-client-test.exe -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt -lshell32 -lshlwapi -lole32
if errorlevel 1 goto failed
echo Native sync protocol fixture: python tests/sync_fixture.py build/sync-client-test.exe
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\sync_server.c src\platform\ssh_client.c src\core\server_profile.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\platform\storage.c src\platform\config_json.c build\nova-sync.o build\sqlite3.o build\ssh2.o build\sync-resource.o -o build\sync-server-test.exe -l:libz.a -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt -lshell32 -lshlwapi -lole32
if errorlevel 1 goto failed
echo Native server administration fixture: python tests/sync_server_fixture.py build/sync-server-test.exe
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tests\sync_migration.ps1
if errorlevel 1 goto failed

popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
