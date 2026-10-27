# NOVA Desktop — incumbent UI refinement

Native Win32 desktop launcher; an Operate surface. Preserve the existing dark desktop identity while prioritizing scanability, familiar Windows interaction and low idle cost.

- Background `#0e131d`; sidebar / controls `#161d2a`; primary text `#ecf1f9`; secondary text `#a7b5ca`; primary action `#354d7a`.
- The NOVA app mark is a dark-navy rounded-square icon with a four-beam indigo/ice-blue nova and white core. Its multi-resolution ICO ships at 16, 20, 24, 32, 40, 48, 64, 128 and 256 px and is shared by the executable, window class, taskbar and tray.
- Microsoft YaHei UI body at 15 logical px; 12 px status; 32 px workspace title. Segoe UI wordmark at 29 px.
- The workspace sidebar expands to 220 logical px and collapses to a 64 px navigation rail. A drawn chevron beside the NOVA wordmark, plus `Ctrl+B`, toggles the state and persists it locally. The compact rail retains the native workspace list with centered initials, selection, keyboard order and accessible full names; main content reflows into the recovered width. Workspace zero is the immutable system workspace `目录`; selecting it always shows the embedded file manager, and rename, delete, bulk launch and launcher-item drops cannot target it. A drawn plus button beside the workspace heading creates ordinary editable workspaces. Expanded content begins at 254 px.
- The workspace title stands alone without instructional subtitle copy. Search and add controls form the compact action row below it; a single primary `全部启动` action sits at the title's far right. Bulk launch snapshots the current workspace and invokes the same open path as an individual double click exactly once per item; it has no delayed background queue and rejects re-entrant clicks while running.
- File identity uses Shell icons. Native list view handles selection, scrolling, keyboard navigation, tooltips and empty state. No permanent decorative animation or full-screen back buffer.
- File-manager commands use one compact row of Windows `Segoe MDL2 Assets` icons with verified Microsoft glyph assignments. Pane navigation and tab actions use the same optically centered 16 px icon language, subtle default surfaces and brighter hover/pressed states; every icon retains a Chinese accessible name, keyboard focus and a tooltip containing its action and shortcut.
- Each sufficiently wide pane places a 48 px command split button after the file-operation group. Its main area runs the persisted default preset in the active pane's filesystem directory; the chevron opens a native menu to run another preset, change the default or open the modeless command manager. Narrow panes keep the command actions in their localized overflow menu. The manager supports up to 16 uniquely named commands, preserves user-authored names, validates before every transactional JSON save and never runs a command while editing or deleting a preset.
- Each file pane owns independent directory tabs and history. Its address field accepts absolute, relative, shell and environment-variable paths; non-path input runs through `cmd.exe` with that pane's filesystem directory as `lpDirectory`, while `>` explicitly forces command mode. Ambiguous absolute/path-shaped input fails as navigation instead of being executed.
- Pressing and holding a directory tab for 350 ms starts an in-pane reorder gesture. A bright insertion marker follows the pointer; release commits the new tab order and session save, while Escape, capture loss, or release outside the tab strip cancels without moving filesystem content. Cut, paste, delete, and new-folder controls live in each pane's navigation row so their target scope is explicit; narrow panes collapse navigation into a localized overflow menu while keeping those four file actions visible.
- Selected directory tabs use a filled primary-color state with a bright top accent so the current folder remains obvious in every pane. Pane dividers expose the standard horizontal or vertical resize cursor, enforce useful minimum pane sizes, and persist each layout's proportions through the debounced local JSON session save.
- Double click or Enter launches; drag-in only imports. Feedback reports added/skipped counts; destructive workspace actions require confirmation.
- Settings opens a Batch tasks manager with a left list of saved named tasks and a right editor for name, command, execution mode, and folders. New task / Delete task act on the collection; Save task and Run task act on the selected entry. Up to 16 tasks retain independent commands, folders and sequential/parallel mode. Whole-task runs disable editing and task switching. Single-row runs keep idle subtasks editable and runnable while locking task switching and task-wide fields. Status and cancellation remain visible; cancellation stops the active command tree as well as pending folders. Failed rows include captured command output, expose the detail in the status line when selected, and open the complete captured detail on double-click. Native dark headers, readable empty states and a full-width status line preserve the incumbent palette at the minimum size and in English.
- In unfiltered ordinary workspaces, left-button down on a launcher immediately captures the pointer. Crossing the native Windows drag threshold starts dragging at once; holding still for 350 ms is an alternate way to begin. The state machine tracks the received down/up messages instead of polling global mouse state, tolerates natural pointer jitter, then applies the native drop highlight and move cursor. Releasing over another icon reorders, releasing over an ordinary workspace moves, and Escape cancels.
- System-DPI-aware scaling. Minimum window 780 × 600 logical px; launch maximized. Native focus indication is retained.

