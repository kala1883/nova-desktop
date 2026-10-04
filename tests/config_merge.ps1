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

# Saving compacts numeric positions; remote deletion must still be recognized.
$base=New-UsageFixture @('10');$local=New-UsageFixture @('10');$remote=New-UsageFixture @('10')
$base.items=@(foreach($i in 1..3){[pscustomobject]@{id=[string]$i;workspace_id='1';position=$i*2;name="item $i";target="C:\fixture$i"}})
$local.items=@($base.items | Select-Object *);$remote.items=@($base.items | Where-Object {$_.id -ne '2'} | Select-Object *)
for($i=0;$i -lt 3;$i++){$local.items[$i].position=$i}
$merged=Get-NovaMergedConfiguration $base $local $remote
Check (-not $merged.Conflicts.Count -and $merged.Data.items.Count -eq 2) 'Position compaction blocked a remote deletion'
Check ($merged.Data.items[0].id -eq '1' -and $merged.Data.items[1].id -eq '3') 'Deletion changed relative order'
$local.items[1].name='local edit'
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ($merged.Conflicts.Count -eq 1) 'Real edit-versus-delete conflict was ignored'
$local.items=@($base.items | Select-Object *);$remote.items=@($base.items | Select-Object *)
$local.items[0].position=9
$merged=Get-NovaMergedConfiguration $base $local $remote
Check (-not $merged.Conflicts.Count -and $merged.Data.items[2].id -eq '1') 'One-sided reorder was lost'
$local.items[0].position=5;$remote.items[2].position=3
$merged=Get-NovaMergedConfiguration $base $local $remote
Check ($merged.Conflicts.Count -gt 0 -and $null -eq $merged.Data) 'Incompatible relative orders must require review'
$base=New-UsageFixture @('10');$local=New-UsageFixture @('10');$remote=New-UsageFixture @('10')
foreach($data in @($base,$local,$remote)){$data.settings+=@([pscustomobject]@{scope='files';section='Pane0';key='Count';value='1'},[pscustomobject]@{scope='files';section='Pane0';key='Tab0';value='C:\fixture'})}
$local.settings+=[pscustomobject]@{scope='files';section='Pane0';key='Tab1';value='C:\closed'}
($remote.settings | Where-Object {$_.key -eq 'Count' -and $_.scope -eq 'files'}).value='2'
$remote.settings+=[pscustomobject]@{scope='files';section='Pane0';key='Tab1';value='C:\remote'}
$merged=Get-NovaMergedConfiguration $base $local $remote
Check (-not $merged.Conflicts.Count -and ($merged.Data.settings | Where-Object {$_.key -eq 'Tab1'}).value -eq 'C:\remote') 'Closed tab residue blocked a remote addition'
Write-Host 'PASS position compaction, real edit/delete conflicts, relative reorder conflicts and closed tab residue'
