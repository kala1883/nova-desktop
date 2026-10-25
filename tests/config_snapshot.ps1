param([string]$Executable = (Join-Path $PSScriptRoot '..\build\nova-desktop.exe'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$sourceRoot=Split-Path -Parent $PSScriptRoot
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('ns-'+[guid]::NewGuid().ToString('N').Substring(0,12))
$repo=Join-Path $fixtureRoot 'repo'; $publisher=Join-Path $fixtureRoot 'publisher'; $remote=Join-Path $fixtureRoot 'remote.git'
$config=Join-Path $repo 'config'; $exe=Join-Path $repo 'build\nova-data-fixture.exe'
$fixtureScript=Join-Path $repo 'deployment\config_snapshot.ps1'
$encoding=[Text.UTF8Encoding]::new($false)
function Check($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Git([string]$Directory,[string[]]$Arguments){
    $result=@(& git.exe -C $Directory @Arguments);if($LASTEXITCODE){throw ($result -join "`n")};return ,$result
}
function New-UsageData([string[]]$Ids){
    $rows=@([pscustomobject]@{scope='batch_tasks';section='Collection';key='Version';value='3'},[pscustomobject]@{scope='batch_tasks';section='Collection';key='Count';value=[string]$Ids.Count})
    for($i=0;$i -lt $Ids.Count;$i++){
        foreach($pair in @(@('Id',$Ids[$i]),@('Name',"task $($Ids[$i])"),@('Command','echo fixture'),@('Mode','0'),@('DirectoryCount','1'),@('Directory0','C:\fixture'),@('StepCommand0','echo step'))){
            $rows += [pscustomobject]@{scope='batch_tasks';section="Task$i";key=$pair[0];value=$pair[1]}
        }
    }
    $rows+=@([pscustomobject]@{scope='files';section='Pane0';key='Count';value='1'},[pscustomobject]@{scope='files';section='Pane0';key='Tab0';value='C:\fixture'})
    return [pscustomobject]@{version=1;application='NOVA Desktop';initialized=$true;settings=$rows;workspaces=@([pscustomobject]@{id='1';position=0;name='Files'});items=@()}
}
function Write-Data([string]$Path,$Value){[IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 100)+"`n",$encoding)}
function Snapshot([string]$Action,[bool]$Success=$true,[switch]$Preview){
    $arguments=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$fixtureScript,'-Action',$Action,'-Executable',$exe,'-ConfigurationDirectory',$config)
    if($Preview){$arguments+='-Preview'}
    & powershell.exe @arguments | Out-Host
    Check (($LASTEXITCODE -eq 0) -eq $Success) "Unexpected $Action exit status"
}
function Bytes([string]$Name){return [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $config $Name)))}
New-Item -ItemType Directory -Path $config,(Join-Path $repo 'build') -Force | Out-Null
try {
    Copy-Item -LiteralPath $Executable -Destination $exe
    New-Item -ItemType Directory -Path (Join-Path $repo 'deployment')|Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'deployment\configuration_merge.ps1') -Destination (Join-Path $repo 'deployment')
    # Keep the real application open during tests: only this disposable script
    # uses a private mutex and the isolated native loader instead of AppData/UI
    # initialization. JSON migration/validation and publication remain real.
    $scriptText=[IO.File]::ReadAllText((Join-Path $sourceRoot 'deployment\config_snapshot.ps1'))
    $fixtureMutexName='NOVA_SNAPSHOT_FIXTURE-'+[guid]::NewGuid().ToString('N')
    $scriptText=$scriptText.Replace("'NOVA_DESKTOP_SINGLE_INSTANCE'",("'"+$fixtureMutexName+"'"))
    $isolatedArguments=@'
