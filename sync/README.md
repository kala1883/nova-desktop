# Native synchronization / 原生设备同步

Use **Settings → Devices and synchronization** or `nova-desktop.exe --sync`.
Windows runs one native process with no Python, browser, local listener or agent.
The stdlib Python broker in `server/` runs on the selected SSH server only.
Existing SSH profiles, saved login records and host verification are reused.

在 **设置 → 设备与同步** 中管理设备、目录配对和传输。本机全部由原生 C 代码实现，
Python 仅用于服务器。关闭同步窗口会隐藏窗口，自动同步随 NOVA 继续运行；退出 NOVA
会取消并等待活动任务结束，不会留下独立后台进程。

## Setup / 配置

1. Add this computer's SSH public key to your server's authorized keys. In
   **Server**, choose/create its SSH connection, select the corresponding private
   key and verify the host fingerprint. Click **Configure server**. It installs
   and connects to the embedded broker; existing queue data and quotas survive.
   A new queue uses `/opt/nova-sync`, 15 GiB staging and 5 GiB free reserve.
   Use the same **Configure server** action to update its embedded server component.
   **Refresh** reloads state. **Published by** shows registered device names;
   missing legacy publication metadata is shown as **Publisher unknown**.
2. The remote account needs write permission to its project/queue paths. Under
   **Advanced**, use a writable home directory for a non-root account. On Linux,
   configuration can install Python using apt-get/dnf/yum/apk when the SSH account
   is already root; there is no sudo or elevation. Otherwise an administrator must
   prepare Python 3.9+, SQLite JSON and zlib. No public port or daemon is created.
3. Click **Register this device**; the computer name is prefilled. Repeat on other
   computers with their own keys and identities. Up to 16 devices are allocated
   automatically. Never copy another device's local.json or cache as enrollment.
4. **Publish folder** chooses the source directory and name. Other computers
   **Refresh**, open **Available folders**, select its publication and **Subscribe folder**, choosing their own
   absolute local path. Saving only exchanges metadata. New folders start with
   automatic mode paused. There are at most 16 publications and 16 local bindings.
5. **Check all** compares every local sync folder and current server versions;
   it hashes contents without publishing or applying files. The upper table shows
   pending pushes, pulls, conflicts or missing source content. Check folders and
   choose **Push checked**, **Pull checked**, or **Sync checked** for both. Selecting
   a row opens its details; only checkboxes set the batch transfer scope.
6. The lower table shows file versions. Select a conflict and choose **Keep local
   version** (publish it as a child of the received server version) or **Use server
   version**. Existing local content is verified and copied to
   `config/sync/client/recovery/<sha256>/<relative-path>` before replacement.
   **Transfers → Open recovery copies** opens these ordinary files. Stale local
   hashes or server-version choices are rejected; there is no automatic text merge.

每台电脑先将自己的 SSH 公钥加入服务器，然后在原生界面**配置服务器 → 注册本设备**，
自动分配身份，最多 16 台。源电脑**发布目录**；其他电脑刷新后选择该目录并**订阅**，
本机路径可以不同。保存只登记元数据，明确点击同步才会传文件。
后续更新服务器组件仍使用**配置服务器**；**刷新**只读取状态。
**发布设备**显示登记的设备名称，包括本机；缺失的旧发布信息显示为**发布设备未知**。

日常使用**检查全部**检查本机所有目录，勾选后选择推送、拉取或双向同步。文件表提供
本机/服务器版本选择；先保留本机恢复副本，再处理冲突。默认关闭自动同步和删除传播。
本机目录与可订阅目录分开展示。选中行查看详情，勾选框决定批量传输范围；自动同步
在本机目录工具栏独立开启/关闭。路径、配额、旧队列连接与迁移收在高级设置。

## Folder lifecycle / 目录状态与操作

Every connected device can edit, push and pull. **Published by** identifies the
original creator, not a primary writer or read-only policy. Three independent
facts describe a local folder:

