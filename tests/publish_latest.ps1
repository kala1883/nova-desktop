$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $sourceRoot 'deployment/toolchain.ps1')
Initialize-NovaToolchain
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('nova publish ' + [guid]::NewGuid().ToString('N'))
$process = $null
function Check($Condition,[string]$Message) { if (-not $Condition) { throw $Message } }
function Publish([string]$Executable) {
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixtureRoot 'deployment/publish_latest.ps1') -Executable $Executable | Out-Host
    return $LASTEXITCODE
}
New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'deployment'),(Join-Path $fixtureRoot 'config'),(Join-Path $fixtureRoot 'build/packages') -Force | Out-Null
try {
    foreach ($name in @('publish_latest.ps1','configuration_info.ps1','package_sync.ps1','deploy-sync-server.ps1','config_snapshot.bat','config_snapshot.ps1','configuration_merge.ps1')) {
        Copy-Item -LiteralPath (Join-Path $sourceRoot "deployment/$name") -Destination (Join-Path $fixtureRoot "deployment/$name")
    }
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'sync') -Destination (Join-Path $fixtureRoot 'sync') -Recurse
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'config/nova.json'),'{"version":1,"application":"NOVA Desktop","initialized":false,"settings":[],"workspaces":[],"items":[]}',[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'THIRD_PARTY_NOTICES.txt'),'fixture')
    $helperSource = Join-Path $sourceRoot 'tests/fixtures/hold_executable.c'
    $latest=Join-Path $fixtureRoot 'build/packages/nova-desktop.exe'
    & gcc.exe -std=c11 -Wall -Wextra -Werror -municode $helperSource -o $latest
    Check ($LASTEXITCODE -eq 0) 'Could not build executable-lock fixture'
    $ready=Join-Path $fixtureRoot 'ready';$quit=Join-Path $fixtureRoot 'quit'
    $process=Start-Process -FilePath $latest -ArgumentList @('"'+$ready+'"','"'+$quit+'"') -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while(-not (Test-Path -LiteralPath $ready) -and [DateTime]::UtcNow -lt $deadline -and -not $process.HasExited){Start-Sleep -Milliseconds 25}
    Check ((Test-Path -LiteralPath $ready) -and -not $process.HasExited) 'Executable fixture did not become ready'
    $source=Join-Path $fixtureRoot 'new.exe';[IO.File]::WriteAllText($source,'new executable fixture')
    $exitCode=Publish $source
    Check ($exitCode -eq 0) 'Publication succeeded but running-image backup cleanup returned failure'
    Check ([IO.File]::ReadAllText($latest) -ceq 'new executable fixture') 'Newest executable was not published'
    Check (-not $process.HasExited) 'Publisher terminated the running executable'
    $heldBackups=@(Get-ChildItem -LiteralPath (Split-Path -Parent $latest) -Filter '.nova-previous-*.tmp')
    Check ($heldBackups.Count -eq 1) 'Running-image backup was not retained'
    [IO.File]::WriteAllText($quit,'quit')
    Check ($process.WaitForExit(10000)) 'Executable fixture did not exit'
    $process.Dispose();$process=$null
    # The next publication removes the unlocked old image and leaves other files alone.
    $unrelated=Join-Path $fixtureRoot 'build/packages/keep.tmp';[IO.File]::WriteAllText($unrelated,'keep')
    $exitCode=Publish $source
    Check ($exitCode -eq 0) 'Retry after process exit failed'
    Check (@(Get-ChildItem -LiteralPath (Split-Path -Parent $latest) -Filter '.nova-previous-*.tmp').Count -eq 0) 'Unlocked deferred backup was not collected'
    Check ((Test-Path -LiteralPath $unrelated) -and -not @(Get-ChildItem -LiteralPath (Split-Path -Parent $latest) -Filter '.nova-latest-*.tmp').Count) 'Cleanup touched unrelated files or left the publication temporary'
    # A true replacement failure must still fail and preserve the old executable.
    [IO.File]::WriteAllText($source,'replacement while locked')
    $lock=[IO.File]::Open($latest,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        $exitCode=Publish $source
        Check ($exitCode -ne 0 -and [IO.File]::ReadAllText($latest) -ceq 'new executable fixture') 'Failed replacement was reported as success or changed the previous executable'
    } finally { $lock.Dispose() }
    Write-Host 'PASS publication: actual running-image lock, successful replacement, deferred cleanup, untouched running process, unlocked retry and true failure preservation'
}
finally {
    if ($process) {
        [IO.File]::WriteAllText((Join-Path $fixtureRoot 'quit'),'quit')
        if (-not $process.WaitForExit(10000)) { $process.Kill();$process.WaitForExit() }
        $process.Dispose()
    }
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved) -like 'nova publish *') { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
