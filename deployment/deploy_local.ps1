[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Message = ('chore: local deployment ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$pushed = $false

function Invoke-Git {
    param([string[]]$Arguments)
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed / Git 执行失败：git $($Arguments -join ' ')" }
}

Push-Location -LiteralPath $repoRoot
try {
    foreach ($tool in @('git', 'gcc', 'windres')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool / 缺少工具：$tool" }
    }
    if ([string]::IsNullOrWhiteSpace($Message)) { throw 'Commit message cannot be empty / 提交说明不能为空。' }
    $gitRoot = Invoke-Git -Arguments @('rev-parse', '--show-toplevel')
    if ([IO.Path]::GetFullPath($gitRoot) -ne [IO.Path]::GetFullPath($repoRoot)) {
        throw 'Run in the NOVA repository / 脚本必须位于 NOVA 仓库中。'
    }
    $branch = Invoke-Git -Arguments @('symbolic-ref', '--quiet', '--short', 'HEAD')
    $remote = & git config --get "branch.$branch.remote"
    if ($LASTEXITCODE -eq 1) { $remote = 'origin' }
    elseif ($LASTEXITCODE -ne 0) { throw 'Cannot read remote / 无法读取远程配置。' }
    $targetRef = & git config --get "branch.$branch.merge"
    if ($LASTEXITCODE -eq 1) { $targetRef = "refs/heads/$branch" }
    elseif ($LASTEXITCODE -ne 0) { throw 'Cannot read upstream branch / 无法读取上游分支。' }
    if ($remote -eq '.' -or -not $targetRef.StartsWith('refs/heads/')) {
        throw 'A remote branch upstream is required / 上游必须是远程分支。'
    }
    Invoke-Git -Arguments @('remote', 'get-url', $remote) | Out-Null
    & git check-ignore --quiet build/.nova-deploy-check
    if ($LASTEXITCODE -ne 0) { throw 'build must be ignored by Git / build 目录必须被 Git 忽略。' }

    Write-Host '[1/4] Commit source changes / 提交源码改动'
    Invoke-Git -Arguments @('add', '--all')
    Invoke-Git -Arguments @('diff', '--cached', '--check')
    & git diff --cached --quiet
    $diffResult = $LASTEXITCODE
    if ($diffResult -eq 1) { Invoke-Git -Arguments @('commit', '-m', $Message) }
    elseif ($diffResult -eq 0) { Write-Host 'No changes; keep existing commit / 没有改动，保留当前提交。' }
    else { throw 'Cannot inspect staged changes / 无法检查暂存区。' }

    Write-Host "[2/4] Push / 推送：$remote $targetRef"
    Invoke-Git -Arguments @('push', '--set-upstream', $remote, "HEAD:$targetRef")
    $pushed = $true
    $revision = Invoke-Git -Arguments @('rev-parse', 'HEAD')
    $packageName = 'nova-desktop-' + $revision.Substring(0, 12) + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    $packageDir = Join-Path $repoRoot "build\packages\$packageName"
    $exe = Join-Path $packageDir 'nova-desktop.exe'
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

    Write-Host '[3/4] Build executable / 编译可执行文件'
    & (Join-Path $PSScriptRoot 'build.bat') $exe
    if ($LASTEXITCODE -ne 0) { throw 'Build failed; no package created / 编译失败，未生成压缩包。' }
    if (-not (Test-Path -LiteralPath $exe) -or (Get-Item -LiteralPath $exe).Length -eq 0) {
        throw 'Build produced no executable / 编译未生成有效文件。'
    }
    if ((Invoke-Git -Arguments @('rev-parse', 'HEAD')) -ne $revision -or (Invoke-Git -Arguments @('status', '--porcelain'))) {
        throw 'Source changed during build; package aborted / 构建期间源码发生变化，停止打包。'
    }

    Write-Host '[4/4] Package executable and notices / 打包可执行文件及版权说明'
    foreach ($name in @('LICENSE', 'NOTICE.md', 'NOTICE.zh-CN.md', 'README.md', 'README.zh-CN.md')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $packageDir
    }
    @("Commit: $revision", "Built (UTC): $([DateTime]::UtcNow.ToString('o'))") |
        Set-Content -LiteralPath (Join-Path $packageDir 'revision.txt') -Encoding UTF8
    $zip = "$packageDir.zip"
    $packageFiles = @(Get-ChildItem -LiteralPath $packageDir -File | ForEach-Object { $_.FullName })
    Compress-Archive -LiteralPath $packageFiles -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "EXE: $exe"
    Write-Host "ZIP: $zip"
    exit 0
}
catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if ($pushed) { Write-Host 'Source was already pushed; no rollback was attempted / 源码已推送，未自动回滚。' }
    exit 1
}
finally {
    Pop-Location
}
