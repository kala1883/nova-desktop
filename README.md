# NOVA Desktop

[简体中文](README.zh-CN.md) · English

NOVA Desktop is a lightweight Windows workspace launcher and multi-pane file
manager written in C with the native Win32 API. It keeps apps, shortcuts,
files, and folders in named workspaces while providing a fixed **Files**
workspace for day-to-day file management.

No web runtime, installer, background service, or Q-Dir installation is
required.

## Highlights

- **Native device synchronization** — register up to 16 devices, publish/subscribe to 16 folders
  and manage incremental, resumable SSH transfers in **Settings → Devices and
  synchronization**. The server stages missing chunks and reclaims them after
  receipt. No Windows Python or local web service is needed; see [sync](sync/README.md).
- **Clear multi-device folder state** — Folders separates local bindings from
  available publications, shows participation and automatic scheduling separately
  from file changes, and offers per-device receipt details. Every connected device
  can edit, push and pull; batch transfers act on checked local folders.
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
- **Local runtime data and shared snapshots** — daily tasks, folders and preferences
  are saved in Git-ignored JSON. `config/nova.json` is an explicit shared snapshot;
  normal use never modifies it.
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

To carry usage data to another computer, close NOVA and explicitly export with
`deployment/config_snapshot.bat -Action Export`, review the `config/nova.json`
diff, then commit/push it (or deploy). On the destination, pull and rebuild;
close NOVA and use `deployment/config_snapshot.bat -Action Import` to merge that
snapshot into local data. A code pull alone never replaces saved local data.
Packages contain the shared snapshot, never private runtime data or identities.
Build/deploy counts and hashes describe that snapshot, not unsaved local changes.
Stored directory paths must exist on the destination computer.

Optional Make entry point, from the repository root:
`mingw32-make -f deployment/Makefile` (delegates to the same build script).

Open directly in file-manager mode:

```powershell
.\build\nova-desktop.exe --files
```

Repository executables use the root `config/` folder; standalone packages use
`config/` beside the executable. Daily data lives in Git-ignored `runtime.json`
(tasks, subtask commands/folders, workspaces, launch items, presets, saved tabs,
favorites and language). `local.json` retains private window/session state and
device identities. Startup backs up these files as `runtime.backup.json` and
`local.backup.json`; close NOVA before manually restoring them.

On the first new-version launch, an existing `nova.json` is validated and migrated
to runtime data, preserving IDs, names, commands, paths, task shortcuts and private
state. Its exact original bytes remain in `nova.migration.json`; `runtime.seed.json`
retains the initial normalized data and `runtime.base.json` tracks snapshot merges.
None of these local files enter Git or packages. If no initialized snapshot exists,
read-only migration from `%APPDATA%\NOVA Desktop` SQLite/INI files still applies.
Original legacy files stay intact. Existing runtime data always wins on later
launches, even after `nova.json` changes. Corrupt/newer runtime data is rejected;
it is never silently replaced by an older shared snapshot.

After upgrading an existing checkout, close NOVA and run:

```powershell
.\deployment\build.bat
.\deployment\config_snapshot.bat -Action Migrate
```

This first saves/validates all local data, then restores only the old migration
source `nova.json` to Git HEAD, with backups. It refuses staged/unmerged or newly
edited snapshots. Local additions remain in runtime data, relative to the HEAD
baseline. This clears old uncommitted configuration so future ordinary pulls
are not blocked by daily use. It does not commit or push. `--migrate-data` alone
also migrates without opening UI or executing tasks, but leaves Git files intact.

JSON remains UTF-8, version 1, with decimal-string 64-bit IDs, bounded validation,
atomic publication and rollback. See [configuration format](config/README.md).

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
transactional local `runtime.json` and are included only on explicit Export; review
hosts, usernames and paths before sharing the snapshot. Removing a profile never deletes server files.

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
.\deployment\test-snapshots.bat
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

## Sharing configuration snapshots

Daily saves never modify tracked `config/nova.json`. After the one-time migration,
use ordinary `git pull --ff-only` for code updates. To explicitly exchange data,
close NOVA and run:

```powershell
.\deployment\config_snapshot.bat -Action Export -Preview
.\deployment\config_snapshot.bat -Action Export
# Review, commit and push config/nova.json when you want to share it.
# On the destination, after pulling and building:
.\deployment\config_snapshot.bat -Action Import -Preview
.\deployment\config_snapshot.bat -Action Import
```

Both directions merge the last baseline, current local data and shared snapshot
by stable IDs. Independent additions survive; edits/deletions of the same entity,
incompatible orders or over-capacity results stop for review. The native loader
validates each raw document and the result before publication. Backups and the
preview remain under ignored `config/.snapshot-*/`. Preview never changes input
files. Failed publication restores exact previous bytes. Local state, credentials,
identities and synchronization caches are never imported/exported. No command
commits or pushes automatically. Export intentionally changes the tracked snapshot.

`sync_config.bat` and `resolve_config.bat` remain available for legacy/shared-snapshot
Git conflicts; they do not import into runtime data. After resolving a shared
snapshot conflict, use explicit Import. Ordinary pulls do not need those tools.

## Native device synchronization

Open **Settings → Devices and synchronization** (or `--sync`). Add each computer's
SSH public key to your server, choose its SSH connection and click **Configure
server**, then **Register this device**. Names default to the computer name;
identities are allocated automatically, up to 16 devices.

Use **Publish folder** on the source and **Subscribe** on another computer,
choosing its own local directory. Refresh shows available publications and their
source device. Saving exchanges metadata; click **Sync selected** to transfer.
**Check all** hashes every local sync folder without transferring work files.
Check the folders to **Push selected**, **Pull selected**, or synchronize both ways.
New and migrated folders keep automatic synchronization paused.

If an older server rejects a synchronization request, update it with **Server →
Configure server** using its existing paths; existing queues are retained. Folder
status shows the failure, and selecting a transfer shows its details. Double-click
a failed transfer to open the complete detail dialog. Checks are recorded too.
**Refresh** reloads server state; it does not update the server component.
**Published by** shows the registered device name, including this computer's name.
Missing publication metadata is shown as **Publisher unknown**.

The file table compares current local/server versions. Choose **Keep local
version** or **Use server version** for a conflict; changed local content is
verified and saved under private `config/sync/client/recovery/<hash>/<relative-path>`
first. **Transfers → Open recovery copies** opens these copies. A changed file or
server version invalidates a stale choice. Chunk caches and version metadata must
be retained; there is no automatic local cleanup.

The server reclaims staged chunks only after all intended subscribers acknowledge
them. Late subscribers request earlier versions again; their source devices must
push cached content when it has already been reclaimed. Offline recipients keep
pending chunks until receipt, so the quota can still fill. A new queue defaults
to `/opt/nova-sync`, a 15 GiB quota and 5 GiB free reserve.

**Server → Advanced** contains paths, quotas, existing-queue connection and
resumable migration. Configuration deploys embedded sources and preserves existing
queues; on supported Linux package managers it installs missing Python when the
SSH account is already root. It never elevates privileges. Other accounts need
Python 3.9+, SQLite JSON, zlib and writable server paths prepared by an administrator.
Windows needs no Python, scripts or extra service. See [setup, migration and limits](sync/README.md).

`deployment/test-sync.bat` covers persistence and native UI. Optional Python
fixtures cover broker fan-out, real loopback SSH, late subscription, directional
transfers, conflict choices and resumable migration using temporary data.
