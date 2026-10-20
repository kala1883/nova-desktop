@echo off
setlocal
call "%~dp0toolchain.bat"
if errorlevel 1 exit /b 1
call "%~dp0openssl.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
if not exist build mkdir build
powershell.exe -NoProfile -Command "$o=Get-Item build/ssh2.o -ErrorAction SilentlyContinue; $compiler=(Get-Command gcc.exe).Source; $identity=$compiler+'|'+$env:NOVA_OPENSSL_SELECTED_ROOT; $stamp=Get-Content build/ssh2.inputs -Raw -ErrorAction SilentlyContinue; $s=@(Get-ChildItem third_party/libssh2/src,third_party/libssh2/include,(Join-Path $env:NOVA_OPENSSL_SELECTED_ROOT 'include/openssl') -File); $s+=Get-Item deployment/build-ssh.bat,deployment/openssl.ps1,deployment/openssl.bat,$compiler; if($o -and $stamp -ceq $identity -and -not ($s | Where-Object LastWriteTime -ge $o.LastWriteTime)){exit 0}; exit 1"
if not errorlevel 1 goto success
rem Relocatable library objects must not contain MinGW executable startup code.
gcc -std=c11 -Os -nostdlib -DLIBSSH2_STATIC -DLIBSSH2_OPENSSL -DLIBSSH2_NO_DSA -DLIBSSH2_NO_RC4 -DLIBSSH2_NO_MD5 -DLIBSSH2_NO_HMAC_RIPEMD -DLIBSSH2_NO_3DES -DLIBSSH2_NO_BLOWFISH -DLIBSSH2_NO_CAST -DLIBSSH2_NO_AES_CBC -Ithird_party/libssh2/include -Ithird_party/libssh2/src -r third_party/libssh2/src/agent.c third_party/libssh2/src/bcrypt_pbkdf.c third_party/libssh2/src/channel.c third_party/libssh2/src/comp.c third_party/libssh2/src/chacha.c third_party/libssh2/src/cipher-chachapoly.c third_party/libssh2/src/crypt.c third_party/libssh2/src/crypto.c third_party/libssh2/src/global.c third_party/libssh2/src/hostkey.c third_party/libssh2/src/keepalive.c third_party/libssh2/src/kex.c third_party/libssh2/src/knownhost.c third_party/libssh2/src/mac.c third_party/libssh2/src/misc.c third_party/libssh2/src/packet.c third_party/libssh2/src/pem.c third_party/libssh2/src/poly1305.c third_party/libssh2/src/publickey.c third_party/libssh2/src/scp.c third_party/libssh2/src/session.c third_party/libssh2/src/sftp.c third_party/libssh2/src/transport.c third_party/libssh2/src/userauth.c third_party/libssh2/src/userauth_kbd_packet.c third_party/libssh2/src/version.c -o build/ssh2.o
if errorlevel 1 goto failed
powershell.exe -NoProfile -Command "$identity=(Get-Command gcc.exe).Source+'|'+$env:NOVA_OPENSSL_SELECTED_ROOT; [IO.File]::WriteAllText((Join-Path $PWD 'build/ssh2.inputs'),$identity)"
if errorlevel 1 goto failed
:success
popd
exit /b 0
:failed
set "nova_exit=%errorlevel%"
popd
exit /b %nova_exit%
