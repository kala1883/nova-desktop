$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '..\deployment\openssl.ps1')
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('nova openssl tests ' + [guid]::NewGuid().ToString('N'))
$originalRoot=$env:NOVA_OPENSSL_ROOT; $originalInclude=$env:CPATH; $originalLibrary=$env:LIBRARY_PATH
$originalSelected=$env:NOVA_OPENSSL_SELECTED_ROOT
function Check($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function New-OpenSSL([string]$Root) {
    New-Item -ItemType Directory -Path (Join-Path $Root 'include/openssl'),(Join-Path $Root 'lib') -Force | Out-Null
    foreach ($name in @('opensslconf.h','crypto.h','opensslv.h')) { [IO.File]::WriteAllText((Join-Path $Root "include/openssl/$name"),'/* fixture */') }
    [IO.File]::WriteAllText((Join-Path $Root 'include/openssl/opensslv.h'),'# define OPENSSL_VERSION_MAJOR 3')
    [IO.File]::WriteAllText((Join-Path $Root 'lib/libcrypto.a'),'fixture')
}
try {
    $prefix=Join-Path $fixtureRoot 'compiler';$bin=Join-Path $prefix 'bin';$opt=Join-Path $prefix 'opt'
    New-Item -ItemType Directory -Path $bin -Force | Out-Null
    New-OpenSSL $opt
    Check ((Resolve-NovaOpenSSLRoot -PreferredRoot '' -CompilerBin $bin) -eq $opt) 'Compiler opt layout was not found'
    New-OpenSSL $prefix
    Check ((Resolve-NovaOpenSSLRoot -PreferredRoot '' -CompilerBin $bin) -eq $prefix) 'Normal compiler prefix was not preferred'
    [IO.File]::WriteAllText((Join-Path $prefix 'include/openssl/opensslv.h'),'# define OPENSSL_VERSION_NUMBER 0x100020ffL')
    Check ((Resolve-NovaOpenSSLRoot -PreferredRoot '' -CompilerBin $bin) -eq $opt) 'Legacy OpenSSL was accepted despite missing Ed25519 support'
    $custom=Join-Path $fixtureRoot 'custom & spaces';New-OpenSSL $custom
    Check ((Resolve-NovaOpenSSLRoot -PreferredRoot $custom -CompilerBin $bin) -eq $custom) 'Explicit OpenSSL root was ignored'
    $failed=$false
    try { Resolve-NovaOpenSSLRoot -PreferredRoot (Join-Path $fixtureRoot 'missing') -CompilerBin $bin | Out-Null }
    catch { $failed=$_.Exception.Message -match 'NOVA_OPENSSL_ROOT' }
    Check $failed 'Invalid explicit root silently used another installation'
    Remove-Item -LiteralPath (Join-Path $custom 'lib/libcrypto.a')
    [IO.File]::WriteAllText((Join-Path $custom 'lib/libcrypto.dll.a'),'fixture')
    $failed=$false
    try { Resolve-NovaOpenSSLRoot -PreferredRoot $custom -CompilerBin $bin | Out-Null } catch { $failed=$true }
    Check $failed 'An import library was accepted as static libcrypto'
    New-OpenSSL $custom
    $env:NOVA_OPENSSL_ROOT=$custom;$env:CPATH='original include';$env:LIBRARY_PATH='original lib'
    Initialize-NovaOpenSSL
    Check ($env:CPATH -eq "$custom\include;original include" -and $env:LIBRARY_PATH -eq "$custom\lib;original lib") 'PowerShell lost existing search paths'
    $bridge=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\deployment\openssl.bat'))
    $probe=Join-Path $fixtureRoot 'probe.bat'
    [IO.File]::WriteAllLines($probe,@('@echo off',"call `"$bridge`"",'if errorlevel 1 exit /b 1','set NOVA_OPENSSL_SELECTED_ROOT','set CPATH','set LIBRARY_PATH'))
    $env:CPATH=$null;$env:LIBRARY_PATH=$null
    $result=@(& $probe)
    Check ($LASTEXITCODE -eq 0 -and $result -contains "CPATH=$custom\include" -and $result -contains "LIBRARY_PATH=$custom\lib") 'Batch bridge lost paths with spaces/ampersand or added empty search directories'
    Check (-not $env:CPATH -and -not $env:LIBRARY_PATH) 'Batch bridge modified parent environment'
    $env:NOVA_OPENSSL_ROOT=Join-Path $fixtureRoot 'missing'
    & $probe | Out-Null
    Check ($LASTEXITCODE -ne 0) 'Batch bridge accepted missing OpenSSL'
    Write-Host 'PASS OpenSSL: prefix/opt discovery, legacy refusal, explicit override, static library requirement, environment preservation and batch failure propagation'
}
finally {
    $env:NOVA_OPENSSL_ROOT=$originalRoot;$env:CPATH=$originalInclude;$env:LIBRARY_PATH=$originalLibrary
    $env:NOVA_OPENSSL_SELECTED_ROOT=$originalSelected
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved) -like 'nova openssl tests *') { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
