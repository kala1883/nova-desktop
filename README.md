# NOVA Desktop

[简体中文](README.zh-CN.md) · English

NOVA Desktop is a lightweight Windows workspace launcher and multi-pane file
manager written in C with the native Win32 API. It keeps apps, shortcuts,
files, and folders in named workspaces while providing a fixed **Files**
workspace for day-to-day file management.

No web runtime, installer, background service, or Q-Dir installation is
required.

## Highlights

- **Native device synchronization** — register two devices, pair up to 16 folders
  and manage incremental, resumable SSH transfers in **Settings → Devices and
  synchronization**. The server stages missing chunks and reclaims them after
  receipt. No Windows Python or local web service is needed; see [sync](sync/README.md).
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
- **Per-folder command presets** — run the default command from a pane toolbar,
  choose another saved command from its drop-down menu, or manage up to 16
  named presets. The active pane's filesystem folder is always the working
  directory.
- **Workspace launcher** — organize up to 8 workspaces with 20 items each;
  reorder or move items with press-and-hold drag, search the current workspace,
  launch every item once, or collapse the workspace sidebar into a compact rail.
- **Saved batch tasks** — up to 16 named tasks, each with up to 24 subtasks with
  individually editable working folders and commands, and sequential or parallel execution. Launch a selected
  task from **Settings → Batch tasks**.
- **JSON configuration** — workspaces, launch items, tasks, commands, tabs,
  layout, navigation-tree state, favorites and preferences are saved together
  in `config/nova.json`, which is available for Git version control.
- **Window modes** — normal, always on top, and desktop-fence mode, plus tray
  behavior and optional startup with Windows.
- **Simplified Chinese and English UI** — switch instantly from
  **Settings → Language**. The preference is restored on the next launch.

## Requirements

- Windows 7 or later
- MinGW-w64 GCC and `windres` to build from source
- OpenSSL 3 headers and static `libcrypto.a` from the same MinGW toolchain
  (MSYS2 MINGW64: `pacman -S mingw-w64-x86_64-openssl`)
- zlib headers and static `libz.a` from that toolchain (MSYS2 MINGW64 package:
  `mingw-w64-x86_64-zlib`); no zlib DLL is required at runtime.
- PowerShell for the build and test scripts

SQLite 3.53.4 is vendored in `third_party/sqlite` and linked statically. After
the first build, no network access is needed. It provides in-memory transactions,
JSON parsing and migration of older databases; new configuration is stored as JSON.

## Build and run

Build, deployment and test scripts select a complete toolchain from
`NOVA_MINGW_BIN` (if set), then `PATH`, then common MSYS2 `mingw64/bin` or
`ucrt64/bin` locations. Automatic discovery checks local drive roots and their
immediate subdirectories, plus Local AppData. For another installation location:

```powershell
$env:NOVA_MINGW_BIN = 'C:\msys64\mingw64\bin'
```

The selected directory must contain both `gcc.exe` and `windres.exe`. Scripts
adjust their own process environment; they do not change the system/user PATH.

OpenSSL is detected under the selected compiler's prefix (`include/openssl` and
`lib/libcrypto.a`), including the `opt` subdirectory used by some MinGW packages.
For a separate compatible MinGW build, set `$env:NOVA_OPENSSL_ROOT` to its prefix
(the directory containing `include` and `lib`). Build/test scripts add those paths
only to their own process environment. Deployment compiles and links a small
OpenSSL probe before staging or pushing; missing headers, a missing static library,
or an incompatible library stop deployment at that point.
Older OpenSSL releases are rejected to preserve the documented Ed25519 support
and the bundled OpenSSL license. A local prefix at `build/deps/openssl` is also
recognized when its `compiler.txt` contains the full path to the selected
`gcc.exe`; dependencies stay ignored by Git and must be prepared on each machine.

```powershell
.\deployment\build.bat
.\build\nova-desktop.exe
```

Build and test entry points are centralized in `deployment/`. Batch scripts
resolve the repository from their own location and can be called from another
working directory. An optional build argument selects an output path relative
to the repository (or an absolute path), useful when the normal exe is running:

