@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\file_manager.c src\core\batch_task.c src\core\file_command.c src\platform\storage.c build\sqlite3.o -o build\files-test.exe -lcomctl32 -lshell32 -lole32 -luuid -lshlwapi -ldwmapi -lgdi32 -luser32
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
