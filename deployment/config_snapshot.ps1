[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Migrate','Import','Export')][string]$Action,
    [switch]$Preview,
    [string]$Executable = '',
    [string]$ConfigurationDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'configuration_merge.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Executable) {
    $Executable = Join-Path $repoRoot 'nova-desktop.exe'
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { $Executable = Join-Path $repoRoot 'build\nova-desktop.exe' }
}
if (-not $ConfigurationDirectory) { $ConfigurationDirectory = Join-Path $repoRoot 'config' }
$Executable = [IO.Path]::GetFullPath($Executable)
$ConfigurationDirectory = [IO.Path]::GetFullPath($ConfigurationDirectory)
$encoding = [Text.UTF8Encoding]::new($false,$true)
if ($Action -eq 'Migrate' -and -not $Preview) {
    $ancestor = Split-Path -Parent $Executable
    $resolvedConfiguration = Join-Path $ancestor 'config'
    while ($ancestor) {
        if ((Test-Path -LiteralPath (Join-Path $ancestor '.git')) -and (Test-Path -LiteralPath (Join-Path $ancestor 'config\nova.json'))) {
            $resolvedConfiguration=Join-Path $ancestor 'config'; break
        }
        $ancestor=Split-Path -Parent $ancestor
    }
    if ([IO.Path]::GetFullPath($resolvedConfiguration) -ne $ConfigurationDirectory) { throw 'Executable/configuration mismatch / 程序与配置目录不匹配。' }
    $start = [Diagnostics.ProcessStartInfo]::new($Executable,'--migrate-data')
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.WindowStyle='Hidden'
    $process=[Diagnostics.Process]::Start($start)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -eq 5) { throw 'Close NOVA first / 请先关闭 NOVA。' }
        if ($process.ExitCode) { throw 'Migration failed; original snapshot retained / 迁移失败，原快照保留。' }
    }
    finally { $process.Dispose() }
}
$created = $false
$guard = [Threading.Mutex]::new($false,'NOVA_DESKTOP_SINGLE_INSTANCE',[ref]$created)
$publications = @()
try {
    if (-not $created) { throw 'Close NOVA before exchanging snapshots / 请先关闭 NOVA，再导入或导出配置。' }
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Build NOVA first / 请先编译 NOVA。' }
    $paths = @{}
    $originals = @{}
    foreach ($name in @('runtime.json','runtime.base.json','nova.json','local.json')) {
        $paths[$name] = Join-Path $ConfigurationDirectory $name
        if (Test-Path -LiteralPath $paths[$name] -PathType Leaf) {
            $originals[$name] = [IO.File]::ReadAllBytes($paths[$name])
            if ($originals[$name].Length -gt 16MB) { throw 'Configuration exceeds 16 MiB / 配置超过 16 MiB。' }
        } elseif ($name -ne 'local.json') { throw 'First launch NOVA or run --migrate-data / 请先启动新版 NOVA 或运行 --migrate-data 完成迁移。' }
    }
    $backup = Join-Path $ConfigurationDirectory ('.snapshot-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup | Out-Null
    foreach ($name in $originals.Keys) { [IO.File]::WriteAllBytes((Join-Path $backup $name),$originals[$name]) }
    Write-Host "Backups / 备份：$backup"
    function Read-ValidatedSnapshot([string]$Name) {
        $directory = Join-Path $backup ('validate-' + $Name)
        New-Item -ItemType Directory -Path $directory | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $directory 'nova.json'),$originals[$Name])
        # The native loader checks raw JSON (including duplicate keys), all
        # domain limits, foreign keys and legacy task migrations before PS reads it.
        $start = [Diagnostics.ProcessStartInfo]::new($Executable,('--check-data "' + $directory + '"'))
        $start.UseShellExecute = $false; $start.CreateNoWindow = $true; $start.WindowStyle = 'Hidden'
        $process = [Diagnostics.Process]::Start($start)
        try { $process.WaitForExit(); if ($process.ExitCode) { throw "Invalid snapshot / 配置校验失败：$Name" } }
        finally { $process.Dispose() }
        return ($encoding.GetString([IO.File]::ReadAllBytes((Join-Path $directory 'runtime.json'))) | ConvertFrom-Json)
    }
    $base = Read-ValidatedSnapshot 'runtime.base.json'
    $local = Read-ValidatedSnapshot 'runtime.json'
    $migration = Join-Path $ConfigurationDirectory 'nova.migration.json'
    $seed = Join-Path $ConfigurationDirectory 'runtime.seed.json'
    if ($Action -eq 'Migrate') {
        # Only an untouched migration source may be cleaned. Newly edited/exported
        # snapshots require explicit Import/Export instead of being discarded.
        $snapshotRepo=Split-Path -Parent $ConfigurationDirectory
        $gitRoot=& git -C $snapshotRepo rev-parse --show-toplevel
        if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($gitRoot) -ne $snapshotRepo) { throw 'Migration requires the repository config directory / 迁移清理需要仓库的 config 目录。' }
        & git -C $snapshotRepo diff --cached --quiet -- config/nova.json
        if ($LASTEXITCODE -ne 0) { throw 'Unstage configuration first / 请先取消配置暂存。' }
        $unmerged=@(& git -C $snapshotRepo ls-files -u -- config/nova.json)
        if ($LASTEXITCODE -ne 0 -or $unmerged.Count) { throw 'Resolve Git conflicts first / 请先解决 Git 冲突。' }
        $headRevision=& git -C $snapshotRepo rev-parse HEAD
        if ($LASTEXITCODE) { throw 'Cannot read HEAD / 无法读取 HEAD。' }
        $start=[Diagnostics.ProcessStartInfo]::new('git',('-C "'+$snapshotRepo+'" show HEAD:config/nova.json'))
        $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.RedirectStandardOutput=$true
        $process=[Diagnostics.Process]::Start($start); $memory=[IO.MemoryStream]::new()
        try { $process.StandardOutput.BaseStream.CopyTo($memory); $process.WaitForExit(); if ($process.ExitCode) { throw 'Cannot read Git snapshot / 无法读取 Git 快照。' }; $headBytes=$memory.ToArray() }
        finally { $memory.Dispose(); $process.Dispose() }
        if ([Convert]::ToBase64String($headBytes) -cne [Convert]::ToBase64String($originals['nova.json']) -and
            (-not (Test-Path -LiteralPath $migration -PathType Leaf) -or
             [Convert]::ToBase64String([IO.File]::ReadAllBytes($migration)) -cne [Convert]::ToBase64String($originals['nova.json']))) {
            throw 'Snapshot changed since migration; use Import/Export / 共享快照在迁移后已修改，请使用 Import/Export。'
        }
        $originals['head.json']=$headBytes
        $remote=Read-ValidatedSnapshot 'head.json'
        $merged=[pscustomobject]@{Data=$local;Conflicts=@()}
    } elseif ((Test-Path -LiteralPath $migration -PathType Leaf) -and (Test-Path -LiteralPath $seed -PathType Leaf) -and
        [Convert]::ToBase64String([IO.File]::ReadAllBytes($migration)) -ceq [Convert]::ToBase64String($originals['nova.json']) -and
        (Get-NovaCanonicalValue $base) -ceq (Get-NovaCanonicalValue ($encoding.GetString([IO.File]::ReadAllBytes($seed)) | ConvertFrom-Json))) {
        # The original v1 task migration may allocate IDs. Reuse its validated
        # baseline when the shared file is still the exact original snapshot.
        $remote = $base
    } else { $remote = Read-ValidatedSnapshot 'nova.json' }
    if ($Action -ne 'Migrate') { $merged = Get-NovaMergedConfiguration $base $local $remote }
    if ($merged.Conflicts.Count) { throw ('Review conflicts; data unchanged / 请核对冲突，数据未修改：' + ($merged.Conflicts -join ', ')) }
    $mergedText = (ConvertTo-Json -InputObject $merged.Data -Depth 100) + "`n"
    [IO.File]::WriteAllText((Join-Path $backup 'merged.json'),$mergedText,$encoding)
    # Validate the merged relationships and capacities too, not only each input.
    $originals['candidate.json'] = $encoding.GetBytes($mergedText)
    $null = Read-ValidatedSnapshot 'candidate.json'
    if ($Preview) { Write-Host 'Preview only; no configuration changed / 仅预览，配置未修改。'; exit 0 }
    # Fail if any producer changed the inputs while validation was running.
    foreach ($name in $paths.Keys) {
        $exists = Test-Path -LiteralPath $paths[$name] -PathType Leaf
        if ($exists -ne $originals.ContainsKey($name) -or ($exists -and
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($paths[$name])) -cne [Convert]::ToBase64String($originals[$name]))) {
            throw 'Configuration changed; retry / 配置已改变，请重试。'
        }
    }
    $updates = [ordered]@{'runtime.json'=$mergedText}
    if ($Action -eq 'Migrate' -and (& git -C $snapshotRepo rev-parse HEAD) -cne $headRevision) { throw 'HEAD changed; retry / HEAD 已改变，请重试。' }
    if ($Action -eq 'Export') { $updates['nova.json']=$mergedText; $updates['runtime.base.json']=$mergedText }
    else { $updates['runtime.base.json']=(ConvertTo-Json -InputObject $remote -Depth 100)+"`n" }
    if ($Action -eq 'Migrate') { $updates['nova.json']=$encoding.GetString($headBytes) }
    # Prepare every write before publishing; rollback exact bytes on a failure.
    foreach ($name in $updates.Keys) {
        $temporary = Join-Path $ConfigurationDirectory ('.nova-snapshot-' + [guid]::NewGuid().ToString('N') + '.tmp')
        $publications += [pscustomobject]@{Name=$name;Temporary=$temporary;Published=$false;Previous=(Join-Path $backup ($name+'.previous'))}
        $stream = [IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $bytes=$encoding.GetBytes($updates[$name]); $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) }
        finally { $stream.Dispose() }
    }
    try {
        foreach ($publication in $publications) {
            [IO.File]::Replace($publication.Temporary,$paths[$publication.Name],$publication.Previous)
            $publication.Published=$true
        }
    } catch {
        for ($index=$publications.Count-1; $index -ge 0; $index--) {
            $publication=$publications[$index]
            if ($publication.Published) { [IO.File]::Replace($publication.Previous,$paths[$publication.Name],($publication.Previous+'.failed')) }
        }
        throw
    }
    if ($Action -eq 'Migrate') {
        # ReplaceFile may preserve a stale Windows stat-cache entry. Verify the
        # exact Git blob before refreshing the identical index entry. This
        # cannot stage a content edit. Leave other Git clients' locks intact.
        $actual=& git -C $snapshotRepo hash-object --path=config/nova.json config/nova.json
        $expected=& git -C $snapshotRepo rev-parse HEAD:config/nova.json
        if ($actual -cne $expected) { throw 'Git snapshot verification failed; backups retained / Git 快照校验失败，备份已保留。' }
        for ($attempt=0; $attempt -lt 5; $attempt++) {
            & git -C $snapshotRepo add -- config/nova.json
            if (-not $LASTEXITCODE) { break }
            Start-Sleep -Milliseconds 200
        }
        if ($LASTEXITCODE) { throw 'Git is busy; data migrated and backups retained / Git 正忙；数据已迁移，备份已保留。' }
    }
    Write-Host 'Snapshot exchanged; no Git commit or push performed / 配置交换完成，未执行 Git 提交或推送。'
} catch { Write-Host ("Snapshot exchange failed / 配置交换失败：`n"+$_.Exception.Message) -ForegroundColor Red; exit 1 }
finally {
    foreach ($publication in $publications) { if (Test-Path -LiteralPath $publication.Temporary) { [IO.File]::Delete($publication.Temporary) } }
    $guard.Dispose()
}
