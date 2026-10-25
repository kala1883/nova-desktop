# Configuration / 配置

Daily data is saved in Git-ignored `runtime.json`: tasks/subtasks, commands and
folders, workspaces, launch items, presets, saved tabs, favorites and language.
Private UI/session state and device identity remain in `local.json`. Tracked
`nova.json` is an explicit shared snapshot; normal saves never modify it.
Repository executables use root `config/`; standalone executables use adjacent
`config/`. Packages include the shared snapshot, server source and explicit snapshot tools;
private runtime files are excluded.

日常任务、子任务命令/目录、工作区、启动项、命令预设、目录标签、收藏和语言
写入被忽略的 `runtime.json`，本机会话及设备身份仍在 `local.json`。
`nova.json` 是纳入 Git 的共享快照，日常保存不修改它；程序包只携带共享快照。
仓库内程序使用根 config，独立程序使用 exe 旁 config。

On first launch, the old validated snapshot is migrated without altering its
bytes, keeping all IDs, tasks, paths and commands. Its original copy is retained
as `nova.migration.json`, with normalized seed `runtime.seed.json` and merge base
`runtime.base.json`. Existing local state wins over legacy transient fields.
If the snapshot is absent/uninitialized, the existing read-only AppData SQLite/INI
migration still applies. Originals stay intact. Existing runtime data always wins
on later launches; corrupt/newer runtime data is rejected without fallback.
Startup backs up runtime/local to their respective `.backup.json` files.

首次启动校验并迁移旧配置，保留任务、路径、命令和全部 ID，旧 nova 的原始字节
保留为 `nova.migration.json`，首次规范化结果为 `runtime.seed.json`，
合并基线为 `runtime.base.json`。已有本机状态优先；无已初始化快照时仍只读迁移
AppData 的 SQLite/INI，原文件保留。后续启动只使用 runtime，不自动应用拉取的快照；
本机数据损坏或版本更新时拒绝加载，不退回旧快照。
启动备份为 `runtime.backup.json` 和 `local.backup.json`，恢复前关闭 NOVA。

After building, close NOVA and run `deployment/config_snapshot.bat -Action Migrate`
once to preserve existing data and clear the old dirty tracked JSON against HEAD.
Staged/conflicted or subsequently edited snapshots are refused; exact backups
remain. `--migrate-data` alone leaves Git files intact and never runs tasks.

编译后关闭 NOVA，执行一次 `deployment/config_snapshot.bat -Action Migrate`，
迁移现有数据，再将旧的未提交 nova 恢复到 HEAD，保留本机新增及原始备份。
已暂存、有冲突或迁移后再次编辑的快照拒绝清理；单独 `--migrate-data` 不操作 Git。

## Explicit exchange / 显式交换

Close NOVA, then use `deployment/config_snapshot.bat -Action Export` to merge and
update the tracked snapshot; review/commit/push it explicitly. After pulling on
another computer, use `-Action Import` to merge into local data. Either action
supports `-Preview`. Both use the last shared baseline and stable IDs; independent
additions survive, real conflicts stop before publication. Native validation
checks raw documents and the merged result. Publication failures restore all
original bytes; backups/previews remain under ignored `.snapshot-*/`. Import
updates the remote baseline; Export advances it to the merged snapshot. Neither
copies local state, credentials, identities or caches, nor commits/pushes.
Ordinary Git pulls do not change runtime data and need no special sync script.

关闭 NOVA 后，`deployment/config_snapshot.bat -Action Export` 合并并导出共享快照，
核对后自行提交、推送；目标电脑拉取后以 `-Action Import` 合并到本机数据。
两者均支持 `-Preview`，按上次共享基线和稳定 ID 合并，保留独立新增，真实冲突停止。
原生校验检查原始 JSON 及结果；写入失败恢复原始字节，备份/预览留在 `.snapshot-*/`。
Import 基线推进到远端快照，Export 推进到合并快照。均不复制本机状态、凭据、身份或缓存，
不自动提交/推送；普通拉代码不修改 runtime，也无需特殊同步脚本。

