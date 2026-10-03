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