('--check-data "'+$ConfigurationDirectory+'"')
'@
    $scriptText=$scriptText.Replace("'--migrate-data'",$isolatedArguments.Trim())
    [IO.File]::WriteAllText($fixtureScript,$scriptText,[Text.UTF8Encoding]::new($true))
    Copy-Item -LiteralPath (Join-Path $sourceRoot '.gitignore') -Destination $repo
    Git $fixtureRoot @('init','--bare',$remote)|Out-Null
    Git $repo @('init','-b','main')|Out-Null
    Git $repo @('config','user.name','NOVA fixture')|Out-Null;Git $repo @('config','user.email','fixture@example.invalid')|Out-Null
    Write-Data (Join-Path $config 'nova.json') (New-UsageData @('10'))
    Git $repo @('add','.')|Out-Null;Git $repo @('commit','-m','base')|Out-Null
    Git $repo @('remote','add','origin',$remote)|Out-Null;Git $repo @('push','-u','origin','main')|Out-Null
    Git $fixtureRoot @('clone','-b','main',$remote,$publisher)|Out-Null
    Git $publisher @('config','user.name','NOVA fixture')|Out-Null;Git $publisher @('config','user.email','fixture@example.invalid')|Out-Null
    Write-Data (Join-Path $config 'nova.json') (New-UsageData @('10','20'))
    $original=Bytes 'nova.json'
    Snapshot Migrate
    Check ((Bytes 'nova.migration.json') -ceq $original) 'Original dirty JSON not preserved byte-for-byte'
    $migrationStatus=Git $repo @('status','--porcelain');if($migrationStatus.Count){Git $repo @('diff','--','config/nova.json')|Out-Host};Check (-not $migrationStatus.Count) ('Migration left tracked configuration dirty: '+($migrationStatus -join '; '))
    $runtime=Bytes 'runtime.json';$local=Bytes 'local.json'
    $fixtureGuard=[Threading.Mutex]::new($false,$fixtureMutexName)
    try { Snapshot Import $false } finally { $fixtureGuard.Dispose() }
    Check ((Bytes 'runtime.json') -ceq $runtime) 'Running-instance guard changed runtime'
    Write-Data (Join-Path $publisher 'config\nova.json') (New-UsageData @('10','30'))
    Git $publisher @('add','config/nova.json')|Out-Null;Git $publisher @('commit','-m','remote task')|Out-Null;Git $publisher @('push')|Out-Null
    Git $repo @('pull','--ff-only','origin','main')|Out-Null
    Check ((Bytes 'runtime.json') -ceq $runtime) 'Pull overwrote runtime data'
    Snapshot Import -Preview
    Check ((Bytes 'runtime.json') -ceq $runtime) 'Preview changed runtime'
    Snapshot Import
    . (Join-Path $sourceRoot 'deployment\configuration_merge.ps1')
    $data=Get-Content -LiteralPath (Join-Path $config 'runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $tasks=Get-NovaTasksMap $data
    Check ($tasks.Count -eq 3 -and $tasks.ContainsKey('10') -and $tasks.ContainsKey('20') -and $tasks.ContainsKey('30')) 'Import lost local/remote task IDs'
    Check ($tasks['20']['StepCommand0'] -eq 'echo step' -and $tasks['20']['Directory0'] -eq 'C:\fixture') 'Import lost independent commands/folders'
    Check ((Bytes 'local.json') -ceq $local) 'Import changed private local state'
    Check (-not (Git $repo @('status','--porcelain')).Count) 'Ordinary import dirtied tracked snapshot'
    $runtime=Bytes 'runtime.json';$shared=Bytes 'nova.json';$baseline=Bytes 'runtime.base.json'
    $locked=[IO.File]::Open((Join-Path $config 'runtime.base.json'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try { Snapshot Export $false } finally { $locked.Dispose() }
    Check ((Bytes 'runtime.json') -ceq $runtime -and (Bytes 'nova.json') -ceq $shared -and (Bytes 'runtime.base.json') -ceq $baseline) 'Failed export did not roll back all documents'
    Snapshot Export
    Check ((Git $repo @('diff','--name-only'))[0] -eq 'config/nova.json') 'Export should explicitly change only tracked nova.json'
    Check ((Bytes 'runtime.json') -ceq (Bytes 'nova.json')) 'Export snapshot differs from merged runtime'
    $data=Get-Content -LiteralPath (Join-Path $config 'runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $other=ConvertFrom-Json (ConvertTo-Json -InputObject $data -Depth 100)
    ($data.settings|Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Command'}).value='echo local'
    ($other.settings|Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Command'}).value='echo remote'
    Write-Data (Join-Path $config 'runtime.json') $data;Write-Data (Join-Path $config 'nova.json') $other
    $runtime=Bytes 'runtime.json';$shared=Bytes 'nova.json';$baseline=Bytes 'runtime.base.json'
    Snapshot Import $false
    Check ((Bytes 'runtime.json') -ceq $runtime -and (Bytes 'nova.json') -ceq $shared -and (Bytes 'runtime.base.json') -ceq $baseline) 'Conflict changed files'
    [IO.File]::WriteAllText((Join-Path $config 'nova.json'),'{"version":1,"version":1,"application":"NOVA Desktop","initialized":true,"settings":[],"workspaces":[],"items":[]}',$encoding)
    Snapshot Import $false
    Check ((Bytes 'runtime.json') -ceq $runtime) 'Invalid input changed runtime'
    # Old v1 tasks have no stable IDs yet. An unchanged original snapshot must
    # reuse the first migration seed instead of allocating a second set of IDs.
    $config=Join-Path $fixtureRoot 'legacy'
    New-Item -ItemType Directory -Path $config|Out-Null
    $old=New-UsageData @('10')
    ($old.settings|Where-Object {$_.scope -eq 'batch_tasks' -and $_.section -eq 'Collection' -and $_.key -eq 'Version'}).value='1'
    $old.settings=@($old.settings|Where-Object {$_.key -ne 'Id'})
    Write-Data (Join-Path $config 'nova.json') $old
    $start=[Diagnostics.ProcessStartInfo]::new($exe,('--check-data "'+$config+'"'))
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WindowStyle='Hidden'
    $process=[Diagnostics.Process]::Start($start)
    try{$process.WaitForExit();Check ($process.ExitCode -eq 0) 'Legacy fixture migration failed'}finally{$process.Dispose()}
    $old=Get-Content -LiteralPath (Join-Path $config 'runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
    $identity=($old.settings|Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Id'}).value
    Snapshot Export
    $old=Get-Content -LiteralPath (Join-Path $config 'runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
    Check ((Get-NovaTasksMap $old).Count -eq 1 -and ($old.settings|Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Id'}).value -eq $identity) 'Export reallocated legacy task IDs'
    Write-Host 'PASS snapshot migration, exact backups, clean Git pull, preview, native validation, task/command/folder retention, explicit export, conflicts and publication rollback (temporary repository only)'
} finally {
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolved) -like 'ns-*') { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