```powershell
.\deployment\build.bat "build\nova-desktop-preview.exe"
.\deployment\test.bat --interactions
.\deployment\test-storage.bat
.\deployment\test-files.bat
```

Every successful build also updates `build/packages/nova-desktop.exe` and
`build/packages/config/nova.json`. The fixed executable replaces the previous
copy; failed builds preserve it. Close a running copy before building when possible.
If Windows permits replacement while it runs, an occupied old-executable backup
is retained for cleanup by a later build; this does not fail the build or ZIP
packaging. A failure to replace the executable itself still stops the build.

For a local commit, push, build, and portable package, run:

```powershell
.\deployment\deploy_local.bat "feat: describe the changes"
```

The message is optional; the default includes a timestamp. The script stages
**all non-ignored changes**, commits when needed, pushes the current branch to
its configured upstream (or `origin` with the same branch name), then builds
the exe and ZIP under `build/packages/`. Each package has a unique directory,
the source commit ID, both READMEs, the license, both notice files and a
`config/nova.json` copy. Binaries
remain local and ignored by Git; the script does not publish a release.
It stops on any failure. A build failure after pushing leaves the source commit
on the remote; rerunning can build it without creating an empty commit. It never
closes a running NOVA instance. Run relevant tests before deploying.

To carry usage data to another computer, close NOVA on the source computer so
pending task/session edits are saved, then deploy there to commit and push the
updated `config/nova.json`. On the destination computer, close NOVA **before**
`git pull --ff-only`, then run `deployment/build.bat` and start the new executable.
Deploy commits/pushes the current checkout; it does not pull remote updates.
The deployment/build output prints the JSON path, initialization state, task
count, per-pane tab counts and SHA256; the archive records the configuration hash
in `revision.txt`. A “no changes” deployment cannot publish another computer's
uncommitted configuration. Only directory paths are stored: the actual files
and folders must exist on the destination computer.

Optional Make entry point, from the repository root:
`mingw32-make -f deployment/Makefile` (delegates to the same build script).

Open directly in file-manager mode:

```powershell
.\build\nova-desktop.exe --files
```

When run inside this repository, the application reads and writes:

```text
config\nova.json
```

Outside the repository it uses `config/nova.json` beside the executable.
An absent JSON file or an empty `initialized: false` file imports existing configuration from
`%APPDATA%\NOVA Desktop\nova.sqlite`, or from the legacy `config.ini` and
`file-manager.ini`, on first launch; original files and their backups stay intact.
Without older data it creates the default configuration. Subsequent launches
use JSON only. All paths, names, commands and stable IDs survive migration.

JSON is UTF-8 with a versioned format. IDs are decimal strings to preserve all
64 bits. Saves atomically replace the file; invalid, duplicate, over-capacity or
newer-format data is rejected without being overwritten. Startup creates
`config/nova.backup.json`. Close NOVA before editing or restoring the JSON file.
The primary JSON is not Git-ignored; backups and temporary files are ignored.
Frequently changing state is stored separately in Git-ignored `config/local.json`:
active workspace, selected tabs, window modes, sidebar state, pane layout,
navigation-tree visibility and divider positions. Tasks, launch items, presets,
saved tab folders, favorites and language remain in the shared `nova.json`.
Older combined files are split automatically on first startup. Local values take
precedence for transient state. Startup backs up both files separately.

Configuration includes user paths and command text, so review its Git diff when
committing or deploying. See [configuration format](config/README.md).

The same file contains saved usage data: tasks and subtask folders/commands,
workspace launch items and task shortcuts, command presets, folder tabs and
favorites. These are saved when you use NOVA, not just copied from a template.
To migrate existing usage data before opening the UI, close NOVA and run
`build/packages/nova-desktop.exe --migrate-data`. This only migrates/saves data
and updates its backup; it does not run tasks or open launch items.

## Using workspaces

- Drop files, folders, executables, or shortcuts into a normal workspace, or
  use **Add file** / **Add folder**. NOVA records paths only; it does not move
  or copy the original items.
- Double-click an item or press Enter to open it with Windows. Removing an item
  from NOVA does not delete the original file.
- Use the `+` button to create a workspace. Rename and delete actions are in
  **Settings**. The fixed **Files** workspace cannot be renamed or deleted.
