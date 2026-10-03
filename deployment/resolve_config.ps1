[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'configuration_merge.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$unmerged = @(& git -C $repoRoot ls-files -u -- config/nova.json)
if ($LASTEXITCODE) { throw 'Cannot inspect Git conflict state / 无法检查 Git 冲突状态。' }
if (-not $unmerged.Count) { Write-Host 'No unresolved config conflict; nothing changed / 当前配置没有未解决冲突，未修改文件。'; exit 0 }
function Read-GitConfigStage([int]$Stage) {
    $start = [Diagnostics.ProcessStartInfo]::new('git',('-C "' + $repoRoot + '" show :' + $Stage + ':config/nova.json'))
    $start.UseShellExecute=$false; $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true; $start.CreateNoWindow=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false,$true)
    $process=[Diagnostics.Process]::Start($start)
    try { $text=$process.StandardOutput.ReadToEnd(); $errorText=$process.StandardError.ReadToEnd(); $process.WaitForExit(); if($process.ExitCode){throw $errorText}; return $text }
    finally { $process.Dispose() }
}
$backup = Join-Path $repoRoot ('build\config-merge\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$base = Read-GitConfigStage 1; $local = Read-GitConfigStage 2; $remote = Read-GitConfigStage 3
foreach ($entry in @(@('base',$base),@('local',$local),@('remote',$remote))) { [IO.File]::WriteAllText((Join-Path $backup ($entry[0]+'.json')),$entry[1],[Text.UTF8Encoding]::new($false)) }
$configPath=Join-Path $repoRoot 'config\nova.json'
Copy-Item -LiteralPath $configPath -Destination (Join-Path $backup 'working.txt')
$localData=$local | ConvertFrom-Json
# A running old version may have saved valid local data over the conflict text.
try { $working=Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json; $localData=$working } catch { }
$result=Get-NovaMergedConfiguration ($base | ConvertFrom-Json) $localData ($remote | ConvertFrom-Json)
if($result.Conflicts.Count){Write-Host "Manual review required / 需要人工确认：$($result.Conflicts -join ', ')";Write-Host "Backups: $backup";exit 2}
$preview=Join-Path $backup 'merged.json'; $json=ConvertTo-Json -InputObject $result.Data -Depth 100
[IO.File]::WriteAllText($preview,$json+"`n",[Text.UTF8Encoding]::new($false))
Write-Host "Merged JSON preview / 合并预览：$preview"
$counter=@($result.Data.settings | Where-Object {$_.scope -eq 'batch_tasks' -and $_.section -eq 'Collection' -and $_.key -eq 'Count'})
if($counter.Count){Write-Host "Tasks / 任务：$($counter[0].value)"}
if($Apply){
    if(Get-Process -Name 'nova-desktop*' -ErrorAction SilentlyContinue){throw 'Close NOVA before applying the merge / 应用合并前请先关闭 NOVA。'}
    $temporary=Join-Path $repoRoot ('config\.nova-merge-'+[guid]::NewGuid().ToString('N')+'.tmp')
    try { Copy-Item -LiteralPath $preview -Destination $temporary; [IO.File]::Replace($temporary,$configPath,(Join-Path $backup 'replaced.json')) }
    finally { if(Test-Path -LiteralPath $temporary){[IO.File]::Delete($temporary)} }
    & git -C $repoRoot add -- config/nova.json
    if($LASTEXITCODE){throw 'Could not stage merged JSON / 无法暂存合并后的 JSON。'}
    Write-Host 'Conflict resolved and staged; commit the merge before pushing / 冲突已解决并暂存；提交合并后再推送。'
}
