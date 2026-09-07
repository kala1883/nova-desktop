@echo off
setlocal
if not exist build mkdir build
windres src\nova.rc -O coff -o build\nova-resource.o
if errorlevel 1 exit /b %errorlevel%
gcc -std=c11 -O2 -Wall -Wextra -municode src\main.c src\core\workspace.c src\core\launch_queue.c src\platform\shell_icons.c src\ui\hold_drag.c build\nova-resource.o -o build\nova-desktop.exe -s -mwindows -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32
if errorlevel 1 exit /b %errorlevel%
echo Built build\nova-desktop.exe
