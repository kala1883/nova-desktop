# Changelog / 更新记录

## v0.6.0 — 多任务批量执行 / Saved batch tasks

### 简体中文

- 新增 **设置 → 批量任务**：最多保存 16 条任务，每条独立配置名称、命令和最多 24 个目录。
- 支持选择任务后单独一键启动，每条任务可保存“顺序执行”或“同时执行”模式。
- 支持自定义 Windows cmd 命令及 `&&` 组合语法，命令最长 2047 个字符。
- 逐目录显示执行状态和退出码；取消时跳过尚未开始的目录，已开始的命令继续完成。
- 任务配置事务化保存在本地 SQLite；已有单条任务配置自动迁移并保留原数据。
- 新增任务管理、配置迁移、顺序/并行执行与取消行为测试。
- 修复文件管理器分隔条命中检查和编译警告，补充 EXE 版本信息。

每次运行一条已保存任务。并行模式下，该任务中的目录同时执行。

### English

- Added **Settings → Batch tasks** with up to 16 saved tasks, each with its own name, command and up to 24 folders.
- Run a selected task individually and save its sequential or parallel execution mode.
- Configure Windows cmd commands, including `&&` combinations, up to 2047 characters.
- Show per-folder status and exit codes. Cancellation skips pending folders while active commands finish.
- Persist task collections transactionally in local SQLite and migrate existing single-task settings while retaining the original data.
- Added coverage for task editing, migration, sequential/parallel execution and cancellation.
- Fixed file-manager splitter hit checks and compiler warnings; added executable version metadata.

One saved task runs at a time. Parallel mode runs that task's folders concurrently.