- Use the chevron beside the NOVA wordmark, or press `Ctrl+B`, to collapse or
  expand the workspace sidebar. The compact rail keeps workspace switching
  available, and the chosen state is restored on the next launch.
- Press `Ctrl+K` to search the active workspace. Clear the search before
  reordering items. In an unfiltered workspace, drag an icon onto another icon
  to reorder it, or onto another normal workspace to move it. Dragging starts
  after normal pointer movement or a 350 ms hold. The original file stays in
  place, and the fixed **Files** workspace cannot receive items.
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

Refreshing also resynchronizes the native file view with the pane size if the
folder list appears clipped into the upper-left corner.

The address bar accepts normal paths, relative paths, UNC paths, `shell:`
locations, and environment variables such as `%USERPROFILE%`. Entering a
normal command such as `git status` starts `cmd.exe` with the current pane's
filesystem folder as its working directory. Prefix a command with `>` to force
command mode.

Each pane also has a command split button after the file-operation controls.
Click its main area to run the default preset, or click the arrow to run a
different preset, choose the default, or open **Manage commands…**. Preset
names and commands are stored in the JSON configuration. NOVA starts with a `cmd.exe`
preset (`cd .`), which opens a command prompt in the current folder without
changing its contents.

## SSH servers

Each file pane has a **Local / Servers** mode button beside its tabs. The four
panes can use different modes. In **Server mode**, `+` and `Ctrl+T` directly list
saved servers; select one, provide its login credentials, and connect in a new
tab. An empty collection opens server management, also available as **Manage
servers…** in the picker. Cancelling creates no tab. Switching modes retains
existing tabs and connections; new local tabs use a local folder already open in
that pane, or the user folder if none exists. Modes live only in `config/local.json`, and restoring never auto-connects.

Use **Servers** in the file-manager toolbar to save a name, host, SSH port,
username and absolute initial directory, then open a remote tab with a password
or private key, including encrypted OpenSSH Ed25519 keys. **Save** and **Connect**
remember the private-key path and password/passphrase in the current user's
Windows Credential Manager on this computer. Reopening or selecting the server
fills these fields automatically; the password remains masked. Keep the original
private-key file in place: NOVA remembers its path and does not copy its contents.
Changing the host, port or username clears recalled login fields. **Forget login**,
removing the server, or saving with both login fields empty removes its saved login.
Credentials do not move with Git or a portable package; enter them once on another
computer or Windows account. Credential-save failures are reported, and failed
configuration writes restore the previous login details. Passwords, passphrases
and key paths are never written to JSON. Independently verify the SHA256 host
fingerprint on first connection. Pins use the current user's Windows Credential
Manager, keyed by host/port; changed pins block connections.
The server holds your public key; select the matching local private key, such as
`id_ed25519`, not `id_ed25519.pub`. Connect requires a password or private key;
this release does not automatically use SSH Agent or other clients' credentials.
Public keys, PPK and UTF-16 files get specific guidance, without modifying files.
Connection failures show a full error dialog and retain copyable stage, native/SSH/SFTP
error codes and details in the Terminal view.

The server needs SSH exec, SFTP and POSIX `sh`, with no NOVA agent or extra listening
port. This release targets Linux/POSIX servers. Save up to 16 profiles and use at
most four remote-tab workers; disconnect or close a tab to release its worker.
Restoring tabs never connects automatically. Profiles and remote tab paths use
shared transactional `nova.json`; review hosts, usernames and paths before
committing it. Removing a profile never deletes server files.

Remote tabs share layouts, history, favorites, refresh and parent navigation, with
case-sensitive paths. Enter absolute/relative directories, or `> command` to force
execution in the remote terminal. Plain text containing spaces also runs
remotely; a single word navigates relatively. Prefix relative paths containing
spaces with `./`; names of listed directories also navigate first. Files mode
uses the content area for the file list. The remote command button opens
**Terminal**; commands entered in the address bar also switch to Terminal and run
in its shell, including single-word commands entered as `> pwd`. If the terminal
needs to start, only the explicitly submitted command waits for startup; failure
or disconnection discards it. Commands are never automatically retried.
Disconnecting does not guarantee a remote process has stopped.

