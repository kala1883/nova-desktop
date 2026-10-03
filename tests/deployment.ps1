# Local-only deployment integration: a disposable repository and bare remote,
# with a tiny compiler stand-in. Never pushes or commits the user's repository.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path -Parent $PSScriptRoot
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('nova deployment ' + [guid]::NewGuid().ToString('N'))
$fixtureRepo = Join-Path $fixtureRoot 'repo with spaces'
$fixtureRemote = Join-Path $fixtureRoot 'remote.git'
$fixtureTools = Join-Path $fixtureRoot 'tools'
$originalPath = $env:PATH
$compiler = (Get-Command gcc -ErrorAction Stop).Source

function Check($condition, [string]$message) { if (-not $condition) { throw $message } }
function Invoke-FixtureGit([string[]]$gitArgs) {
    & git @gitArgs
    if ($LASTEXITCODE -ne 0) { throw "Fixture git failed: $($gitArgs -join ' ')" }
}
function Deploy([string]$message, [bool]$success) {
    & (Join-Path $fixtureRepo 'deployment\deploy_local.bat') $message
    Check (($LASTEXITCODE -eq 0) -eq $success) 'Unexpected deploy exit status'
}
function PackageCount {
    return @(Get-ChildItem -LiteralPath (Join-Path $fixtureRepo 'build\packages') -Filter '*.zip' -ErrorAction SilentlyContinue).Count
}

