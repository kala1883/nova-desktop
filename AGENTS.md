# AGENTS.md

This file applies to the entire repository.

## Project overview

NOVA Desktop is a lightweight Windows workspace launcher and embedded
multi-pane file manager. It is written in C11 against the Win32 and Windows
Shell APIs. The application is a native, single-process desktop program: do not
introduce a web runtime, managed runtime, installer framework, service, or
additional background process without an explicit product requirement.

Read these files before making broad changes:

- `README.md` or `README.zh-CN.md` for supported behavior and limitations.
- `ARCHITECTURE.md` for ownership, dependency, performance, and migration goals.
- `DESIGN.md` for interaction and visual decisions.

Treat future-state sections in `ARCHITECTURE.md` as direction, not as claims
about code that already exists.

## Repository map

- `src/main.c`: application setup, main window, commands, and message loop.
- `src/core/`: UI-independent workspace and launch-queue rules.
- `src/platform/`: Windows/JSON integration, in-memory SQLite transactions and resource ownership.
- `src/ui/`: file manager and reusable interaction logic.
- `src/i18n.h`: Simplified Chinese / English string selection.
- `tests/`: unit and Windows integration tests.
- `third_party/sqlite/`: pinned SQLite amalgamation and provenance.

Do not edit `third_party/sqlite/sqlite3.c` or `sqlite3.h` as part of ordinary
feature work. Update the vendored version only as a deliberate dependency
change, and update its README, source URL, hashes, and compatibility tests.

## Architecture and ownership rules

- Keep mutable UI state and HWND ownership on the main UI thread unless a
  documented subsystem explicitly owns a worker thread.
- `src/core` must stay independent of HWNDs, COM, registry access, file I/O,
  message boxes, and UI text.
- Put Windows-specific behavior behind `src/platform` or `src/ui`; do not move
  Win32 dependencies into core data rules.
- Preserve resource ownership conventions. Destroy owned HICON, HIMAGELIST,
  HFONT, HBRUSH, COM, timer, registry, and window resources on every success
  and error path. Never destroy borrowed system image lists or handles.
- COM initialization and release must occur on the same thread. The current
  application uses the caller's STA/OLE apartment.
- Prefer bounded state and explicit limits. Current limits are 8 workspaces,
  20 launch items per normal workspace, 4 file panes, 12 tabs per pane,
  32 favorites, and 24 in-memory history entries per tab.
- Preserve transactional behavior for workspace changes and JSON writes.
  Reject invalid, duplicate, over-capacity, newer-schema, or corrupt data
  without silently truncating or overwriting it.

## User-data and system-safety invariants

- Dragging or adding a path records metadata only. It must never execute, copy,
  move, rename, or delete the dropped item.
- Removing a launch item or workspace must not delete the original user file.
- File operations inside the file manager must continue to use Windows Shell
  semantics, including confirmation, Recycle Bin, permissions, and progress.
- Startup integration may modify only the current user's `HKCU` Run value named
  `NOVA Desktop`. Tests must use the existing isolated registry mechanism and
  must not touch the user's real startup setting.
- Do not add elevation, `runas`, analytics, advertising, telemetry, network
  upload, or cloud synchronization without explicit requirements and review.
- Do not construct shell command strings from user file paths. Keep executable,
  arguments, and working directory separate when launching programs.
- Preserve `config/nova.json`, its one-time read-only migration from
  `%APPDATA%\NOVA Desktop\nova.sqlite` / INI files, and JSON backup behavior
  unless a migration is included. Keep the shared JSON available to Git;
  transient state belongs in Git-ignored `config/local.json`.

## Localization

- Every new user-visible string must include both Simplified Chinese and
  English. Use `nova_text(L"中文", L"English")` rather than branching ad hoc.
- Include menus, tooltips, empty states, dialogs, status messages, accessible
  window names, and error paths in localization work.
- Language changes must take effect immediately and persist through the
  `app/Nova/Language` setting in JSON.
- Do not translate user-created workspace or item names. The fixed first
  workspace may be displayed as `目录` / `Files` without changing its stored
  identity.
- Keep `README.md` and `README.zh-CN.md` aligned when supported features,
  requirements, commands, privacy behavior, or limitations change.

## Coding conventions

