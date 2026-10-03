$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '..\deployment\configuration_merge.ps1')
function Check($condition,[string]$message){if(-not $condition){throw $message}}
function New-UsageFixture([string[]]$Ids){
    $rows=@([pscustomobject]@{scope='batch_tasks';section='Collection';key='Version';value='3'},[pscustomobject]@{scope='batch_tasks';section='Collection';key='Count';value=[string]$Ids.Count})
    for($i=0;$i -lt $Ids.Count;$i++){
        foreach($pair in @(@('Id',$Ids[$i]),@('Name',"task $($Ids[$i])"),@('Command','echo fixture'),@('Mode','0'),@('DirectoryCount','0'))){$rows += [pscustomobject]@{scope='batch_tasks';section="Task$i";key=$pair[0];value=$pair[1]}}
    }
    [pscustomobject]@{version=1;application='NOVA Desktop';initialized=$true;settings=$rows;workspaces=@([pscustomobject]@{id='1';position=0;name='Files'});items=@()}
}
$base=New-UsageFixture @('10');$local=New-UsageFixture @('10','20');$remote=New-UsageFixture @('10','30')
$sourceBefore=Get-NovaCanonicalValue $local
$merged=Get-NovaMergedConfiguration $base $local $remote
Check (-not $merged.Conflicts.Count) 'Independent task additions should merge'
Check ((Get-NovaTasksMap $merged.Data).Count -eq 3) 'Task1 index collision lost a task'
Check ((Get-NovaTasksMap $merged.Data).ContainsKey('20') -and (Get-NovaTasksMap $merged.Data).ContainsKey('30')) 'Stable task IDs changed'
Check ((Get-NovaCanonicalValue $local) -eq $sourceBefore) 'Merge mutated an input'
$local=New-UsageFixture @('10');$remote=New-UsageFixture @('10')
($local.settings | Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Command'}).value='echo local'
($remote.settings | Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Command'}).value='echo remote'
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ($merged.Conflicts.Count -eq 1 -and $null -eq $merged.Data) 'Conflicting edits must not pick a side'
$local=New-UsageFixture @();$remote=New-UsageFixture @('10')
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ((Get-NovaTasksMap $merged.Data).Count -eq 0) 'A one-sided deletion was resurrected'
($remote.settings | Where-Object {$_.section -eq 'Task0' -and $_.key -eq 'Command'}).value='echo edit'
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ($merged.Conflicts.Count -eq 1) 'Delete-versus-edit conflict was ignored'
$local=New-UsageFixture @('10');$remote=New-UsageFixture @('10','9223372036854775807')
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ((Get-NovaTasksMap $merged.Data).ContainsKey('9223372036854775807')) '64-bit ID lost precision'
Write-Host 'PASS semantic JSON merge: independent additions, stable IDs, pure inputs, deletion, edit/delete conflicts and 64-bit precision'