New-Item -ItemType Directory -Path $fixtureRepo, $fixtureTools -Force | Out-Null
Push-Location -LiteralPath $fixtureRoot
try {
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'deployment') -Destination $fixtureRepo -Recurse
    $fixtureConfig = Join-Path $fixtureRepo 'config'
    New-Item -ItemType Directory -Path $fixtureConfig -Force | Out-Null
    # Use synthetic data: the real config may now contain personal usage data.
    [IO.File]::WriteAllText((Join-Path $fixtureConfig 'nova.json'), '{"version":1,"application":"NOVA Desktop","initialized":false,"settings":[],"workspaces":[],"items":[]}', [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'config\README.md') -Destination $fixtureConfig
    foreach ($name in @('LICENSE','NOTICE.md','NOTICE.zh-CN.md','README.md','README.zh-CN.md')) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot $name) -Destination $fixtureRepo
    }
    Set-Content -LiteralPath (Join-Path $fixtureRepo '.gitignore') -Value '/build/' -Encoding ASCII
    New-Item -ItemType Directory -Path (Join-Path $fixtureRepo 'third_party\sqlite') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRepo 'third_party\sqlite\sqlite3.c') -Value '/* fixture */' -Encoding ASCII
    $stubSource = Join-Path $fixtureRoot 'compiler.c'
    @'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    if(getenv("NOVA_DEPLOY_TEST_BUILD_FAIL"))return 27;
    for(int i=1;i+1<argc;i++)if(!strcmp(argv[i],"-o")){
        FILE *file=fopen(argv[i+1],"wb");if(!file)return 28;
        const char *payload=getenv("NOVA_DEPLOY_TEST_PAYLOAD");
        fputs(payload?payload:"deployment test fixture",file);return fclose(file)?29:0;
    }
    return 30;
}
'@ | Set-Content -LiteralPath $stubSource -Encoding ASCII
    & $compiler $stubSource -o (Join-Path $fixtureTools 'gcc.exe')
    Check ($LASTEXITCODE -eq 0) 'Could not build compiler stand-in'
    Copy-Item -LiteralPath (Join-Path $fixtureTools 'gcc.exe') -Destination (Join-Path $fixtureTools 'windres.exe')
    $env:PATH = "$fixtureTools;$originalPath"
    Invoke-FixtureGit -gitArgs @('init','--bare',$fixtureRemote)
    Invoke-FixtureGit -gitArgs @('init','-b','main',$fixtureRepo)
    Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'config','user.name','NOVA deployment test')
    Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'config','user.email','nova-test@example.invalid')
    Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'remote','add','origin',$fixtureRemote)
    Deploy 'test: initial deployment' $true
    Check ((PackageCount) -eq 1) 'First deploy must create one archive'
    $latestExe = Join-Path $fixtureRepo 'build\packages\nova-desktop.exe'
    Check (([IO.File]::ReadAllText($latestExe)) -eq 'deployment test fixture') 'Missing latest executable at packages root'
    $firstRevision = Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'rev-parse','HEAD')
    Check ((Invoke-FixtureGit -gitArgs @('--git-dir',$fixtureRemote,'rev-parse','refs/heads/main')) -eq $firstRevision) 'Remote did not receive commit'
    Check (-not (Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'status','--porcelain'))) 'Generated output must stay ignored'
    $zip = Get-ChildItem -LiteralPath (Join-Path $fixtureRepo 'build\packages') -Filter '*.zip' | Select-Object -First 1
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
    try { foreach ($name in @('nova-desktop.exe','LICENSE','NOTICE.md','NOTICE.zh-CN.md','revision.txt','config/nova.json')) { Check ($null -ne $archive.GetEntry($name)) "Archive missing $name" } }
    finally { $archive.Dispose() }
    $env:NOVA_DEPLOY_TEST_PAYLOAD = 'second deployment fixture'
    Deploy 'test: no source changes' $true
    Check (([IO.File]::ReadAllText($latestExe)) -eq 'second deployment fixture') 'Latest executable was not replaced'
    Check (Test-Path -LiteralPath (Join-Path $fixtureRepo 'build\packages\config\nova.json')) 'Latest executable missing configuration copy'
    Check ((PackageCount) -eq 2) 'Clean deployment should still build a package'
    Check ((Invoke-FixtureGit -gitArgs @('-C',$fixtureRepo,'rev-parse','HEAD')) -eq $firstRevision) 'Clean deployment created an empty commit'
    $latestLock = [IO.File]::Open($latestExe,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        $env:NOVA_DEPLOY_TEST_PAYLOAD = 'replacement after unlock'
        Deploy 'test: locked latest executable' $false
        Check ((PackageCount) -eq 2) 'Locked latest executable produced an archive'
        Check (([IO.File]::ReadAllText($latestExe)) -eq 'second deployment fixture') 'Locked executable was changed'
        & (Join-Path $fixtureRepo 'deployment\build.bat') $latestExe
        Check ($LASTEXITCODE -ne 0) 'Direct latest build must reject a locked destination'
        Check (([IO.File]::ReadAllText($latestExe)) -eq 'second deployment fixture') 'Direct latest build overwrote a locked destination'
    }
    finally { $latestLock.Dispose() }
    # Reject the next push at a local hook; no build or archive may follow it.
    [IO.File]::WriteAllText((Join-Path $fixtureRemote 'hooks\pre-receive'), "#!/bin/sh`nexit 1`n", [Text.UTF8Encoding]::new($false))
    Add-Content -LiteralPath (Join-Path $fixtureRepo 'README.md') -Value 'push rejection fixture'
    Deploy 'test: rejected push' $false
    Check ((PackageCount) -eq 2) 'Rejected push produced a package'
    Remove-Item -LiteralPath (Join-Path $fixtureRemote 'hooks\pre-receive')
    $env:NOVA_DEPLOY_TEST_BUILD_FAIL = '1'
    Deploy 'test: failed build' $false
    Check ((PackageCount) -eq 2) 'Failed build produced an archive'
    Check (([IO.File]::ReadAllText($latestExe)) -eq 'second deployment fixture') 'Failed build changed latest executable'
    Remove-Item Env:NOVA_DEPLOY_TEST_BUILD_FAIL
    Deploy 'test: retry build' $true
    Check ((PackageCount) -eq 3) 'Retry did not recover'
    & (Join-Path $fixtureRepo 'deployment\build.bat') $latestExe
    Check ($LASTEXITCODE -eq 0) 'Direct latest executable build did not recover after unlock'
    Write-Host 'PASS deployment: external cwd with spaces, commit/push, clean rerun, archive, rejected push and failed build'
}
finally {
    $env:PATH = $originalPath
    $env:NOVA_DEPLOY_TEST_PAYLOAD = $null
    Remove-Item Env:NOVA_DEPLOY_TEST_BUILD_FAIL -ErrorAction SilentlyContinue
    Pop-Location
    # The exact unique temp root is resolved and checked before recursive cleanup.
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedFixture.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedFixture) -like 'nova deployment *') {
        Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
    }
}
