@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
powershell.exe -NoProfile -Command "$o=Get-Item build/vterm.o -ErrorAction SilentlyContinue; $compiler=(Get-Command gcc.exe).Source; $stamp=Get-Content build/vterm.inputs -Raw -ErrorAction SilentlyContinue; $s=@(Get-ChildItem third_party/libvterm/src,third_party/libvterm/include -Recurse -File); $s+=Get-Item deployment/build-terminal.bat,$compiler; if($o -and $stamp -ceq $compiler -and -not ($s | Where-Object LastWriteTime -ge $o.LastWriteTime)){exit 0}; exit 1"
if not errorlevel 1 goto success
rem Upstream ELF visibility attributes do not apply to MinGW PE objects.
gcc -std=c11 -Os -Wno-attributes -nostdlib -Ithird_party/libvterm/include -r third_party/libvterm/src/encoding.c third_party/libvterm/src/keyboard.c third_party/libvterm/src/mouse.c third_party/libvterm/src/parser.c third_party/libvterm/src/pen.c third_party/libvterm/src/screen.c third_party/libvterm/src/state.c third_party/libvterm/src/unicode.c third_party/libvterm/src/vterm.c -o build/vterm.o
if errorlevel 1 goto failed
powershell.exe -NoProfile -Command "[IO.File]::WriteAllText((Join-Path $PWD 'build/vterm.inputs'),(Get-Command gcc.exe).Source)"
if errorlevel 1 goto failed
:success
popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
