$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$sourceRoot=Split-Path -Parent $PSScriptRoot
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('nova config sync '+[guid]::NewGuid().ToString('N'))
$repo=Join-Path $fixtureRoot 'repo';$publisher=Join-Path $fixtureRoot 'publisher';$remote=Join-Path $fixtureRoot 'remote.git'
function Check($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Git([string]$Directory,[string[]]$Arguments){
    $result=@(& git.exe -C $Directory @Arguments);if($LASTEXITCODE){throw ($result -join "`n")};return ,$result
}
function Write-Config([string]$Directory,$Data){[IO.File]::WriteAllText((Join-Path $Directory 'config\nova.json'),(ConvertTo-Json -InputObject $Data -Depth 100)+"`n",[Text.UTF8Encoding]::new($false))}
function Read-Config([string]$Directory){return Get-Content -LiteralPath (Join-Path $Directory 'config\nova.json') -Raw -Encoding UTF8 | ConvertFrom-Json}
function Sync([bool]$Success,[switch]$Preview){
    $args=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $repo 'deployment\sync_config.ps1'))
    if($Preview){$args+='-Preview'}
    & powershell.exe @args | Out-Host
    Check (($LASTEXITCODE -eq 0) -eq $Success) 'Unexpected synchronization status'
}
New-Item -ItemType Directory -Path $fixtureRoot,$repo -Force | Out-Null
try {
    Git $fixtureRoot @('init','--bare',$remote) | Out-Null
    Git $repo @('init','-b','main') | Out-Null
    Git $repo @('config','user.name','NOVA fixture') | Out-Null
    Git $repo @('config','user.email','fixture@example.invalid') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $repo 'deployment'),(Join-Path $repo 'config') -Force | Out-Null
    foreach($name in @('sync_config.ps1','sync_config.bat','configuration_merge.ps1')){Copy-Item -LiteralPath (Join-Path $sourceRoot "deployment\$name") -Destination (Join-Path $repo "deployment\$name")}
    Set-Content -LiteralPath (Join-Path $repo '.gitignore') -Value '/build/' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $repo 'other.txt') -Value 'base' -Encoding ASCII
    $data=[pscustomobject]@{version=1;application='NOVA Desktop';initialized=$true;settings=@();workspaces=@([pscustomobject]@{id='1';position=0;name='Files'});items=@()}
    Write-Config $repo $data
    Git $repo @('add','.') | Out-Null;Git $repo @('commit','-m','base') | Out-Null
    Git $repo @('remote','add','origin',$remote) | Out-Null;Git $repo @('push','-u','origin','main') | Out-Null
    Git $fixtureRoot @('clone','-b','main',$remote,$publisher) | Out-Null
    Git $publisher @('config','user.name','NOVA fixture') | Out-Null;Git $publisher @('config','user.email','fixture@example.invalid') | Out-Null
    $local=Read-Config $repo;$incoming=Read-Config $publisher
    $local.settings=@([pscustomobject]@{scope='app';section='Fixture';key='Local';value='saved'})
    $incoming.settings=@([pscustomobject]@{scope='app';section='Fixture';key='Remote';value='saved'})
    Write-Config $repo $local;Write-Config $publisher $incoming
    Git $publisher @('add','.') | Out-Null;Git $publisher @('commit','-m','remote addition') | Out-Null;Git $publisher @('push') | Out-Null
    $before=[IO.File]::ReadAllText((Join-Path $repo 'config\nova.json'));$head=(Git $repo @('rev-parse','HEAD'))[0]
    Sync $true -Preview
    Check ((Git $repo @('rev-parse','HEAD'))[0] -eq $head -and [IO.File]::ReadAllText((Join-Path $repo 'config\nova.json')) -ceq $before) 'Preview changed working data or HEAD'
    Sync $true
    $saved=Read-Config $repo
    Check ($saved.settings.Count -eq 2 -and (Git $repo @('stash','list')).Count -eq 0) 'Sync lost data or touched stashes'
    Check ((Git $repo @('rev-parse','HEAD'))[0] -eq (Git $publisher @('rev-parse','HEAD'))[0]) 'Sync did not fast-forward'
    # Both sides change one key: stop before changing HEAD or local bytes.
    $incoming.settings[0].value='remote edit';Write-Config $publisher $incoming
    Git $publisher @('add','.') | Out-Null;Git $publisher @('commit','-m','conflict') | Out-Null;Git $publisher @('push') | Out-Null
    ($saved.settings | Where-Object {$_.key -eq 'Remote'}).value='local edit';Write-Config $repo $saved
    $before=[IO.File]::ReadAllText((Join-Path $repo 'config\nova.json'));$head=(Git $repo @('rev-parse','HEAD'))[0]
    Sync $false
    Check ((Git $repo @('rev-parse','HEAD'))[0] -eq $head -and [IO.File]::ReadAllText((Join-Path $repo 'config\nova.json')) -ceq $before) 'Conflict changed HEAD or local data'
    # An unrelated dirty file blocks Git: restore the saved config on failure.
    ($saved.settings | Where-Object {$_.key -eq 'Remote'}).value='saved';Write-Config $repo $saved
    Set-Content -LiteralPath (Join-Path $publisher 'other.txt') -Value 'remote' -Encoding ASCII
    Git $publisher @('add','.') | Out-Null;Git $publisher @('commit','-m','other update') | Out-Null;Git $publisher @('push') | Out-Null
    Set-Content -LiteralPath (Join-Path $repo 'other.txt') -Value 'local' -Encoding ASCII
    $before=[IO.File]::ReadAllText((Join-Path $repo 'config\nova.json'))
    Sync $false
    Check ([IO.File]::ReadAllText((Join-Path $repo 'config\nova.json')) -ceq $before) 'Failed pull lost local configuration'
    Git $repo @('restore','--','other.txt') | Out-Null
    Git $repo @('add','config/nova.json') | Out-Null;Sync $false
    Check ((Git $repo @('diff','--cached','--name-only'))[0] -eq 'config/nova.json') 'Staged config was modified'
    Write-Host 'PASS config sync: preview, dirty config, independent additions, conflict refusal, failed-pull restoration and staged protection'
} finally {
    $resolved=[IO.Path]::GetFullPath($fixtureRoot)
    Check ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)) 'Unsafe fixture cleanup path'
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