| Fact / 事实 | States / 状态 | Action / 操作 |
| --- | --- | --- |
| Participation / 接入关系 | Unverified, local only, published here, subscribed here; legacy creator unknown shows Connected / 待确认、待接入、本机发布、本机订阅；旧记录来源未知显示已接入 | Refresh or retry connection / 刷新或重试接入 |
| Automatic / 自动同步 | On or off, independent of manual transfer / 开启或关闭，与手动传输独立 | Toggle for the current folder / 切换当前目录开关 |
| Last comparison / 最近比较 | Needs check, changes, conflicts, awaiting source, matches server / 待检查、有差异、冲突、等待补发、与服务器一致 | Check; push, pull or sync checked folders; explicitly resolve conflicts / 检查，推送、拉取或同步勾选项，明确选择冲突版本 |

**File changes** lists differences and the device that authored each server
version. **Devices** lists bindings, local paths and outstanding receipt counts.
No pending versions means the server has no unreceived queued version for that
device; it says nothing about its connectivity or unpublished local edits. Older
servers without receipt counts show Unknown; use **Server → Configure server** to
update the embedded component. No schema or device identity migration is needed.

Successful transfers return a new comparison using the same connection. Starting
a file operation, refreshing successfully or switching servers invalidates older
comparisons. A failed check does not turn off automatic mode; a failed transfer
pauses automatic work for that folder. Batch and automatic work retain the user's
detail selection; automatic work waits while a folder is being edited.

Saving a binding can succeed locally while the server is unreachable. The folder
retains its local settings and offers **Retry connection**. **Cancel edit** discards
an unsaved draft. **Disconnect here** removes only this device's binding, retaining
local files, other devices and the discoverable publication; it does not withdraw
the publication globally. Global publication deletion is not currently exposed.

Re-saving keeps version history and receipts. With deletion sync enabled, deleting
one file queues one tombstone; it does not republish the other files. Local content
checks still read/hash files and show **Checking local files (no transfer)**.
Timestamp-only differences do not create a version; an identical incoming version
does not rewrite the working file. Diagnostics in `config/logs/nova.log` distinguish
`comparison`, `file_skip`, `file_publish`, `file_receive`, and `run_end` with wire
content byte counts. The two rotated files are at most 2 MiB each, local only, and
contain bounded path-hash samples rather than full work paths or credentials.

重新保存会保留历史版本和回执。启用删除同步后，删除一项只排队一条删除记录，不会重新
发布其他文件；“校验本机文件（无传输）”仍会读取并计算文件哈希。仅时间变化不创建版本，
相同内容的接收版本不重写工作文件。`config/logs/nova.log` 记录比较、跳过、发布、接收和
任务汇总及实际内容传输字节。两份轮转日志各限 2 MiB，仅保存在本机，文件判定只记录
有界的路径哈希样本，不含完整工作路径或凭据。

各参与设备均可修改并推送、拉取；发布者只是最初创建目录的设备，不是唯一写入者。
目录关系、自动调度、文件比较独立展示。下方“文件差异”显示两端版本与来源设备，只有
冲突可选择版本；“参与设备”显示路径、待接收数与等待补发数。无待接收版本不等于设备
在线，也不代表其本机没有尚未推送的修改。旧服务器缺少回执统计时显示未知，可通过
“服务器 → 配置服务器”更新内置组件，无需迁移身份或数据格式。

传输成功后在同一连接内重新比较；开始文件操作、成功刷新或切换服务器会清除失效
比较。检查失败不关闭自动同步，传输失败暂停该目录自动同步。批量及自动任务保留当前
查看的目录，编辑期间等待自动任务。接入失败保留本机配置并支持重试，取消编辑丢弃未保存
草稿。“断开本机”保留本机文件、其他设备及服务器发布信息，目前没有全局撤销发布入口。

服务器已有 Python 时直接配置；缺失时，SSH 账户本身为 root 才会使用支持的系统
包管理器安装，不会提权。非 root 账户需管理员预先准备 Python 3.9+、SQLite JSON、
zlib，并在高级设置选择有写权限的目录。本机不需要 Python 或脚本。

## Server migration / 服务器迁移

Choose a destination in **Servers** and click **Advanced → Migrate and switch** on
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
   **Sync checked**, then re-enable automatic pairs as desired.

