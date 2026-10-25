[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Source, [switch]$ArchiveSource,
      [switch]$ControllerAlreadyStopped)
$ErrorActionPreference = 'Stop'
function Get-SyncMigrationHash([string]$Path) {
    $taskHashStream=[IO.File]::OpenRead($Path)
    $taskHasher=[Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToBase64String($taskHasher.ComputeHash($taskHashStream)) }
    finally { $taskHasher.Dispose();$taskHashStream.Dispose() }
}
$taskRepo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$taskSource = [IO.Path]::GetFullPath($Source).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $taskSource 'chunksync\client.py'))) { throw 'Not a ChunkSync project / 不是续桥项目。' }
if ((Get-Item -LiteralPath $taskSource).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked source is not allowed / 不允许链接源目录。' }
if ($taskRepo.StartsWith($taskSource + '\',[StringComparison]::OrdinalIgnoreCase) -or $taskSource.StartsWith($taskRepo + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Source and destination must be independent / 源与目标不可相互包含。' }
$taskConfigFile = Join-Path $taskSource 'runtime\app\config.json'
$taskOriginalBytes = [IO.File]::ReadAllBytes($taskConfigFile)
$taskConfig = [Text.Encoding]::UTF8.GetString($taskOriginalBytes).TrimStart([char]0xfeff) | ConvertFrom-Json
if ($taskConfig.folders.Count -gt 16) { throw 'Too many pairs; original configuration preserved / 配对超限，原配置保留。' }
$taskPrivate = Join-Path $taskRepo 'config\sync'
$taskArchiveRoot = Join-Path $taskRepo 'build\sync-migration'
if ((Test-Path -LiteralPath (Join-Path $taskPrivate 'client')) -or (Test-Path -LiteralPath (Join-Path $taskPrivate 'import.json'))) {
    throw 'Native state already exists; refusing to overwrite / 已有原生同步状态，拒绝覆盖。'
}
$taskArchive = [IO.Path]::GetFullPath((Join-Path $taskArchiveRoot ('archive\'+(Split-Path -Leaf $taskSource))))
if ($ArchiveSource -and (-not $taskArchive.StartsWith($taskRepo+'\build\sync-migration\archive\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $taskArchive))) {
    throw 'Archive target is invalid or occupied / 归档目标无效或已占用。'
}
$taskClientSource=Join-Path $taskSource 'runtime\app\client'
if (Test-Path -LiteralPath $taskClientSource) {
    $taskLinks=@(Get-ChildItem -LiteralPath $taskClientSource -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    if ((Get-Item -LiteralPath $taskClientSource).Attributes -band [IO.FileAttributes]::ReparsePoint -or $taskLinks.Count) { throw 'Linked cache is not allowed / 不允许链接缓存。' }
}
if (-not $ControllerAlreadyStopped) { try {
    $taskState = Invoke-RestMethod -Uri 'http://127.0.0.1:18765/api/state' -TimeoutSec 3
    if ($taskState.identity.node_id -ne $taskConfig.identity.node_id) { throw 'A different local controller owns this port / 该端口属于另一个控制器。' }
    foreach ($taskFolder in $taskState.folders) {
        Invoke-RestMethod -Uri 'http://127.0.0.1:18765/api/pause' -Method Post -ContentType 'application/json' -Headers @{'X-CSRF-Token'=$taskState.csrf} -Body (@{id=$taskFolder.id;paused=$true}|ConvertTo-Json -Compress) -TimeoutSec 5 | Out-Null
    }
    for ($taskAttempt=0;$taskAttempt -lt 45;$taskAttempt++) {
        $taskState=Invoke-RestMethod -Uri 'http://127.0.0.1:18765/api/state' -TimeoutSec 3
        if (-not $taskState.task -or $taskState.task.status -ne 'running') { break }
        Start-Sleep -Seconds 1
    }
    if ($taskState.task -and $taskState.task.status -eq 'running') { throw 'Controller has not paused / 旧控制器尚未暂停。' }
} catch [System.Net.WebException] {
    if ($_.Exception.Status -ne [System.Net.WebExceptionStatus]::ConnectFailure) { throw }
    # A refused connection is safe to migrate; the PID is still checked below.
} }
& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $taskSource 'scripts\Stop-Sync.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Old controller was not stopped / 旧控制器未停止。' }
New-Item -ItemType Directory -Path $taskPrivate -Force | Out-Null
New-Item -ItemType Directory -Path $taskArchiveRoot -Force | Out-Null
[IO.File]::WriteAllBytes((Join-Path $taskArchiveRoot 'original-config.json'),$taskOriginalBytes)
# Resolve metadata only. No private key, password or trust record is imported.
$taskAlias = [string]$taskConfig.connection.host
if ($taskAlias -match '^[A-Za-z0-9_][A-Za-z0-9_.@:-]{0,199}$') {
    $taskResolved=@{}
    & ssh -G $taskAlias 2>$null | ForEach-Object { if ($_ -match '^(hostname|user|port) (.+)$') { $taskResolved[$Matches[1]]=$Matches[2] } }
    $taskShared=Get-Content -LiteralPath (Join-Path $taskRepo 'config\nova.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $taskProfiles=@{}
    foreach($taskEntry in $taskShared.settings){if($taskEntry.scope -eq 'ssh_servers' -and $taskEntry.section -match '^Server[0-9]+$') { if(-not $taskProfiles.ContainsKey($taskEntry.section)){$taskProfiles[$taskEntry.section]=@{}}; $taskProfiles[$taskEntry.section][$taskEntry.key]=$taskEntry.value }}
    $taskMatch=@($taskProfiles.Values | Where-Object { $_.Host -eq $taskResolved.hostname -and $_.User -ceq $taskResolved.user -and $_.Port -eq $taskResolved.port })
    if($taskMatch.Count -eq 1){$taskConfig | Add-Member -NotePropertyName nova_server_id -NotePropertyValue ([string]$taskMatch[0].Id) -Force}
}
foreach($taskFolder in $taskConfig.folders){$taskFolder.paused=$true}
$taskStaging=Join-Path $taskPrivate ('.migration-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskStaging | Out-Null
$taskClientTarget=Join-Path $taskPrivate 'client'
if(Test-Path -LiteralPath $taskClientTarget){throw 'Native client state already exists; refusing to overwrite / 已有原生同步状态，拒绝覆盖。'}
if(Test-Path -LiteralPath $taskClientSource){
    Copy-Item -LiteralPath $taskClientSource -Destination (Join-Path $taskStaging 'client') -Recurse
    foreach($taskFile in Get-ChildItem -LiteralPath $taskClientSource -Recurse -File){
        $taskRelative=$taskFile.FullName.Substring($taskClientSource.Length+1)
        $taskCopy=Join-Path (Join-Path $taskStaging 'client') $taskRelative
        if((Get-SyncMigrationHash $taskFile.FullName) -cne (Get-SyncMigrationHash $taskCopy)){throw 'Cache verification failed / 缓存校验失败。'}
    }
    $taskValidated=[IO.Path]::GetFullPath((Join-Path $taskStaging 'client'))
    if(-not $taskValidated.StartsWith($taskPrivate+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid cache target'}
    Move-Item -LiteralPath $taskValidated -Destination $taskClientTarget
}
$taskImportTemp=Join-Path $taskStaging 'import.json'
$taskConfig | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $taskImportTemp -Encoding utf8
Move-Item -LiteralPath $taskImportTemp -Destination (Join-Path $taskPrivate 'import.json')
$taskArchive=$null
if($ArchiveSource){
    $taskArchiveParent=Join-Path $taskArchiveRoot 'archive'
    New-Item -ItemType Directory -Path $taskArchiveParent -Force | Out-Null
    $taskArchive=[IO.Path]::GetFullPath((Join-Path $taskArchiveParent (Split-Path -Leaf $taskSource)))
    if(-not $taskArchive.StartsWith($taskRepo+'\build\sync-migration\archive\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $taskArchive)){throw 'Archive target is invalid or occupied / 归档目标无效或已占用。'}
    # Exact absolute source and target were checked above; one-shell, same-volume move.
    Move-Item -LiteralPath $taskSource -Destination $taskArchive
}
@{Source=$taskSource;PrivateState=$taskPrivate;Archive=$taskArchive;DeviceIdentityPreserved=$true;Pairs=$taskConfig.folders.Count;Paused=$true;CredentialsCopied=$false} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskArchiveRoot 'migration.json') -Encoding utf8
Write-Output 'Identity, pairs and checkpoints staged; original project archived under ignored build; credentials untouched / 已迁移身份、目录与断点，登录凭据未复制。'
