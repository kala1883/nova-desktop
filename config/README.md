# Configuration / 配置

`nova.json` is the UTF-8 configuration read and written by NOVA. It is available
for Git version control. Executables inside this checkout use the repository's
`config/nova.json`; outside a checkout, a package uses `config/nova.json` beside
its executable. Every build copies configuration alongside the latest executable;
deployment archives include their own copy.

`nova.json` 是 NOVA 实际读写的 UTF-8 配置，可以纳入 Git。在仓库内运行时使用
仓库的 `config/nova.json`；独立发布包使用 exe 旁的 `config/nova.json`。
每次编译将配置复制到最新程序目录，部署压缩包也包含配置副本。

An empty initial file may have `initialized: false` and empty arrays. First launch imports
existing SQLite or INI configuration from `%APPDATA%\NOVA Desktop`, preserving
the originals. Without older data it creates defaults. It then writes
`initialized: true`, and subsequent launches use JSON. Leave the initial file
unchanged if you want to import existing configuration on first launch.

空的初始文件可设置 `initialized: false`，数组为空。首次启动从
`%APPDATA%\NOVA Desktop` 导入旧 SQLite 或 INI 配置，原文件保留；没有旧数据时
生成默认配置。之后写入 `initialized: true`，后续启动使用 JSON。
如需导入旧配置，请在首次启动前保留初始文件。

This file stores actual saved data as well as preferences: named tasks, each
subtask's command and folder, workspace launch items and task shortcuts, command
presets, tabs and favorites. To populate the initial file without opening NOVA's
UI, close NOVA and run `build/packages/nova-desktop.exe --migrate-data`.
Re-running this command uses an already initialized JSON file; it does not
overwrite it with older data or execute any task.

这里保存的也包含实际使用数据：已命名任务、每个子任务的命令与目录、工作区
启动项和任务入口、命令预设、标签及收藏。关闭 NOVA 后运行
`build/packages/nova-desktop.exe --migrate-data`，可在不开界面的情况下填充初始文件。
重复运行时使用已初始化的 JSON，不会用旧数据覆盖它，也不会执行任务。

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

Startup backs up valid configuration as `nova.backup.json`. To restore, close NOVA
and copy the backup over `nova.json`. Backups and `.nova-*.tmp` files are ignored
by Git. The primary file includes your names, paths and command text.

启动时备份为 `nova.backup.json`。恢复时关闭 NOVA，再将备份复制覆盖 `nova.json`。
备份和 `.nova-*.tmp` 被 Git 忽略。主配置包含用户名称、路径和命令文本。

For cross-computer use, close NOVA on both computers first. Commit/push the
source computer's changed JSON, then pull it on the destination and rebuild.
Building/deploying on the destination alone cannot retrieve uncommitted source
data. Build/deploy output reports the configuration path, task/tab counts and
SHA256. Copy the whole standalone package, including its config folder, when
not using a Git checkout. Stored absolute folder paths must exist on that computer.

跨电脑使用时先关闭两台电脑的 NOVA。在源电脑提交、推送已修改的 JSON，目标电脑
再拉取并编译；仅在目标电脑编译或部署，无法取得源电脑未提交的数据。
编译、部署会显示配置路径、任务/标签数量和 SHA256。使用独立程序包时，复制整个包，
包括 config 文件夹；已保存的绝对目录路径需要在该电脑存在。

## Shared data and local state / 共享数据与本机状态

`nova.json` remains tracked and contains tasks, subtasks, workspaces, launch items,
command presets, saved folder tabs, favorites and language. `local.json` is ignored
by Git and contains active workspace, selected tab indices, window modes, sidebar,
pane layout/navigation-tree state and splitter positions. Existing combined JSON
is migrated automatically, preserving the current machine's state; existing local
values override imported transient values. A synchronized tab deletion resets an
out-of-range cached selection to zero. Local-only changes do not rewrite shared
JSON. Transactions restore both files if publication fails. Each file has its own
ignored startup backup (`nova.backup.json`, `local.backup.json`). Packages include
shared data only, so the destination machine keeps its own local state.

`nova.json` 继续纳入 Git，保存任务、子任务、工作区、启动项、命令预设、保存的目录
标签、收藏和语言。`local.json` 被 Git 忽略，保存活动工作区、选中标签索引、窗口模式、
侧边栏、窗格布局/目录树状态和分隔线位置。旧文件自动迁移并保留本机状态；已有本机
值优先于旧共享文件中的临时状态。同步删除标签后，失效的本机选中索引重置为 0。
仅改变本机状态不会重写共享 JSON，发布失败时事务恢复两份文件。
两份文件分别备份为被忽略的 `nova.backup.json`、`local.backup.json`。
发布包只携带共享数据，目标电脑保留自己的本机状态。

For a Git conflict, close NOVA and run `deployment/resolve_config.bat` for a backed-up
preview. `-Apply` applies/stages a clean semantic merge. Review conflicts reported
for the same entity; do not use `git push --force` or blindly choose a full file.

Git 冲突时，关闭 NOVA 后运行 `deployment/resolve_config.bat` 生成带备份的预览；
加 `-Apply` 应用、暂存无冲突的语义合并。同一条数据的冲突需要核对，避免强制推送
或直接选择整份文件覆盖另一边。

For routine updates with uncommitted configuration on `main`, close NOVA and run
`deployment/sync_config.bat` (`-Preview` for a read-only working-file preview).
It backs up all three JSON versions, fast-forwards from `origin/main`, and merges
local changes without using stashes or committing. Real conflicts stop before
working files change. Item position compaction and closed-tab residue are ignored
during semantic comparison; incompatible relative reorderings require review.

`main` 分支有未提交配置时，关闭 NOVA 后运行 `deployment/sync_config.bat`；
`-Preview` 只获取远端并生成预览，不修改工作文件。它备份三份 JSON，快进更新
`origin/main` 并合并本机修改，不操作 stash 或提交。真正冲突会在工作文件修改前停止。
语义比较忽略启动项编号压缩和关闭标签残留；不兼容的相对顺序调整仍需人工核对。
