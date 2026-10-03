@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "nova_output=build\nova-desktop.exe"
if not "%~1"=="" set "nova_output=%~1"
for %%I in ("%nova_output%") do if not exist "%%~dpI" mkdir "%%~dpI"
if errorlevel 1 goto failed
set "nova_compile_output=%nova_output%"
for %%I in ("%nova_output%") do set "nova_target_path=%%~fI"
for %%I in ("build\packages\nova-desktop.exe") do set "nova_fixed_path=%%~fI"
if /I "%nova_target_path%"=="%nova_fixed_path%" set "nova_compile_output=build\.nova-build-latest.exe"
if not exist build mkdir build
call "%~dp0build-sqlite.bat"
if errorlevel 1 goto failed
windres src\nova.rc -O coff -o build\nova-resource.o
if errorlevel 1 goto failed
gcc -std=c11 -O2 -Wall -Wextra -municode src\main.c src\core\workspace.c src\core\launch_queue.c src\core\batch_task.c src\core\file_command.c src\platform\shell_icons.c src\platform\batch_runner.c src\ui\hold_drag.c src\ui\file_manager.c src\ui\batch_tasks.c src\platform\storage.c src\platform\config_json.c build\sqlite3.o build\nova-resource.o -o "%nova_compile_output%" -s -mwindows -ldwmapi -lcomdlg32 -lcomctl32 -luxtheme -lshell32 -lole32 -ladvapi32 -lgdi32 -luser32 -luuid -lshlwapi
if errorlevel 1 goto failed
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish_latest.ps1" -Executable "%nova_compile_output%"
if errorlevel 1 goto failed
echo Built "%nova_output%"

:success
popd
exit /b 0

:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
