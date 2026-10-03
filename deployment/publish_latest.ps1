[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Executable)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageRoot = Join-Path $repoRoot 'build\packages'
$latest = Join-Path $packageRoot 'nova-desktop.exe'
$source = [IO.Path]::GetFullPath($Executable)
$temporary = Join-Path $packageRoot ('.nova-latest-' + [guid]::NewGuid().ToString('N') + '.tmp')
$previous = Join-Path $packageRoot ('.nova-previous-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -eq 0) {
        throw 'No valid executable to publish / 没有可更新的有效程序。'
    }
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
    $latestConfig = Join-Path $packageRoot 'config'
    New-Item -ItemType Directory -Path $latestConfig -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot 'config\nova.json') -Destination (Join-Path $latestConfig 'nova.json')
    if ($source -ne [IO.Path]::GetFullPath($latest)) {
        Copy-Item -LiteralPath $source -Destination $temporary
        if (Test-Path -LiteralPath $latest) { [IO.File]::Replace($temporary, $latest, $previous) }
        else { [IO.File]::Move($temporary, $latest) }
    }
    Write-Host "Latest EXE: $latest"
}
catch {
    Write-Host ('Cannot update latest executable; close it if running. The previous copy is preserved / 无法更新最新程序；如正在运行，请关闭后重试。保留原副本。' + "`n" + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
finally {
    if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) }
    if (Test-Path -LiteralPath $previous) { [IO.File]::Delete($previous) }
}
