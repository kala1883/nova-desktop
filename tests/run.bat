@echo off
setlocal
if not exist build mkdir build
call build-sqlite.bat
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -Werror tests\modules.c src\core\workspace.c src\core\launch_queue.c src\core\batch_task.c src\core\file_command.c -o build\modules-test.exe
if errorlevel 1 exit /b %errorlevel%
build\modules-test.exe
if errorlevel 1 exit /b %errorlevel%
windres src\nova.rc -O coff -o build\nova-resource.o
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\core.c src\core\workspace.c src\core\launch_queue.c src\core\batch_task.c src\core\file_command.c src\platform\shell_icons.c src\platform\batch_runner.c src\ui\hold_drag.c src\ui\file_manager.c src\ui\batch_tasks.c src\platform\storage.c build\sqlite3.o build\nova-resource.o -o build\core-test.exe -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -luuid -lshlwapi
if errorlevel 1 exit /b %errorlevel%
build\core-test.exe %*
exit /b %errorlevel%
