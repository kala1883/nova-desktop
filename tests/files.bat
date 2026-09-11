@echo off
setlocal
if not exist build mkdir build
call build-sqlite.bat
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\file_manager.c src\core\batch_task.c src\core\file_command.c src\platform\storage.c build\sqlite3.o -o build\files-test.exe -lcomctl32 -lshell32 -lole32 -luuid -lshlwapi -ldwmapi -lgdi32 -luser32
if errorlevel 1 exit /b %errorlevel%
build\files-test.exe
exit /b %errorlevel%
