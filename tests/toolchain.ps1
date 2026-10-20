$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '..\deployment\toolchain.ps1')
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('nova toolchain ' + [guid]::NewGuid().ToString('N'))
$originalPath = $env:PATH
$originalOverride = $env:NOVA_MINGW_BIN
function Check($condition, [string]$message) { if (-not $condition) { throw $message } }
function New-CompilerBin([string]$directory) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    foreach ($name in @('gcc.exe', 'windres.exe')) { [IO.File]::WriteAllText((Join-Path $directory $name), '') }
}
try {
    $first = Join-Path $fixtureRoot 'first bin'
    $second = Join-Path $fixtureRoot 'preferred bin & spaces'
    $incomplete = Join-Path $fixtureRoot 'incomplete'
    $installRoot = Join-Path $fixtureRoot 'apps with spaces\msys64'
    $mingw = Join-Path $installRoot 'mingw64\bin'
    $ucrt = Join-Path $installRoot 'ucrt64\bin'
    foreach ($directory in @($first, $second, $incomplete, $mingw, $ucrt)) { New-CompilerBin $directory }
    Remove-Item -LiteralPath (Join-Path $incomplete 'windres.exe')
    $resolved = Resolve-NovaToolchainBin -PreferredBin '' -SearchPath "$incomplete;`"$first`";$second" -InstallRoots @($installRoot)
    Check ($resolved -eq $first) 'PATH precedence, quoted paths or incomplete-toolchain handling failed'
    Check ((Resolve-NovaToolchainBin -PreferredBin $second -SearchPath $first -InstallRoots @()) -eq $second) 'Explicit toolchain override was ignored'
    Check ((Resolve-NovaToolchainBin -PreferredBin '' -SearchPath '' -InstallRoots @($installRoot)) -eq $mingw) 'MSYS2 fallback did not prefer MINGW64'
    Remove-Item -LiteralPath (Join-Path $mingw 'gcc.exe')
    Check ((Resolve-NovaToolchainBin -PreferredBin '' -SearchPath '' -InstallRoots @($installRoot)) -eq $ucrt) 'UCRT64 fallback failed'
    $roots = @(Get-NovaMsysRoots -DriveRoots @($fixtureRoot))
    Check ($roots -contains $installRoot) 'MSYS2 under an immediate drive subdirectory was not discovered'
    $failed = $false
    try { Resolve-NovaToolchainBin -PreferredBin $incomplete -SearchPath $first -InstallRoots @() | Out-Null }
    catch { $failed = $_.Exception.Message -match 'NOVA_MINGW_BIN' }
    Check $failed 'Invalid explicit override silently fell back to another compiler'
    $failed = $false
    try { Resolve-NovaToolchainBin -PreferredBin '' -SearchPath $incomplete -InstallRoots @() | Out-Null }
    catch { $failed = $_.Exception.Message -match 'NOVA_MINGW_BIN' }
    Check $failed 'Missing toolchain did not explain how to configure it'

    $env:NOVA_MINGW_BIN = $second
    Initialize-NovaToolchain
    Check ($env:PATH.StartsWith("$second;")) 'PowerShell did not prepend the selected toolchain'
    $env:PATH = $originalPath
    $probe = Join-Path $fixtureRoot 'probe.bat'
    $bridge = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\deployment\toolchain.bat'))
    [IO.File]::WriteAllLines($probe, @('@echo off', "call `"$bridge`"", 'if errorlevel 1 exit /b 1', 'where.exe gcc.exe', 'where.exe windres.exe'))
    $output = & $probe
    Check ($LASTEXITCODE -eq 0 -and $output[0] -eq (Join-Path $second 'gcc.exe') -and
        $output -contains (Join-Path $second 'windres.exe')) 'Batch bridge lost the toolchain path containing spaces/ampersand'
    Check ($env:PATH -eq $originalPath) 'Batch bridge changed the parent process environment'
    $env:NOVA_MINGW_BIN = $incomplete
    & $probe
    Check ($LASTEXITCODE -ne 0) 'Batch bridge did not propagate toolchain failure'
    Write-Host 'PASS toolchain: PATH/override precedence, paired tools, MSYS2 discovery, fallback, quoted paths and failure propagation'
}
finally {
    $env:PATH = $originalPath
    $env:NOVA_MINGW_BIN = $originalOverride
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedFixture.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedFixture) -like 'nova toolchain *') {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