- Compile as C11 and use Unicode Win32 APIs (`...W`) for paths and UI text.
- Keep `_WIN32_WINNT` / `WINVER` compatibility at Windows 7 unless the minimum
  supported version is intentionally changed and documented.
- Maintain warning-clean builds under `-Wall -Wextra`; tests use `-Werror`.
- Match the surrounding C style when making a focused edit. For larger new
  logic, prefer small named functions, explicit ownership, early validation,
  and comments that explain constraints rather than restating code.
- Use bounded wide-string operations and check sizes before concatenation or
  formatting. Do not weaken the existing path and capacity checks.
- Keep keyboard access, DPI scaling, native focus behavior, accessible control
  names, and dark-theme drawing intact when changing UI layout or controls.

## 故障排查与日志规范

排查必须沿真实调用链取证，不能把“逐个显示文件”“没有报错”或“单测通过”直接当作根因或修复结论。

1. 先记录触发操作、预期/实际结果、影响对象、两端构建版本及复现时间。区分用户观察、已证实事实、假设和未知；先检查工作区改动与实际运行的 EXE，确认探针命中新版和原故障路径。
2. 从结果反查入口、校验/计划、队列、worker、平台调用、持久化/回执、UI 结果这条链。标明已观测与未观测的边界。对比正常与异常对象，在边界两侧核对同一操作的 ID、输入摘要、判定理由和输出，避免比较不同批次。
3. 先读本机 `config/logs/nova.log` 与 `nova.previous.log`，保留原始证据。时间为 UTC；客户端时间需先换算。同一进程用 `pid` + `op` 关联通用操作，`parent` 关联父操作；同步用 `pid` + `run`，再关联 connection/job/file 标识。跨设备分别读取相同复现时段的日志，不能把一个设备的正常结果推断成另一端正常。

   ```powershell
   Get-Content .\config\logs\nova.previous.log -Tail 300 -ErrorAction SilentlyContinue
   Get-Content .\config\logs\nova.log -Tail 300 -ErrorAction SilentlyContinue
   Select-String -Path .\config\logs\nova*.log -Pattern 'pid=1234 .*op=42(?: |$)'
   ```

4. 同步问题先区分本地哈希校验、清单/版本更新、缺失块实际传输、文件重建与确认。`sent_bytes` / `received_bytes` 是压缩内容载荷，不含元数据；`published` / `received_versions` 含删除记录。检查 `index_open` 的已有记录、`comparison` 数量、file 判定原因、repair/publish 计划及 `run_end`。删除一项时检查是否仅增加该项的删除版本，其他项的内容哈希、版本与文件时间是否保持；不能仅根据进度条声称全量重传。
5. 提出能区分机制的实验并提前写明预测；优先测“最后正常 → 最先异常”的边界。没有日志的边界先补观测。连续两次实验没有缩小范围，先复盘隐藏输入、观测盲区与竞争假设，不继续堆同类补丁。
6. 用临时目录、假回调、隔离注册表或回环 SSH 复现原操作顺序。修复前验证目标症状、修复后在同条件验证；同时覆盖相邻正常路径。同步应覆盖删除后重新保存、仅时间变化、大小/时间保持的内容修改、断点与回执。不要清缓存、重建设备身份、删除回执或关闭安全校验来掩盖故障；不写真实用户工作文件。
7. 结论说明机制复现、现场来源证据、已验证修复和仍未覆盖的实机范围。日志缺失/被轮转、旧 EXE、只测 mock 或只通过编译都属于证据限制，不能报告“已完全解决”。

所有新增/修改的重要链路必须使用 `platform/app_log` 补齐日志，已有链路不得因重构失去观测：

