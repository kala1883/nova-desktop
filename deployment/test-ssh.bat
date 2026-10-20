@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
call "%~dp0build-ssh.bat"
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\ssh.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c build\sqlite3.o -o build\ssh-test.exe -lshell32 -lshlwapi -lole32
if errorlevel 1 goto failed
build\ssh-test.exe
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\ssh_client.c src\platform\ssh_client.c src\core\server_profile.c build\ssh2.o -o build\ssh-client-test.exe -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt
if errorlevel 1 goto failed
echo Built loopback SSH/SFTP integration test. Run tests/ssh_fixture.py with Python and Paramiko.
popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
