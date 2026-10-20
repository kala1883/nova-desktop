[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Executable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'configuration_info.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $repoRoot 'build\packages'
$latest = Join-Path $packageRoot 'nova-desktop.exe'
$source = [IO.Path]::GetFullPath($Executable)
$temporary = Join-Path $packageRoot ('.nova-latest-' + [guid]::NewGuid().ToString('N') + '.tmp')
$previous = Join-Path $packageRoot ('.nova-previous-' + [guid]::NewGuid().ToString('N') + '.tmp')
function Remove-NovaPublicationTemporary([string]$Path) {
    # A running image may be renamed successfully but remain undeletable until
    # its process exits. Cleanup must not change the publication result.
    try { if (Test-Path -LiteralPath $Path -PathType Leaf) { [IO.File]::Delete($Path) } }
    catch {
        Write-Host ("Temporary cleanup deferred; file retained / 临时文件暂无法清理，已保留：$Path") -ForegroundColor Yellow
    }
}
function Remove-NovaPreviousExecutables {
    try { $files = @(Get-ChildItem -LiteralPath $packageRoot -Filter '.nova-previous-*.tmp' -File -ErrorAction Stop) }
    catch { return }
    foreach ($file in $files) {
        if ($file.Name -cmatch '^\.nova-previous-[0-9a-f]{32}\.tmp$') { Remove-NovaPublicationTemporary $file.FullName }
    }
}
try {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -eq 0) {
        throw 'No valid executable to publish / 没有可更新的有效程序。'
    }
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
    Remove-NovaPreviousExecutables
    Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt') -Destination $packageRoot
    $latestConfig = Join-Path $packageRoot 'config'
    New-Item -ItemType Directory -Path $latestConfig -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\nova.json') -Destination (Join-Path $latestConfig 'nova.json')
    if ($source -ne [IO.Path]::GetFullPath($latest)) {
        Copy-Item -LiteralPath $source -Destination $temporary
        if (Test-Path -LiteralPath $latest) { [IO.File]::Replace($temporary, $latest, $previous) }
        else { [IO.File]::Move($temporary, $latest) }
    }
    Write-Host "Latest EXE: $latest"
    Show-NovaConfigurationInfo -Info (Get-NovaConfigurationInfo -Path (Join-Path $repoRoot 'config\nova.json'))
}
catch {
    Write-Host ('Cannot update latest executable; close it if running. The previous copy is preserved / 无法更新最新程序；如正在运行，请关闭后重试。保留原副本。' + "`n" + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
finally {
    Remove-NovaPublicationTemporary $temporary
    Remove-NovaPublicationTemporary $previous
}
