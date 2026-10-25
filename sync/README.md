# Native synchronization / 原生设备同步

Use **Settings → Devices and synchronization** or `nova-desktop.exe --sync`.
Windows runs one native process with no Python, browser, local listener or agent.
The stdlib Python broker in `server/` runs on the selected SSH server only.
Existing SSH profiles, saved login records and host verification are reused.

在 **设置 → 设备与同步** 中管理设备、目录配对和传输。本机全部由原生 C 代码实现，
Python 仅用于服务器。关闭同步窗口会隐藏窗口，自动同步随 NOVA 继续运行；退出 NOVA
会取消并等待活动任务结束，不会留下独立后台进程。

## Setup / 配置

1. Open the **Servers** tab, choose/create a NOVA SSH profile and enter its login
   details. Verify the host fingerprint on first connection. The server needs
   Linux, SSH/SFTP, Python 3.9+ with SQLite JSON support, and write permission to
   the selected absolute project/queue paths. Use a writable home directory when
   the SSH user cannot write `/opt`; no elevation or local Python is used.
2. On a fresh server choose **One-click deploy**. The EXE carries all six server
   sources as resources, uploads verified files over SSH and activates an immutable
   release. Defaults for a new queue are 15 GiB quota and 5 GiB free reserve.
   Existing queue data and quotas survive redeployment. An unrelated nonempty
   project directory is refused. No public port or daemon is created.
3. On an already deployed server choose **Connect / safe switch**. Checking or
   selecting a destination alone never changes the active server or folder groups.
   Fresh clients can connect; existing clients must match their registered node
   and server lineage. An empty destination cannot silently reset existing history.
4. In **Devices**, enter the first computer's name, select A and register. On the
   second computer connect to the same server, select B and register. Use a fresh
   local identity on B; never copy A's local.json or chunk cache as B's enrollment.
5. In **Folder pairs**, add A's directory and create a group. On B add its local
   directory and select that existing group. Paths may differ. New pairs are paused
   and saving transfers binding metadata only. Limits: two devices and 16 pairs.
6. **Sync now** on the source and wait for upload; sync on the destination before
   editing. Automatic mode scans enabled pairs every 60 seconds while NOVA runs.
   A failure pauses that pair. Start with deletion sync disabled.

新电脑全程使用原生界面：服务器页选择 SSH 连接，连接已有服务器；设备页登记 B；
目录页选择本机目录和已有同步组，即可手动同步。首次使用空服务器时点击“一键部署”，
无需手动执行脚本，本机无需 Python。服务器需预装 Python 3.9+、支持 SQLite JSON，
并允许该 SSH 用户写入所选目录；已有队列与迁移保留原配额。此版本仍为 A/B 双设备，
不支持第三台设备或自动抢占已有设备位。

## Server migration / 服务器迁移

Choose a destination in **Servers** and click **Migrate / resume and switch** on
an already registered computer. This pauses its local pairs and:

1. Deploys/verifies the destination and upgrades the source to the gate-aware
   server release. First upgrading a legacy server requires its old transfer
   sessions to finish; managed servers coordinate through a process-shared gate.
2. Durably freezes source writes, snapshots SQLite and inventories queued chunks
   and partially uploaded files. Every other device is blocked from writing there.
3. Streams packets through this computer to a destination staging queue, retaining
   per-file offsets and verifying SHA-256 and SQLite integrity. No whole working
   folder or large migration archive is stored on this computer.
4. Prepares the destination, commits the source's redirect and only then enables
   the new queue. Device IDs, bindings, versions, receipts, chunk inventory and
   unreceived/partial jobs are retained. The local active server changes only after
   verified activation; failed local settings writes leave a resumable checkpoint.
5. Keeps the old server and its data. Local pairs remain paused for review; test
   **Sync now**, then re-enable automatic pairs as desired.

A dropped connection or closed NOVA preserves `config/sync/client/server-migration.json`.
Open the manager again and choose **Migrate / resume and switch**. A crash between
source redirect and destination activation is recoverable by the same action.
Before redirect is committed, **Restore source** re-enables the old queue; after
commit it refuses rollback, preventing two writable copies. A restored migration's
old destination remains frozen; use a new empty queue directory for a fresh attempt.
Unknown/nonempty destinations and identical server identities are refused.

On the other computer, attempting the old server reports the move. Click **Follow
migrated destination**, enter that computer's own SSH login and verify the new host
if needed. It verifies node identity and the retained lineage before switching.
If the old server is gone, choose the new server directly and use **Connect / safe
switch**. Both devices must use this updated NOVA version for the follow UI.
The migration lineage is bounded to 64 moves and manifests to 50,000 queue files.

