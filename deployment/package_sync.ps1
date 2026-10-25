function Copy-NovaSyncAssets {
    param([Parameter(Mandatory=$true)][string]$RepositoryRoot,
          [Parameter(Mandatory=$true)][string]$Destination)
    # Explicit source allowlist: never package machine identities, caches or logs.
    $assetPaths = @('sync/README.md','sync/PROTOCOL.md','sync/server/sync_server.py',
        'sync/server/chunksync/__init__.py','sync/server/chunksync/common.py',
        'sync/server/chunksync/wire.py','sync/server/chunksync/broker.py','sync/server/chunksync/control.py',
        'deployment/deploy-sync-server.ps1')
    foreach ($assetPath in $assetPaths) {
        $assetSource = Join-Path $RepositoryRoot $assetPath
        $assetTarget = Join-Path $Destination $assetPath
        New-Item -ItemType Directory -Path (Split-Path -Parent $assetTarget) -Force | Out-Null
        Copy-Item -LiteralPath $assetSource -Destination $assetTarget -Force
    }
}
