@echo off
setlocal
if not exist build mkdir build
powershell -NoProfile -Command "$o=Get-Item build/sqlite3.o -ErrorAction SilentlyContinue; if($o -and $o.LastWriteTime -gt (Get-Item third_party/sqlite/sqlite3.c).LastWriteTime -and $o.LastWriteTime -gt (Get-Item build-sqlite.bat).LastWriteTime){exit 0}; exit 1"
if not errorlevel 1 exit /b 0
gcc -std=c11 -Os -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 -c third_party\sqlite\sqlite3.c -o build\sqlite3.o
exit /b %errorlevel%
