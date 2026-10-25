$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$testRepo=Split-Path -Parent $PSScriptRoot
$testRoot=Join-Path $testRepo ('build\sync-migration-test-'+[guid]::NewGuid().ToString('N'))
$testNative=Join-Path $testRoot 'native'
$testLegacy=Join-Path $testRoot 'legacy'
function Check($condition,[string]$message){if(-not $condition){throw $message}}
try {
    New-Item -ItemType Directory -Path (Join-Path $testNative 'deployment'),(Join-Path $testNative 'config'),(Join-Path $testLegacy 'chunksync'),(Join-Path $testLegacy 'scripts'),(Join-Path $testLegacy 'runtime\app\client\chunks') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $testRepo 'deployment\migrate-sync.ps1') -Destination (Join-Path $testNative 'deployment')
    [IO.File]::WriteAllText((Join-Path $testLegacy 'chunksync\client.py'),'# synthetic marker')
    [IO.File]::WriteAllText((Join-Path $testLegacy 'scripts\Stop-Sync.ps1'),'exit 0')
    $testConfig=@{identity=@{name='Synthetic A';node_id='synthetic-original-node';registered=$true;role='A'};connection=@{host='fixture-alias';remote_project='/opt/fixture';remote_root='/opt/fixture/runtime/broker'};folders=@(@{id='pair1';dataset='dataset1';name='Fixture pair';group_name='Fixture group';path='C:\synthetic-only';excludes=@('.git','*.tmp');paused=$false;sync_deletes=$false})}
    $testConfig|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $testLegacy 'runtime\app\config.json') -Encoding UTF8
    $testConfigBytes=[IO.File]::ReadAllBytes((Join-Path $testLegacy 'runtime\app\config.json'))
    [IO.File]::WriteAllText((Join-Path $testLegacy 'runtime\app\client\chunks\fixture'),'immutable fixture')
    [IO.File]::WriteAllText((Join-Path $testNative 'config\nova.json'),'synthetic shared configuration')
    $profiles=@{settings=@(@{scope='ssh_servers';section='Server0';key='Id';value='77'},@{scope='ssh_servers';section='Server0';key='Host';value='fixture.invalid'},@{scope='ssh_servers';section='Server0';key='User';value='fixture'},@{scope='ssh_servers';section='Server0';key='Port';value='22'})}
    $profiles|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $testNative 'config\runtime.json') -Encoding UTF8
    $runner=Join-Path $testRoot 'run-fixture.ps1'
    @'
param($Script,$Source)
function global:ssh { 'hostname fixture.invalid'; 'user fixture'; 'port 22' }
& $Script -Source $Source -ArchiveSource -ControllerAlreadyStopped
exit $LASTEXITCODE
'@ | Set-Content -LiteralPath $runner -Encoding ASCII
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $runner -Script (Join-Path $testNative 'deployment\migrate-sync.ps1') -Source $testLegacy
    Check ($LASTEXITCODE -eq 0) 'Migration failed'
    $testImported=Get-Content -LiteralPath (Join-Path $testNative 'config\sync\import.json') -Raw|ConvertFrom-Json
    Check ($testImported.identity.node_id -ceq 'synthetic-original-node' -and $testImported.folders[0].paused -eq $true) 'Identity changed or pair enabled'
    Check ($testImported.nova_server_id -eq '77') 'Migration used stale shared profiles instead of runtime data'
    Check ([IO.File]::ReadAllText((Join-Path $testNative 'config\sync\client\chunks\fixture')) -ceq 'immutable fixture') 'Cache changed'
    Check ([IO.File]::ReadAllText((Join-Path $testNative 'config\nova.json')) -ceq 'synthetic shared configuration') 'Shared configuration changed'
    $testArchived=Join-Path $testNative 'build\sync-migration\archive\legacy'
    Check ((Test-Path -LiteralPath $testArchived) -and -not (Test-Path -LiteralPath $testLegacy)) 'Archive move failed'
    Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $testArchived 'runtime\app\config.json'))) -ceq [Convert]::ToBase64String($testConfigBytes)) 'Original configuration was not preserved'
    Copy-Item -LiteralPath $testArchived -Destination $testLegacy -Recurse
    $testSavedPreference=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $testNative 'deployment\migrate-sync.ps1') -Source $testLegacy -ControllerAlreadyStopped 2>$null | Out-Null
    $testRefused=$LASTEXITCODE -ne 0
    $ErrorActionPreference=$testSavedPreference
    Check $testRefused 'Existing native data was overwritten'
    Write-Host 'PASS isolated migration: identity/cache preservation, paused pairs, exact archive, unchanged shared config and overwrite refusal; no controller, credentials or network used'
} finally {
    $testResolved=[IO.Path]::GetFullPath($testRoot)
    if($testResolved.StartsWith([IO.Path]::GetFullPath((Join-Path $testRepo 'build'))+'\',[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $testResolved) -cmatch '^sync-migration-test-[0-9a-f]{32}$'){
        Remove-Item -LiteralPath $testResolved -Recurse -Force
    }
}
