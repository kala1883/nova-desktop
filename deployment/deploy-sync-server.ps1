[CmdletBinding()]
param([string]$Server = 'huawei-ai', [string]$RemoteProject = '/opt/cloud-file-sync-research')
$ErrorActionPreference = 'Stop'
if ($Server -notmatch '^[A-Za-z0-9_][A-Za-z0-9_.@:-]{0,199}$' -or
    $RemoteProject -notmatch '^/[A-Za-z0-9_./-]+$' -or $RemoteProject.Split('/') -contains '..') {
    throw 'Invalid SSH alias or remote project path / SSH 别名或远端项目路径无效。'
}
$taskRepo = Split-Path -Parent $PSScriptRoot
$taskSource = Join-Path $taskRepo 'sync\server'
$taskStage = Join-Path $taskRepo 'build\sync-deployment'
New-Item -ItemType Directory -Path $taskStage -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$taskArchive = Join-Path $taskStage ('server-' + [guid]::NewGuid().ToString('N') + '.zip')
$taskFiles = @(Get-ChildItem -LiteralPath $taskSource -Recurse -File -Filter '*.py' | Where-Object { $_.FullName -notlike '*\__pycache__\*' })
$taskHashes = @{}
$taskZip = [IO.Compression.ZipFile]::Open($taskArchive, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($taskFile in $taskFiles) {
        $taskName = $taskFile.FullName.Substring($taskSource.Length + 1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskZip, $taskFile.FullName, $taskName) | Out-Null
        $taskHashes[$taskName] = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
} finally { $taskZip.Dispose() }
$taskHash = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant()
$taskOptions = @('-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','UpdateHostKeys=no','-o','ConnectTimeout=10','-o','ControlMaster=no','-o','ControlPath=none')
$taskPrepare = @"
from pathlib import Path
root=Path('$RemoteProject')
marker=root/'.chunksync-project.json'
if root.exists() and not marker.exists() and any(root.iterdir()):
    raise SystemExit('Refusing unknown nonempty project')
root.mkdir(parents=True,exist_ok=True)
root.chmod(0o700)
(root/'incoming').mkdir(exist_ok=True)
marker.write_text('{"project":"NOVA Desktop sync"}',encoding='utf-8')
"@
$taskPrepare | & ssh @taskOptions $Server 'python3 -B -'
if ($LASTEXITCODE -ne 0) { throw 'Remote preparation failed / 远端目录准备失败。' }
$taskRemoteZip = "$RemoteProject/incoming/$taskHash.zip"
& scp -q @taskOptions $taskArchive "${Server}:$taskRemoteZip"
if ($LASTEXITCODE -ne 0) { throw 'Source upload failed / 服务端源码上传失败。' }
$taskExpected = $taskHashes | ConvertTo-Json -Compress
$taskInstall = @"
from pathlib import Path
import hashlib,json,os,zipfile,uuid
root=Path('$RemoteProject').resolve()
archive=Path('$taskRemoteZip')
expected=json.loads('''$taskExpected''')
if hashlib.sha256(archive.read_bytes()).hexdigest()!='$taskHash':
    raise SystemExit('Archive checksum mismatch')
with zipfile.ZipFile(archive) as bundle:
    if set(bundle.namelist())!=set(expected):
        raise SystemExit('Unexpected archive entries')
    for name in expected:
        target=(root/name).resolve()
        if not target.is_relative_to(root):
            raise SystemExit('Source path escape')
        data=bundle.read(name)
        if hashlib.sha256(data).hexdigest()!=expected[name]:
            raise SystemExit('Code checksum mismatch')
        target.parent.mkdir(parents=True,exist_ok=True)
        temporary=target.with_name(target.name+'.'+uuid.uuid4().hex+'.tmp')
        with temporary.open('xb') as output:
            output.write(data);output.flush();os.fsync(output.fileno())
        os.replace(temporary,target)
(root/'reports').mkdir(exist_ok=True)
(root/'reports'/'nova-deployment.json').write_text(json.dumps({'archive_sha256':'$taskHash','files':expected},indent=2),encoding='utf-8')
print('NOVA synchronization source installed; existing queue preserved')
"@
$taskInstall | & ssh @taskOptions $Server 'python3 -B -'
if ($LASTEXITCODE -ne 0) { throw 'Source verification failed / 服务端源码校验失败。' }
& ssh @taskOptions $Server "python3 -B $RemoteProject/sync_server.py --root $RemoteProject/runtime/broker --init --quota-gib 15 --reserve-gib 5"
if ($LASTEXITCODE -ne 0) { throw 'Schema initialization failed / 队列初始化失败。' }
@{ Server=$Server; RemoteProject=$RemoteProject; ArchiveSHA256=$taskHash; Files=$taskHashes; AddedPublicPorts=0 } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskStage 'deployment.json') -Encoding utf8
Write-Output 'Deployed through existing SSH; no Windows Python/helper or new public port / 已复用 SSH 部署，不新增公网端口。'
