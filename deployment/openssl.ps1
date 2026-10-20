function Resolve-NovaOpenSSLRoot {
    param([string]$PreferredRoot = $env:NOVA_OPENSSL_ROOT,
          [string]$CompilerBin = (Split-Path -Parent (Get-Command gcc.exe -ErrorAction Stop).Source))
    $explicit = -not [string]::IsNullOrWhiteSpace($PreferredRoot)
    if ($explicit) { $roots = @($PreferredRoot.Trim().Trim('"')) }
    else {
        $prefix = Split-Path -Parent $CompilerBin
        $roots = @($prefix, (Join-Path $prefix 'opt'))
        $localRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\deps\openssl'
        $marker = Join-Path $localRoot 'compiler.txt'
        if ((Test-Path -LiteralPath $marker -PathType Leaf) -and
            [IO.File]::ReadAllText($marker) -ceq (Join-Path $CompilerBin 'gcc.exe')) { $roots = @($localRoot) + $roots }
    }
    foreach ($root in $roots) {
        $complete = $true
        foreach ($file in @('include\openssl\opensslconf.h','include\openssl\crypto.h','include\openssl\opensslv.h','lib\libcrypto.a')) {
            if (-not (Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)) { $complete = $false; break }
        }
        if ($complete) {
            $version = [IO.File]::ReadAllText((Join-Path $root 'include\openssl\opensslv.h'))
            if ($version -match '(?m)^\s*#\s*define\s+OPENSSL_VERSION_MAJOR\s+([3-9][0-9]*)\b') { return [IO.Path]::GetFullPath($root) }
        }
    }
    throw 'OpenSSL 3 development headers and static libcrypto.a not found. Use the selected MinGW prefix (or its opt folder), or set NOVA_OPENSSL_ROOT. MSYS2 MINGW64: pacman -S mingw-w64-x86_64-openssl / 缺少 OpenSSL 3 开发头文件或静态 libcrypto.a。请安装到当前 MinGW 前缀（或 opt 目录），或设置 NOVA_OPENSSL_ROOT。'
}

function Initialize-NovaOpenSSL {
    $root = Resolve-NovaOpenSSLRoot
    $include = Join-Path $root 'include'; $library = Join-Path $root 'lib'
    if ($env:CPATH) { $include += ';' + $env:CPATH }
    if ($env:LIBRARY_PATH) { $library += ';' + $env:LIBRARY_PATH }
    $env:CPATH = $include; $env:LIBRARY_PATH = $library
    $env:NOVA_OPENSSL_SELECTED_ROOT = $root
}

function Test-NovaOpenSSLLink {
    # Compile/link a real consumer before deployment is allowed to stage or push.
    $probeRoot = Join-Path ([IO.Path]::GetTempPath()) ('nova openssl ' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $probeRoot | Out-Null
    try {
        $source = Join-Path $probeRoot 'probe.c'; $output = Join-Path $probeRoot 'probe.exe'
        [IO.File]::WriteAllText($source, @'
#include <openssl/opensslconf.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_MAJOR < 3
#error OpenSSL 3 is required
#endif
int main(void) {
    return OpenSSL_version_num() == 0;
}
'@, [Text.Encoding]::ASCII)
        & gcc.exe $source -o $output '-l:libcrypto.a' '-lws2_32' '-lcrypt32' '-lbcrypt'
        if ($LASTEXITCODE -ne 0) { throw 'OpenSSL compile/link check failed; nothing staged or pushed / OpenSSL 编译链接检查失败，未暂存或推送。' }
    }
    finally {
        # This path is created directly under the system temp folder above.
        $resolved = [IO.Path]::GetFullPath($probeRoot)
        if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolved) -like 'nova openssl *') { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try { Resolve-NovaOpenSSLRoot }
    catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
}
