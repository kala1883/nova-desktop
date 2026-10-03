# Read-only diagnostics: never print saved names, commands or working folders.
[CmdletBinding()]
param([Alias('Path')][string]$ConfigurationPath = '')
function Get-NovaConfigurationInfo {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Configuration missing / 配置文件不存在：$resolved"
    }
    $bytes = [IO.File]::ReadAllBytes($resolved)
    if ($bytes.Length -gt 16MB) { throw 'Configuration exceeds 16 MiB / 配置超过 16 MiB。' }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes).TrimStart([char]0xfeff)
    $data = $text | ConvertFrom-Json
    if ($data.application -ne 'NOVA Desktop' -or $data.version -ne 1) {
        throw 'Unsupported configuration / 配置格式或版本不受支持。'
    }
    $taskCount = 0
    $counter = @($data.settings | Where-Object { $_.scope -eq 'batch_tasks' -and $_.section -eq 'Collection' -and $_.key -eq 'Count' })
    if ($counter.Count -gt 1 -or ($counter.Count -eq 1 -and (-not [int]::TryParse([string]$counter[0].value, [ref]$taskCount) -or $taskCount -lt 0 -or $taskCount -gt 16))) {
        throw 'Invalid task count / 任务数量无效。'
    }
    $tabs = @()
    for ($pane = 0; $pane -lt 4; $pane++) {
        $count = @($data.settings | Where-Object { $_.scope -eq 'files' -and $_.section -eq "Pane$pane" -and $_.key -eq 'Count' })
        $tabs += $(if ($count.Count -eq 1) { [string]$count[0].value } else { '-' })
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') }
    finally { $sha.Dispose() }
    [pscustomobject]@{ Path = $resolved; Hash = $hash; Initialized = $data.initialized; Tasks = $taskCount; PaneTabs = $tabs }
}

function Show-NovaConfigurationInfo {
    param([Parameter(Mandatory = $true)]$Info)
    Write-Host "JSON: $($Info.Path)"
    Write-Host "SHA256: $($Info.Hash)"
    Write-Host "Initialized / 已初始化：$($Info.Initialized); Tasks / 任务：$($Info.Tasks); Pane tabs / 窗格标签：$($Info.PaneTabs -join ', ')"
}

# Dot-sourcing loads reusable functions; direct execution displays diagnostics.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    if ([string]::IsNullOrWhiteSpace($ConfigurationPath)) { $ConfigurationPath = Join-Path (Split-Path -Parent $PSScriptRoot) 'config\nova.json' }
    Show-NovaConfigurationInfo -Info (Get-NovaConfigurationInfo -Path $ConfigurationPath)
}
