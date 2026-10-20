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
call "%~dp0build-terminal.bat"
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\ssh.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c build\sqlite3.o -o build\ssh-test.exe -lshell32 -lshlwapi -lole32
if errorlevel 1 goto failed
build\ssh-test.exe
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\ssh_credentials.c -o build\ssh-credentials-test.exe -ladvapi32
if errorlevel 1 goto failed
build\ssh-credentials-test.exe
if errorlevel 1 goto failed

gcc -std=c11 -O2 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0601 -municode tests\ssh_client.c src\platform\ssh_client.c src\core\server_profile.c build\ssh2.o -o build\ssh-client-test.exe -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\terminal.c build\vterm.o -o build\terminal-test.exe -lgdi32 -luser32
if errorlevel 1 goto failed
build\terminal-test.exe
if errorlevel 1 goto failed

gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\ssh_terminal.c src\platform\ssh_client.c src\platform\ssh_credentials.c src\core\workspace.c src\core\batch_task.c src\core\file_command.c src\core\server_profile.c src\platform\storage.c src\platform\config_json.c build\sqlite3.o build\ssh2.o build\vterm.o -o build\ssh-terminal-test.exe -lcomctl32 -lshell32 -lole32 -luuid -lshlwapi -ldwmapi -lgdi32 -luser32 -l:libcrypto.a -lws2_32 -lcrypt32 -lbcrypt -luxtheme -lcomdlg32 -ladvapi32
if errorlevel 1 goto failed

echo Built loopback SSH/SFTP integration test. Run tests/ssh_fixture.py with Python and Paramiko.
popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
