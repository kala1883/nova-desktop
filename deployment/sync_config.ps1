[CmdletBinding()]
param([switch]$Preview)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'configuration_merge.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot 'config\nova.json'
function Invoke-NovaGit([string[]]$Arguments) {
    $output = @(& git -C $repoRoot @Arguments)
    if ($LASTEXITCODE) { throw 'Git operation failed; backups retained / Git 操作失败，备份已保留。' }
    return ,$output
}
function Read-NovaGitConfig([string]$Revision) {
    # Revisions here are verified hexadecimal object IDs, never user text.
    if ($Revision -notmatch '^[0-9a-f]{40,64}$') { throw 'Invalid revision / 无效的提交。' }
    $start = [Diagnostics.ProcessStartInfo]::new('git',('-C "' + $repoRoot + '" show ' + $Revision + ':config/nova.json'))
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false,$true)
    $process = [Diagnostics.Process]::Start($start)
    try {
        $text = $process.StandardOutput.ReadToEnd(); $errorText = $process.StandardError.ReadToEnd()
        $process.WaitForExit(); if ($process.ExitCode) { throw $errorText }; return $text
    } finally { $process.Dispose() }
}
if ((Invoke-NovaGit @('branch','--show-current'))[0] -cne 'main') { throw 'Run on main / 请在 main 分支运行。' }
if (-not $Preview -and (Get-Process -Name 'nova-desktop*' -ErrorAction SilentlyContinue)) { throw 'Close NOVA before syncing / 同步前请先关闭 NOVA。' }
if ((Invoke-NovaGit @('ls-files','-u')).Count) { throw 'Resolve existing Git conflicts first / 请先解决已有 Git 冲突。' }
& git -C $repoRoot diff --cached --quiet -- config/nova.json
if ($LASTEXITCODE -ne 0) { throw 'Commit or unstage the staged configuration first / 请先提交或取消暂存配置。' }
foreach ($state in @('MERGE_HEAD','rebase-merge','rebase-apply','CHERRY_PICK_HEAD','REVERT_HEAD')) {
    $statePath = (Invoke-NovaGit @('rev-parse','--git-path',$state))[0]
    if (-not [IO.Path]::IsPathRooted($statePath)) { $statePath = Join-Path $repoRoot $statePath }
    if (Test-Path -LiteralPath $statePath) { throw 'Finish the current Git operation first / 请先完成当前 Git 操作。' }
}
$head = (Invoke-NovaGit @('rev-parse','HEAD'))[0]
Invoke-NovaGit @('fetch','origin','main') | Out-Host
$remote = (Invoke-NovaGit @('rev-parse','FETCH_HEAD'))[0]
& git -C $repoRoot merge-base --is-ancestor $head $remote
if ($LASTEXITCODE -ne 0) { throw 'Fast-forward required; local commits need a separate merge / 需要快进更新，本地提交分叉请另行合并。' }
$backup = Join-Path $repoRoot ('build\config-sync\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$original = [IO.File]::ReadAllBytes($configPath)
[IO.File]::WriteAllBytes((Join-Path $backup 'local.json'),$original)
$encoding = [Text.UTF8Encoding]::new($false,$true)
$baseText = Read-NovaGitConfig $head; $remoteText = Read-NovaGitConfig $remote
[IO.File]::WriteAllText((Join-Path $backup 'base.json'),$baseText,$encoding)
[IO.File]::WriteAllText((Join-Path $backup 'remote.json'),$remoteText,$encoding)
$base = $baseText | ConvertFrom-Json; $remoteData = $remoteText | ConvertFrom-Json
$local = $encoding.GetString($original).TrimStart([char]0xfeff) | ConvertFrom-Json
$merged = Get-NovaMergedConfiguration $base $local $remoteData
Write-Host "Backups / 备份：$backup"
if ($merged.Conflicts.Count) {
    Write-Host "Manual review required; working files unchanged / 需要人工核对，工作文件未修改：$($merged.Conflicts -join ', ')"
    exit 2
}
$mergedText = (ConvertTo-Json -InputObject $merged.Data -Depth 100) + "`n"
# If only serialization/stale-key differences remain, use Git's exact bytes.
$normalizedRemote = Get-NovaMergedConfiguration $remoteData $remoteData $remoteData
if ((Get-NovaCanonicalValue $merged.Data) -ceq (Get-NovaCanonicalValue $normalizedRemote.Data)) { $mergedText = $remoteText }
$previewPath = Join-Path $backup 'merged.json'
[IO.File]::WriteAllText($previewPath,$mergedText,$encoding)
Write-Host "Merged preview / 合并预览：$previewPath"
if ($Preview) { exit 0 }
if (Get-Process -Name 'nova-desktop*' -ErrorAction SilentlyContinue) { throw 'Close NOVA before syncing / 同步前请先关闭 NOVA。' }
if ((Invoke-NovaGit @('rev-parse','HEAD'))[0] -cne $head -or
    [Convert]::ToBase64String([IO.File]::ReadAllBytes($configPath)) -cne [Convert]::ToBase64String($original)) {
    throw 'Configuration or HEAD changed; retry / 配置或 HEAD 已改变，请重新运行。'
}
$temporary = Join-Path $repoRoot ('config\.nova-sync-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    [IO.File]::WriteAllText($temporary,$mergedText,$encoding)
    # The preflight verifies that the index equals HEAD for this path.
    # Force materialization even when a rapid same-sized edit has cached stat data.
    Invoke-NovaGit @('checkout-index','--force','--','config/nova.json') | Out-Host
    try {
        # Refresh Git's cached stat after restoring a same-sized file rapidly.
        $restoredBlob = (Invoke-NovaGit @('hash-object','--path=config/nova.json','config/nova.json'))[0]
        $headBlob = (Invoke-NovaGit @('rev-parse','HEAD:config/nova.json'))[0]
        if ($restoredBlob -cne $headBlob) { throw 'Restored configuration differs from HEAD / 恢复的配置与 HEAD 不一致。' }
        Invoke-NovaGit @('add','--','config/nova.json') | Out-Host
        Invoke-NovaGit @('merge','--ff-only',$remote) | Out-Host
    }
    catch { [IO.File]::WriteAllBytes($configPath,$original); throw }
    [IO.File]::Replace($temporary,$configPath,(Join-Path $backup 'pulled.json'))
} finally { if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) } }
Write-Host 'Synced; local additions retained. Review config diff before committing / 同步完成，本机新增数据已保留；提交前核对配置差异。'
