function Test-NovaToolchainBin([string]$Directory) {
    if ([string]::IsNullOrWhiteSpace($Directory)) { return $false }
    try {
        return (Test-Path -LiteralPath ([IO.Path]::Combine($Directory, 'gcc.exe')) -PathType Leaf -ErrorAction SilentlyContinue) -and
            (Test-Path -LiteralPath ([IO.Path]::Combine($Directory, 'windres.exe')) -PathType Leaf -ErrorAction SilentlyContinue)
    }
    catch { return $false }
}

function Get-NovaMsysRoots {
    param([string[]]$DriveRoots)
    # Check common install locations, at most one directory below each local
    # drive root. Custom/deeper installations can use NOVA_MINGW_BIN.
    if (-not $PSBoundParameters.ContainsKey('DriveRoots')) {
        $DriveRoots = @([IO.DriveInfo]::GetDrives() |
            Where-Object { $_.IsReady -and $_.DriveType -eq [IO.DriveType]::Fixed } |
            ForEach-Object { $_.RootDirectory.FullName })
    }
    foreach ($driveRoot in $DriveRoots) {
        foreach ($pattern in @('msys64', 'msys2', '*\msys64', '*\msys2')) {
            Get-Item -Path (Join-Path $driveRoot $pattern) -ErrorAction SilentlyContinue |
                Where-Object { $_.PSIsContainer } |
                ForEach-Object { $_.FullName }
        }
    }
    if ($env:LOCALAPPDATA) {
        foreach ($name in @('msys64', 'msys2')) {
            Join-Path $env:LOCALAPPDATA $name
            Join-Path $env:LOCALAPPDATA "Programs\$name"
        }
    }
}

function Resolve-NovaToolchainBin {
    param(
        [string]$PreferredBin = $env:NOVA_MINGW_BIN,
        [string]$SearchPath = $env:PATH,
        [string[]]$InstallRoots
    )
    if (-not [string]::IsNullOrWhiteSpace($PreferredBin)) {
        $preferred = $PreferredBin.Trim().Trim('"')
        if (-not (Test-NovaToolchainBin $preferred)) {
            throw 'NOVA_MINGW_BIN must contain gcc.exe and windres.exe / NOVA_MINGW_BIN 指定的目录必须同时包含 gcc.exe 和 windres.exe。'
        }
        return [IO.Path]::GetFullPath($preferred)
    }
    # Keep an explicitly configured PATH toolchain ahead of auto-detection.
    # Both tools must come from the same directory.
    foreach ($entry in ($SearchPath -split ';')) {
        $directory = $entry.Trim().Trim('"')
        if (Test-NovaToolchainBin $directory) { return [IO.Path]::GetFullPath($directory) }
    }
    if (-not $PSBoundParameters.ContainsKey('InstallRoots')) { $InstallRoots = @(Get-NovaMsysRoots) }
    foreach ($root in $InstallRoots) {
        foreach ($prefix in @('mingw64', 'ucrt64')) {
            $directory = Join-Path $root "$prefix\bin"
            if (Test-NovaToolchainBin $directory) { return [IO.Path]::GetFullPath($directory) }
        }
    }
    throw 'MinGW-w64 GCC/windres not found. Install MSYS2/MinGW-w64 or set NOVA_MINGW_BIN to its bin directory / 未找到 MinGW-w64 GCC/windres；请安装 MSYS2/MinGW-w64，或将 NOVA_MINGW_BIN 设为工具链的 bin 目录。'
}

function Initialize-NovaToolchain {
    $directory = Resolve-NovaToolchainBin
    $env:PATH = "$directory;$env:PATH"
}

# The batch bridge needs only the resolved directory on stdout. Dot-sourcing
# exposes the same resolver to PowerShell deployment and isolated tests.
if ($MyInvocation.InvocationName -ne '.') {
    try { Resolve-NovaToolchainBin }
    catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
}