Pinning changes only the HWND_TOPMOST band, preserving window geometry and resizing. Desktop-fence mode is a separate, persistent form implemented as a same-process top-level tool window kept at the bottom of the normal application Z order. It never uses cross-process `SetParent`, which avoids black or missing composition surfaces on Windows 11. The mode uses a useful compact default size, forces a full frame/child redraw, verifies that paint completed, and restores the previous floating placement on exit or failure. Entering either mode exits the other. Icon buttons have accessible names, tooltips, keyboard focus, hover feedback and persistent highlighted states.

Caption controls share a single 30 logical px title bar: settings, desktop fence, pin, minimize, maximize/restore, close, aligned flush right in 46 px cells. Use 1 logical px strokes, neutral dark hover, red close hover; leave the workspace header free of window controls. Caption blank space supports native drag and double-click maximize through hit testing.

Validation: inspect the rendered Windows surface and keyboard/control names; core import/persistence/startup tests use isolated state, and window integration verifies topmost on/off without changing geometry.

SSH servers use an on-demand Servers toolbar button. The native dialog saves up
to 16 profiles and opens a remote tab in the active pane. Tabs identify the server
and folder; restoring tabs never connects automatically. A single-selection native
SFTP list fills Files mode, with explicit connection,
upload/download, current-file progress and cancellation controls that wrap in
narrow panes. Remote commands always target their own tab and never invoke local
Windows presets. Terminal status identifies the server; operation errors remain
copyable there. Permanent remote deletion requires confirmation; transfers refuse
existing destinations. First-use host trust is a separate default-deny fingerprint
confirmation. Save/Connect remembers each server's key path and masked
password/passphrase in the current Windows user's local Credential Manager.
Reopening or switching servers restores those fields; changing host, port or user
clears recalled credentials. Forget login sits below New/Remove in the server
library. Key files stay in place; secrets and key paths never enter shared settings.
Credential-write failures remain visible, and a failed JSON save restores the
previous login record.

Each pane's tab strip includes a Local / Servers mode menu beside the add-tab
button. Server mode highlights the selector and makes `+` / Ctrl+T open a native
saved-server picker, followed by authentication for the chosen profile. Cancellation
does not create a tab. Existing tabs survive mode switches; local mode creates a
local tab even when a remote tab is currently selected. Narrow panes use a mode
icon and expose tab closing in the mode menu. Mode preferences are local-only.

Batch tasks expose an Add to workspace action in the header. A native popup lists only ordinary workspaces. Task launch items use the NOVA icon, their current saved name, and a tooltip with command, mode and folder count. Double-click, Enter and Launch all share the internal task dispatcher. Launch all distinguishes accepted asynchronous tasks from completed app launches; duplicate active/pending task requests are ignored. Removing a task shortcut leaves the saved task intact.

Run selected and Cancel selected sit above the subtask table alongside Add subtask, Edit and Delete. Cancel selected is enabled only for a separately started active row, changes to Cancelling during its cancellation, and stops that row without cancelling other rows or clearing the task queue. After completion the row becomes editable and runnable again. Run selected requires a selected idle row, saves pending edits, then runs only that row. During single-row execution, other idle rows remain runnable, editable and deletable, and subtasks can be added. Each run starts independently of the task's sequential/parallel setting. Active rows reject edits, deletion and duplicate launches. Name, default command, mode and task switching stay locked until all runs finish; whole-task execution locks all editing and single-row launches. Cancel all at the bottom stops every active command and clears the task queue. Status and captured output stay on the source row, including after other rows are deleted. Completed rows (success, failure or cancellation) open output on double-click; the explicit Edit action remains available for editing them. Other unchanged rows retain their results.