A dropped connection or closed NOVA preserves `config/sync/client/server-migration.json`.
Open the manager again and choose **Advanced → Migrate and switch**. A crash between
source redirect and destination activation is recoverable by the same action.
Before redirect is committed, **Restore source** re-enables the old queue; after
commit it refuses rollback, preventing two writable copies. A restored migration's
old destination remains frozen; use a new empty queue directory for a fresh attempt.
Unknown/nonempty destinations and identical server identities are refused.

On the other computer, attempting the old server reports the move. Click **Follow
migrated destination**, enter that computer's own SSH login and verify the new host
if needed. It verifies node identity and the retained lineage before switching.
If the old server is gone, choose the new server directly and use **Connect / safe
switch**. All devices must use this updated NOVA version for the follow UI.
The migration lineage is bounded to 64 moves and manifests to 50,000 queue files.

服务器页选择目标后点“高级设置 → 迁移并切换”。迁移会暂停本机配对、冻结旧端，完整复制
并校验设备、配对、版本、接收记录、块库存以及未接收/未传完的内容，再提交切换。
旧端数据保留，迁移不复制实际工作目录；数据按包中转，本机不落地大型迁移包。
中断或退出后重新打开 NOVA，继续同一迁移即可。提交切换前可点“恢复旧服务器”；
提交后只能继续完成切换，避免两个可写副本。恢复旧端后，旧目标保留为冻结快照，
再次迁移请选择新的空队列目录。另一台电脑可点“跟随已迁移目标”，或直接选择新端
后“高级设置 → 连接已有队列”。登录凭据由各电脑自行保存，不从旧端复制。首次升级旧版服务器
需等待旧会话结束；单次迁移最多 50,000 个队列文件，版本链最多保留 64 次服务器迁移。

## Transfer and recovery / 传输与恢复

- Gear content-defined chunks are 128 KiB–2 MiB, averaging about 512 KiB.
  SHA-256 checks chunks and reconstructed files. Only missing chunks are uploaded,
  with optional zlib compression. This is block reuse: compressed/encrypted files
  or widely changed data may still require most of the file.
- Upload and download retain flushed 128 KiB packet offsets across interruption.
  Resume using **Sync checked**. Received files are verified, atomically replaced and
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
- Conflicts pause and preserve the working file until a local/server version is
  chosen. Only regular files are supported, not empty directories,
  ACLs, alternate streams or link identity. Reparse points are not followed. Paths
  obey Windows filesystem limits. Scans stop at depth 32 or 100,000 entries.
  Content comparisons hash files, including edits that preserve size and timestamps.
- `.git` and NOVA reconstruction files are always excluded. Other patterns are
  semicolon-separated Windows wildcards matched against relative paths/basenames.
  Suggested defaults exclude dependencies, build caches, transient files and `*.tmp`.
- Deletion synchronization is opt-in on both ends. Received deletes use Windows
  Shell confirmation and Recycle Bin semantics, subject to filesystem support.
  Declining pauses the task. Removing a pair unbinds metadata and never deletes
  work files, snapshots or pending versions.

The broker retains up to 16 synchronization groups, including groups whose local
bindings were removed. Group/history deletion is not exposed in this version.

云端只暂存未交接的块，所有目标端持久保存、校验并确认后回收。双方本机块缓存需要保留，
目前不会自动清理历史块。首次全量超过配额时分批交接。删除同步默认关闭，相关设备均需
启用；接收删除走 Windows Shell 的确认和回收站流程。冲突暂停并保留本机文件。
空目录、ACL、备用数据流与重解析点不在范围内。扫描限制为 32 层、100,000 个条目。
云端最多保留 16 个同步组（包括已移除本机绑定的组），当前不提供删除同步组或历史的入口。

Subscriptions added after reclamation reactivate retained version metadata.
Source devices **push** the required historical chunks from their durable caches;
new subscribers then **pull** their version chains. With a small quota, alternate
source pushes and subscriber pulls to drain a large history. A device must keep
its cache, including chunks for versions it previously published. Unsubscribing
does not discard already pending receipts or files.

后加入的电脑订阅后，云端会重新标记历史接收任务。相应源设备推送本机缓存补发，
新设备再拉取；历史内容超过暂存配额时，可交替推送和拉取逐步交接。所有订阅端确认
之前不会回收内容。取消订阅不丢弃此前尚未接收的任务或原文件。

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
python -B tests/sync_workflow.py
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