服务器页选择目标后点“迁移 / 继续并切换”。迁移会暂停本机配对、冻结旧端，完整复制
并校验设备、配对、版本、接收记录、块库存以及未接收/未传完的内容，再提交切换。
旧端数据保留，迁移不复制实际工作目录；数据按包中转，本机不落地大型迁移包。
中断或退出后重新打开 NOVA，继续同一迁移即可。提交切换前可点“恢复旧服务器”；
提交后只能继续完成切换，避免两个可写副本。恢复旧端后，旧目标保留为冻结快照，
再次迁移请选择新的空队列目录。另一台电脑可点“跟随已迁移目标”，或直接选择新端
后“连接 / 安全切换”。登录凭据由各电脑自行保存，不从旧端复制。首次升级旧版服务器
需等待旧会话结束；单次迁移最多 50,000 个队列文件，版本链最多保留 64 次服务器迁移。

## Transfer and recovery / 传输与恢复

- Gear content-defined chunks are 128 KiB–2 MiB, averaging about 512 KiB.
  SHA-256 checks chunks and reconstructed files. Only missing chunks are uploaded,
  with optional zlib compression. This is block reuse: compressed/encrypted files
  or widely changed data may still require most of the file.
- Upload and download retain flushed 128 KiB packet offsets across interruption.
  Resume using **Sync now**. Received files are verified, atomically replaced and
  journaled before ack. The server then reclaims unreferenced content; unreceived
  content is never deleted on a timer. Quotas reject new allocations safely.
- Device identity, SSH profile ID and pairs live in Git-ignored `config/local.json`.
  `config/sync/client/` holds the disk SQLite index, chunks, snapshots and downloads.
  Never copy A's identity to B or delete the receiver's cache: future versions may
  depend on chunks already reclaimed from the server. Packages exclude this data.
- The local cache retains historical chunks without automatic garbage collection.
  Reserve local space for this cache and temporary reconstruction files. The
  server quota limits pending chunks, not total working-folder size; first uploads
  larger than the quota need smaller handoffs. Metadata has a separate 256 MiB cap.
- Conflicts pause and preserve the working file; reconcile manually. There is no
  conflict-resolution UI. Only regular files are supported, not empty directories,
  ACLs, alternate streams or link identity. Reparse points are not followed. Paths
  obey Windows filesystem limits. Scans stop at depth 32 or 100,000 entries.
  Unchanged detection uses size/mtime, so externally preserved timestamps can hide edits.
- `.git` and NOVA reconstruction files are always excluded. Other patterns are
  semicolon-separated Windows wildcards matched against relative paths/basenames.
  Suggested defaults exclude dependencies, build caches, transient files and `*.tmp`.
- Deletion synchronization is opt-in on both ends. Received deletes use Windows
  Shell confirmation and Recycle Bin semantics, subject to filesystem support.
  Declining pauses the task. Removing a pair unbinds metadata and never deletes
  work files, snapshots or pending versions.

The broker retains up to 16 synchronization groups, including groups whose local
bindings were removed. Group/history deletion is not exposed in this version.

云端只暂存未交接的块，目标端持久保存、校验并确认后回收。双方本机块缓存需要保留，
目前不会自动清理历史块。首次全量超过配额时分批交接。删除同步默认关闭，两端均需
启用；接收删除走 Windows Shell 的确认和回收站流程。冲突暂停并保留本机文件。
空目录、ACL、备用数据流与重解析点不在范围内。扫描限制为 32 层、100,000 个条目。
云端最多保留 16 个同步组（包括已移除本机绑定的组），当前不提供删除同步组或历史的入口。

## Migration / 迁移旧原型

```powershell
.\deployment\migrate-sync.ps1 -Source '<old-project-directory>' -ArchiveSource
```

This pauses and stops only the old PID-verified local controller, not NOVA. It
verifies/copies the old cache and index, preserves node identity, forces pairs
paused, and stages `config/sync/import.json` for transactional first-open import.
Original research, configuration and prototype are preserved under Git-ignored
`build/sync-migration/`. Credentials/private keys are not imported. Restart an old
running NOVA build yourself; never run both controllers with the same identity.

迁移保留身份、目录和断点，全部目录先暂停；旧项目与研究材料归档到 NOVA 项目内，
不会移动工作目录。首次打开原生窗口会导入配置。旧版 NOVA 需要自行退出并重启。

## Verification / 验证

`deployment/test-sync.bat` checks private persistence, migration and native UI
using temporary folders and fake callbacks. The optional loopback fixture needs
Python with Paramiko for tests only:

```powershell
python -B tests/sync_fixture.py build/sync-client-test.exe
python -B tests/sync_server_fixture.py build/sync-server-test.exe
```

It injects upload/download loss and checks offsets, native/Python Gear agreement,
delta reuse, hashes, reclamation and a fake recycle callback. It uses no real
cloud hosts, work folders or saved credentials. The administration fixture runs
real bootstrap subprocesses behind loopback SSH, exercises embedded deployment,
READY/STAGING record migration, interrupted copying/cutover, pre-commit restoration,
offline peer attachment and unknown/nonempty destination refusal.
See [PROTOCOL.md](PROTOCOL.md).