Batch task redesign: a 232 logical px task library groups New task above the list and Delete task below it. Save task and Add to workspace sit in the detail header. Name, execution mode and a default command precede a Folder / Command / Status table, whose Run selected / Cancel selected / Add subtask / Edit / Delete actions sit immediately above it. The inline editor below the table changes the selected folder and its independent command; Apply changes commits both transactionally. Pending row edits are also validated before selection changes, save, run and close. New rows copy the default command; legacy rows with empty overrides inherit it. The native window starts at 1120 x 800 and fits the monitor work area, with a 1000 x 720 logical minimum. Maximize/restore returns the main window to a centered size capped at 1100 x 760 logical pixels and the current work area.

Subtask rows support a 350 ms left-button hold followed by vertical dragging, with a bright insertion line and edge scrolling. Release saves the reordered folders and commands transactionally and moves their results and selection together. Escape, capture loss, focus loss or an outside drop cancels. Sorting stays disabled during execution so worker row indices remain stable.

Remote tabs offer adjacent Files / Terminal view buttons, with a filled primary
selection and Ctrl+Shift+T switching. Files dedicates the content area to its list.
The command button and address-bar commands open the terminal view. Terminal mode
replaces the list with a full-height native Consolas terminal in the existing dark
palette; Connect and Disconnect remain visible. Narrow panes wrap these actions.
The shell starts in the current file-view directory and survives view/tab changes.
Terminal keys go to the PTY; Ctrl+Shift+C/V handle text copy/paste. Selection,
scrollback and copying remain available after disconnection. An explicit status
explains exit or connection failure, and reconnect never replays commands.

## 设备与同步

首次打开按“配置服务器 → 注册本设备 → 发布或订阅目录”前进，成功后直接进入下一步。
本机名称预填电脑名，身份自动分配。技术性的设备位不展示；服务器路径、配额、已有
队列连接与迁移收进“高级设置”。服务器选择与检查仍不修改当前连接，切换仍验证身份
及迁移祖先链。公钥准备仅在服务器配置上下文出现一次。

日常默认进入同步目录，以“本机目录 / 可订阅目录”切换视图。可订阅目录不展示勾选框，
选择后只提供订阅入口。本机目录分列展示最近文件比较、本机发布/订阅关系和自动同步
开关；冲突、等待补发优先出现在行摘要，选中详情保留全部差异计数。检查全部覆盖本机
所有目录；批量范围标明勾选数量，推送、拉取及双向同步明确命名“勾选项”，零勾选禁用。
编辑、断开本机、自动开关和接入重试属于当前目录；后台及批量任务不跳走用户的选择，
编辑期间不调度自动任务。编辑表单隐藏目录表，有独立取消入口，排除规则与删除传播按需展开。

详情以“文件差异 / 参与设备”切换：文件仅展示有差异的条目及版本来源设备，版本选择
只针对冲突启用。参与设备展示绑定路径及服务器回执：无待接收、待接收或等待源设备补发。
它不推测设备在线状态、自动设置或尚未推送的本机修改。发布者表示最初创建目录的设备，
不表示独占写权限；所有已接入设备都可推送和拉取。断开仅取消本机绑定，保留文件、
其他设备和发布信息。底部反馈进度与失败；传输记录标明检查、接入、推送、拉取等操作。

逐文件哈希阶段明确显示“校验本机文件（无传输）”，上传/下载阶段及实际内容字节数独立显示，避免把全目录检查误解为全量传输。

保存只登记元数据，新建目录默认暂停自动同步。冲突选择先校验并保留本机恢复副本，
版本已变化时拒绝旧选择；接收删除仍保留系统确认及回收站语义。恢复副本有内容时，
传输记录页提供打开入口。关闭管理窗口只隐藏，退出 NOVA 才停止任务。所有名称、
按钮、状态与错误中英即时切换，用户设备/目录名称及未保存输入保持原文。
