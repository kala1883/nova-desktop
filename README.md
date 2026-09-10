# NOVA Desktop

[简体中文](README.zh-CN.md) · English

NOVA Desktop is a lightweight Windows workspace launcher and multi-pane file
manager written in C with the native Win32 API. It keeps apps, shortcuts,
files, and folders in named workspaces while providing a fixed **Files**
workspace for day-to-day file management.

No web runtime, installer, background service, or Q-Dir installation is
required.

## Highlights

- **12 resizable file-manager layouts** — four panes, two columns, two rows, a
  single pane, four three-pane arrangements, and three/four-column or row
  layouts. Drag pane dividers to resize them; proportions are restored locally.
- **Independent tabs and history** — up to 12 clearly highlighted tabs per pane,
  with separate paths and back/forward history. Press and hold a folder tab,
  then drag it to change that pane's saved tab order.
- **Native Windows file operations** — selection, sorting, filtering, context
  menus, thumbnails, clipboard operations, drag and drop, rename, delete, and
  new-folder actions use Windows Shell behavior. Cut, paste, delete, and new
  folder controls are available directly in every pane.
- **Workspace launcher** — organize up to 8 workspaces with 20 items each;
  reorder or move items with press-and-hold drag, search the current workspace,
  or launch every item once.
- **Saved batch tasks** — up to 16 named tasks, each with its own command, up to
  24 working folders, and sequential or parallel execution. Launch a selected
  task from **Settings → Batch tasks**.
- **Local session restore** — workspaces, tabs, layout, navigation-tree state,
  and favorites are stored in an embedded SQLite database.
- **Window modes** — normal, always on top, and desktop-fence mode, plus tray
  behavior and optional startup with Windows.
- **Simplified Chinese and English UI** — switch instantly from
  **Settings → Language**. The preference is restored on the next launch.

## Requirements

- Windows 7 or later
- MinGW-w64 GCC and `windres` to build from source
- PowerShell for the build and test scripts

SQLite 3.53.4 is vendored in `third_party/sqlite` and linked statically. After
the first build, no network access is needed.

## Build and run

```powershell
.\build.bat
.\build\nova-desktop.exe
```

Open directly in file-manager mode:

```powershell
.\build\nova-desktop.exe --files
```

The application stores its data in:

```text
%APPDATA%\NOVA Desktop\nova.sqlite
```

Existing `config.ini` and `file-manager.ini` data is migrated once and left in
place. A valid database is backed up as `nova.backup.sqlite` during startup.

## Using workspaces

- Drop files, folders, executables, or shortcuts into a normal workspace, or
  use **Add file** / **Add folder**. NOVA records paths only; it does not move
  or copy the original items.
- Double-click an item or press Enter to open it with Windows. Removing an item
  from NOVA does not delete the original file.
- Use the `+` button to create a workspace. Rename and delete actions are in
  **Settings**. The fixed **Files** workspace cannot be renamed or deleted.
- Press `Ctrl+K` to search the active workspace. Clear the search before
  reordering items.
- Use **Launch all** or double-click a workspace name to open a snapshot of all
  its items once. There is no recurring or background launch queue.

## File-manager shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+L` | Focus the folder/command address bar |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `Alt+Up` | Parent folder |
| `Ctrl+T` / `Ctrl+W` | New / close tab |
| `Ctrl+Tab` | Next tab |
| `F6` | Next pane |
| `F5` | Refresh |
| `Ctrl+C/X/V` | Copy / cut / paste in the native file view |
| `F2` / `Delete` | Rename / delete |
| `Ctrl+Shift+N` | New folder |

The address bar accepts normal paths, relative paths, UNC paths, `shell:`
locations, and environment variables such as `%USERPROFILE%`. Entering a
normal command such as `git status` starts `cmd.exe` with the current pane's
filesystem folder as its working directory. Prefix a command with `>` to force
command mode.

## Batch tasks

Open **Settings → Batch tasks…**. Use **New task** to create a named task,
set its command, add its working folders, and choose **Save task**. Select a
saved task in the left list and choose **Run task**. Editing, switching tasks
and closing the window save the current task; invalid edits must be corrected.
Each task keeps its own execution mode:

- **Sequential** waits for each folder to finish before starting the next.
- **Parallel** starts all folders without waiting for earlier ones to finish.

NOVA runs one saved task at a time, with up to 24 commands active in parallel.
Cancel terminates active commands and their child processes, then skips folders
that have not started. A failure shows its exit code and captured output tail;
double-click the failed row for the full captured detail. Standard input is
closed so an interactive prompt fails instead of waiting invisibly forever.
A failed folder does not stop other folders.
Use **Add to workspace…** in the task manager to place a shortcut in a normal
launcher workspace. Double-click the shortcut, press Enter, or use the
workspace's **Launch all** button. If several task shortcuts are launched,
their configurations are snapshotted and queued in order; each task retains
its own sequential/parallel folder mode. Repeated launches of an active or
queued task are ignored. Cancel task also clears queued tasks.

Task shortcuts follow the saved task by a stable ID, including after rename
or reorder. Removing a shortcut does not delete the task. Deleting a task
makes its remaining shortcuts unavailable; they never launch another task.
The fixed Files workspace cannot contain task shortcuts.

Commands use Windows cmd syntax, up to 2047 characters; for example
`git pull origin main`, `npm run build`, or `git status && git log -1`.
The existing single-task configuration becomes the first saved task.
Tasks persist in local SQLite; folder paths are passed separately as working
directories to system `cmd.exe /d /s /c`.

## Validation

```powershell
.\tests\run.bat
.\tests\run.bat --interactions
.\tests\storage.bat
.\tests\files.bat
```

The tests use temporary directories and an isolated registry location. They do
not change the real Windows startup entry or operate on personal work files.

## Current limitations

- Paths added to launcher workspaces and batch tasks are limited to 259
  characters. The file manager address field supports up to 2047 characters,
  while actual access is still subject to Windows Shell behavior.
- This is an independent implementation of a Q-Dir-style core workflow, not a
  one-for-one clone. It does not import `.qdr` sessions or reproduce every
  Q-Dir option.
- Windows Shell extensions, network devices, or thumbnail providers may delay
  a file view. Hidden views are released after an idle period, but Windows and
  third-party caches are outside NOVA's memory policy.

## Project documents

- [Architecture](ARCHITECTURE.md)
- [Design notes](DESIGN.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [Notices and rights](NOTICE.md)

## License

NOVA Desktop is available under the [MIT License](LICENSE). SQLite is public
domain software; see [`third_party/sqlite/README.md`](third_party/sqlite/README.md)
for provenance. Product names mentioned for compatibility or comparison belong
to their respective owners.