Choose **Terminal** in a remote tab, or press `Ctrl+Shift+T`, to replace the file
list with an interactive SSH terminal. It starts your server's shell in the
folder currently shown by the file view. The server must allow PTY allocation
and provide `stty`. The same shell retains `cd`, environment variables and
command history when switching back to **Files** or another tab. File navigation
and the running shell's directory remain independent after startup. ANSI colors,
UTF-8, Tab completion, arrow keys, Ctrl+C and alternate screens are supported.
While the terminal has focus, its keyboard shortcuts go to the server; use the
view buttons or `Ctrl+Shift+T` to return to file operations. Drag to select text;
`Ctrl+Shift+C` copies the selection (or visible screen), and `Ctrl+Shift+V` /
`Shift+Insert` pastes up to 8192 characters. Mouse wheel and the scrollbar browse
up to 1000 retained lines. Resizing updates the remote PTY. Closing, disconnecting,
exiting the shell or changing interface language ends the session; use **Connect**
to reopen it. Sessions and terminal contents are not saved or replayed at startup.
The terminal uses statically linked libvterm; it needs no local terminal process.

Single-item selection, context menus, Ctrl+C/X/V, F2, Delete and Ctrl+Shift+N work
remotely. Copy regular files or move files/directories within the same server.
Upload/Download transfer files/directories recursively with current-file progress;
double-clicking a regular file opens download selection. Transfers and renames
refuse existing destinations and never merge directories. Download then upload to
copy a directory. Local/remote transfers use explicit buttons; system clipboard,
drag/drop and cross-server copies are not supported yet. Unsafe Windows destination
names are rejected. Recursive transfers do not follow symlinks or reparse points.
Deletion requires permanent-delete confirmation, never uses the Windows Recycle
Bin, and does not follow symlinks. Failures may partially complete directory
operations; refresh to check. Listings are bounded at 10000 entries; recursion at
32 levels/10000 items. Limits produce errors rather than truncated views. Network
waits are normally 30 seconds; interactive terminals have no idle deadline. DNS resolution
and cancellation delays are also subject to the Windows networking stack.
Remote paths allow up to 2007 UTF-16 code units; local transfer/key paths up to 259 characters.

Builds additionally require the same MinGW toolchain's OpenSSL 3 headers and static
`libcrypto.a` (MSYS2 MINGW64: `pacman -S mingw-w64-x86_64-openssl`). libssh2 is
vendored and libcrypto is linked statically; no OpenSSL DLL is required at runtime.
Run `deployment/test-ssh.bat` for rules/storage tests and to build the protocol test.
Optionally run `python tests/ssh_fixture.py build/ssh-client-test.exe` with Python
and Paramiko installed. It uses only loopback, temporary keys, accounts and files.

## Batch tasks

Open **Settings → Batch tasks…**. The left task library groups **New task** and
**Delete task**; **Save task** is at the top right. Set the name, default command
and execution mode, then use **Add subtask** to choose a working folder.
Each new subtask copies the default command and can be edited independently.
Select a row and use **Edit** (or double-click), change its folder or command
in the editor below, then choose **Apply changes**. **Delete** removes that
subtask's configuration only. Working folders within a task must remain unique.
Legacy rows without their own command continue to inherit the default command.
Hold the left mouse button on a subtask for 350 ms, then drag up or down to
reorder it. The insertion line shows its new position; the list scrolls at its
edges. Release to save the order, or press Esc / release outside the list to
cancel. Folders, commands and results move together; sequential execution follows
the saved order. Reordering is disabled while any subtask is running.
Use **Run selected** above the subtask list to execute only the selected row,
including pending edits. Its status and captured output appear on that row;
double-click any completed row to view output, including successful commands.
Other rows keep their results. While a single row runs, you can run, edit or
delete other idle rows, or add subtasks. Single-row runs start independently,
even in sequential mode; an active row cannot be edited, deleted or started
again. The task's name, default command, execution mode and task switching stay
locked until all active rows finish. Select a separately started running row and
choose **Cancel selected** to stop only that command and its child processes;
other rows and queued tasks continue. The row shows **Cancelling…** until it
finishes, then becomes editable and runnable again. **Cancel all** stops all
active commands
and clears queued tasks. Whole-task runs keep editing and single-row runs locked.
Select a saved task and choose **Run task**. Saving, switching rows or tasks,
running, and closing the window also save pending edits; invalid edits must be corrected.
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
queued task are ignored. Cancel all also clears queued tasks.