SSH profiles (`ssh_servers`) and remote paths are shareable snapshot data;
passwords, private key paths and host trust pins are excluded. Restoring remote
tabs never connects automatically. `files/PaneN/ServerMode` is local-only.
SSH 配置与远端路径可以显式共享；登录凭据、私钥路径、主机信任不进入 JSON。
远端标签恢复不自动连接，各窗格 ServerMode 只存在 local.json。

## JSON format / JSON 格式

The root contains exactly six fields / 根对象包含以下六个字段：

| Field / 字段 | Meaning / 含义 |
| --- | --- |
| `version` | Format version, currently `1` / 格式版本，目前为 `1` |
| `application` | Fixed `NOVA Desktop` / 固定为 `NOVA Desktop` |
| `initialized` | Whether initialization completed / 是否已经初始化 |
| `settings` | Objects with string `scope`, `section`, `key`, `value` / 字符串字段 `scope`、`section`、`key`、`value` 组成的设置 |
| `workspaces` | Objects with string `id`, integer `position`, string `name` / 字符串 `id`、整数 `position`、字符串 `name` 组成的工作区 |
| `items` | Objects with string `id`, string `workspace_id`, integer `position`, string `name`, string `target` / 启动项的 ID、工作区 ID、排序、名称和目标 |

Settings preserve existing keys and version markers for `app` preferences,
`files` pane sessions and favorites, `batch_tasks` tasks and subtask commands,
and `file_commands` presets. IDs are positive 64-bit decimal strings; positions
start at zero. Task shortcut targets remain `nova-batch:<id>`.

设置保留已有键名和版本标记，包括 `app` 界面偏好、`files` 窗格会话和收藏、
`batch_tasks` 任务与子任务命令、`file_commands` 命令预设。
ID 是正 64 位整数的十进制字符串，排序从 0 开始。任务入口仍为 `nova-batch:<id>`。

Close NOVA before editing this file. Keep IDs unique and preserve supported
limits: 8 workspaces, 20 items each, 16 tasks, 24 subtasks each, 4 panes,
12 tabs each, 32 favorites. The document is limited to 16 MiB. Unsupported
versions, duplicate keys, missing fields, malformed UTF-8, orphan items and
over-capacity data are rejected. Reading configuration never executes commands.

编辑前关闭 NOVA。保持 ID 唯一，并遵守容量限制：8 个工作区、每区 20 项、16 条
任务、每条 24 个子任务、4 个窗格、每窗格 12 个标签、32 个收藏；整个文件最多
16 MiB。更新版本、重复键、缺失字段、无效 UTF-8、无对应工作区的启动项和超容量
数据会被拒绝。读取配置本身不会执行命令。


## Device synchronization / 设备同步

The entire versioned `sync` settings scope belongs to `local.json`: Manager stores
Version, Count, Role, Interval, Server, Registered, Node, Name, Project and Broker;
PairN stores Id, Dataset, Name, Group, Path, Excludes, Paused and Deletes. It is
transactional and never exported into shared JSON. Limits: two device slots and
16 pairs. `config/sync/client/` contains the private disk SQLite journal, chunk
cache and partial downloads. Preserve it; do not copy identities between devices.
Packages include shared configuration and server source only. The first-open
`sync/import.json` migration preserves identity and starts all folders paused.

`sync` scope 全部保存在被 Git 忽略的 local.json，身份、配对与路径不进入共享配置。
`config/sync/client/` 保存断点与块缓存，需要保留，不随包分发；第二台电脑应独立登记。
详见 [同步说明](../sync/README.md)。

`sync/Manager/Instance` is an optional Version=1 private field identifying the
active server instance. Migration ancestry is verified remotely before switching.
`sync/client/server-migration.json` is a version-1 credential-free recovery checkpoint;
retain it while migration is pending. After local connection settings commit,
it becomes `server-migration.completed.json`. Neither file enters Git/packages.
候选服务器不会提前覆盖活动连接；迁移断点不包含密码、私钥或口令，未完成时需保留。