- 入口接受/拒绝及理由；计划与关键分支（跳过、复用、冲突、容量、权限）；排队、worker 开始、取消；平台/远端调用结果；提交、回滚及回滚失败；完成投递和 UI 消费/丢弃；关闭与资源回收。覆盖启动/迁移、JSON 加载保存备份、启动器/批量任务、文件视图导航及 Shell 操作、SSH/SFTP/终端生命周期、同步与服务器管理。
- 通用操作使用 `nova_log_begin` / `nova_log_end`，每个开始都有明确结果或等待中的后续阶段。异步请求将关联 ID 随快照/结果传递；不要仅用线程号或裸 HWND/指针作关联。Shell “调用已接受”和任务“已排队”不能写成实际文件操作/进程已经完成。
- 固定的 `component` / `event` / `action` 和机器可读 `key=value` 字段使用稳定技术标识；记录 `ok`、阶段、原始 Win32/HRESULT/SQLite/SSH/SFTP 错误码、耗时、数量与内容字节。错误码在清理前捕获；单独报告取消、拒绝、无变化、回滚与未知远端结果。界面文案仍须中英本地化。
- 只记录稳定业务 ID、索引、枚举、布尔值、计数或必要的哈希。禁止记录口令/私钥/凭据、原始命令及参数、终端/子进程输出、文件内容、配置值、主机/用户名或完整本地/远端路径。不打印原始请求/响应/SQL/异常文字；它们可能包含敏感值。
- 日志必须有界、单行、线程安全、离线；遵守现有 2 MiB × 2 文件轮转，不进入 Git/包。禁止逐字节、逐数据包、每次绘制/鼠标移动/轮询写日志；逐文件决策使用有界采样，汇总保留完整计数。禁止为日志重读文件、增加网络请求或引入后台进程。
- 日志及其辅助方法不能改变业务返回值、错误码、取消、事务、线程/资源所有权或原有重试策略。日志不可写时业务继续。测试须验证真实调用边界的关键事件及敏感字段缺席，覆盖成功、失败、取消/拒绝、回滚/恢复和异步关联；不能只检索源码中是否出现日志字符串。

## Build and test

Run commands from the repository root in PowerShell.

```powershell
.\deployment\build.bat
.\deployment\test.bat
.\deployment\test-storage.bat
.\deployment\test-files.bat
```

Additional UI coverage:

```powershell
.\deployment\test.bat --interactions
.\deployment\test.bat --desktop
```

Choose tests proportionally, but always compile after changing C code. Run:

- `deployment\test.bat` for core, launcher, drag/drop, startup, and main-window work.
- `deployment\test-storage.bat` for schema, migration, transactions, lazy loading, or
  backup changes.
- `deployment\test-files.bat` for file-manager panes, tabs, navigation, commands, Shell
  operations, layouts, favorites, or view lifecycle changes.
- `deployment\test-sync.bat` for sync persistence, queues, receipts, migrations,
  native sync UI and the local diagnostic logger. Its `diagnostics-test.exe`
  verifies real storage rollback/restore and command/task logging in temporary data.
- `deployment\test-ssh.bat` for SSH profiles, credentials and terminal rules;
  optional `python -X utf8 -B tests/ssh_fixture.py build/ssh-client-test.exe` and
  `python -X utf8 -B tests/sync_fixture.py build/sync-client-test.exe` exercise
  loopback transport, worker/UI boundaries and private-data exclusion. These
  fixtures require Paramiko and never use saved credentials or real servers.
- `--interactions` for workspace/UI command wiring; `--desktop` for window mode,
  geometry, resizing, and desktop-fence behavior.
- `deployment\test-deployment.bat` for build/deployment script changes; it uses
  a temporary Git repository, local bare remote, and compiler stand-in.

Build entry points live in `deployment/`. `deployment\build.bat` accepts an
optional repository-relative or absolute exe output path. `deploy_local.bat`
stages all non-ignored changes, commits, pushes the current branch, then builds
and packages local artifacts under ignored `build/packages/`. Creating or
editing this script does not authorize running it against the user's repository.

Tests must use temporary directories, fake callbacks, and isolated registry
state. They must not open user launch items or modify personal work files. If
`build\nova-desktop.exe` is locked by a running instance, do not terminate the
user's app automatically; link a temporary executable under the ignored
`build\` directory for verification or ask the user to close it.

Before committing, also run:

```powershell
git diff --check
```

Keep build products, local databases, logs, machine-specific files, and the
local `.agents/` directory out of version control. Never commit credentials or
personal paths.

## Documentation and release hygiene

- Update both READMEs for user-visible changes and `ARCHITECTURE.md` when module
  boundaries, ownership, persistence, or performance assumptions change.
- Keep `LICENSE`, both NOTICE files, `SECURITY.md`, and third-party attribution
  accurate. Do not claim affiliation with Microsoft or Q-Dir.
- Do not commit or publish binaries unless the release process explicitly calls
  for them and the binary was built and tested from the tagged source.