Task shortcuts follow the saved task by a stable ID, including after rename
or reorder. Removing a shortcut does not delete the task. Deleting a task
makes its remaining shortcuts unavailable; they never launch another task.
The fixed Files workspace cannot contain task shortcuts.

Commands use Windows cmd syntax, up to 2047 characters; for example
`git pull origin main`, `npm run build`, or `git status && git log -1`.
The existing single-task configuration becomes the first saved task. Existing
task collections upgrade transactionally, retaining their IDs and commands.
Tasks persist in the JSON configuration; folder paths are passed separately as working
directories to system `cmd.exe /d /s /c`.

The title bar places **Settings** first, followed by desktop fence, pin, and
the standard window controls. Restoring from maximized centers a useful normal
window in the current monitor's work area (up to 1100 × 760 logical pixels).

## Validation

```powershell
.\deployment\test.bat
.\deployment\test.bat --interactions
.\deployment\test-storage.bat
.\deployment\test-files.bat
.\deployment\test-deployment.bat
```

The deployment test uses a temporary repository, a local bare remote, and a
compiler stand-in to check packaging and failure handling without committing
or pushing this repository.

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

## Configuration merge conflicts

For routine updates on `main`, close NOVA and run
`deployment/sync_config.bat`. It fetches `origin/main`, backs up the base, local
and remote JSON under ignored `build/config-sync/`, then fast-forwards and merges
uncommitted configuration changes. Independent additions and deletions merge;
renumbering item positions and leftover closed-tab keys do not count as edits.
Real conflicting edits stop before changing working files. `-Preview` only fetches
and creates a backed-up preview. The command does not commit, push, or touch
existing stashes; review and commit retained local changes before publishing.
Staged configuration, unfinished Git operations and diverged commits require
separate handling. Shared task or folder edits can still block ordinary `git pull`;
the local-state split only prevents transient UI state from doing so.

If pull is blocked by uncommitted `config/nova.json`, first close NOVA, then save
that file with `git stash push -m "nova config before pull" -- config/nova.json`.
Pull after the source computer has published its changes, then use
`git stash apply 'stash@{0}'` to retain the stash while restoring local edits.
If apply conflicts, use the semantic merge tool below before building or running
NOVA. Keep the stash until the merged data has been verified. Directly running
`deployment/configuration_info.ps1` now prints diagnostics; dot-sourcing it only
loads its reusable functions.

Close NOVA, then run `deployment/resolve_config.bat` to back up Git's base/local/
remote versions and generate a merged preview under ignored `build/config-merge/`.
`deployment/resolve_config.bat -Apply` applies and stages a conflict-free result;
commit the merge before pushing. It merges independently added tasks by stable ID,
even when both computers used the same Task1 index, and preserves task shortcuts.
Conflicting edits/deletion of the same task or over-capacity data require review;
it never forces a side or truncates entries. Runtime `local.json` is not synchronized.

## Native device synchronization

Open **Settings → Devices and synchronization** (or `--sync`) to register A/B,
pair folders and run transfers. New and migrated pairs start paused. On B, select
the existing synchronization group and B's own local path. Keep local chunk
caches after cloud acknowledgement; no automatic local cache cleanup is provided.
See [setup, migration and limits](sync/README.md). `deployment/test-sync.bat`
checks persistence and native UI; its optional loopback fixture verifies actual
SSH framing, incremental reuse and interruption recovery without user data.

The native manager's **Servers** tab now includes one-click deployment from
embedded EXE resources, verified connection, resumable migration and source
restoration before cutover. A new computer needs only NOVA, its own SSH login,
device registration and folder pairing through the UI. No manual scripts or
Windows Python are required. Linux SSH/SFTP, Python 3.9+ with SQLite JSON, and
write permission to the chosen server directory are prerequisites. Migration
retains queue/history/identities, freezes the old queue and advertises the
destination to offline peers; see [the workflow](sync/README.md).
