@echo off
setlocal
if not exist build mkdir build
gcc -std=c11 -O2 -Wall -Wextra -Werror tests\modules.c src\core\workspace.c src\core\launch_queue.c -o build\modules-test.exe
if errorlevel 1 exit /b %errorlevel%
build\modules-test.exe
if errorlevel 1 exit /b %errorlevel%
windres src\nova.rc -O coff -o build\nova-resource.o
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -Werror -municode tests\core.c src\core\workspace.c src\core\launch_queue.c src\platform\shell_icons.c src\ui\hold_drag.c build\nova-resource.o -o build\core-test.exe -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32
if errorlevel 1 exit /b %errorlevel%
build\core-test.exe %*
exit /b %errorlevel%
