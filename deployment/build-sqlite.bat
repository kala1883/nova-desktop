@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
powershell -NoProfile -Command "$o=Get-Item build/sqlite3.o -ErrorAction SilentlyContinue; if($o -and $o.LastWriteTime -gt (Get-Item third_party/sqlite/sqlite3.c).LastWriteTime -and $o.LastWriteTime -gt (Get-Item deployment/build-sqlite.bat).LastWriteTime){exit 0}; exit 1"
if not errorlevel 1 goto success
gcc -std=c11 -Os -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 -c third_party\sqlite\sqlite3.c -o build\sqlite3.o
if errorlevel 1 goto failed

:success
popd
exit /b 0

:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
