@echo off
setlocal
call build-sqlite.bat
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\storage.c src\core\workspace.c src\core\launch_queue.c src\platform\shell_icons.c src\ui\hold_drag.c src\ui\file_manager.c src\platform\storage.c build\sqlite3.o -o build\storage-test.exe -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -luuid -lshlwapi
if errorlevel 1 exit /b %errorlevel%
build\storage-test.exe
exit /b %errorlevel%
