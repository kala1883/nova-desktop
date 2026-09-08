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
- `src/platform/`: Windows/SQLite integration and resource ownership.
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
- Preserve transactional behavior for workspace changes and SQLite writes.
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
- Preserve the local data location `%APPDATA%\NOVA Desktop\nova.sqlite`, the
  one-time INI migration, and backup behavior unless a migration is included.

## Localization

- Every new user-visible string must include both Simplified Chinese and
  English. Use `nova_text(L"中文", L"English")` rather than branching ad hoc.
- Include menus, tooltips, empty states, dialogs, status messages, accessible
  window names, and error paths in localization work.
- Language changes must take effect immediately and persist through the
  `app/Nova/Language` SQLite setting.
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

## Build and test

Run commands from the repository root in PowerShell.

```powershell
.\build.bat
.\tests\run.bat
.\tests\storage.bat
.\tests\files.bat
```

Additional UI coverage:

```powershell
.\tests\run.bat --interactions
.\tests\run.bat --desktop
```

Choose tests proportionally, but always compile after changing C code. Run:

- `tests\run.bat` for core, launcher, drag/drop, startup, and main-window work.
- `tests\storage.bat` for schema, migration, transactions, lazy loading, or
  backup changes.
- `tests\files.bat` for file-manager panes, tabs, navigation, commands, Shell
  operations, layouts, favorites, or view lifecycle changes.
- `--interactions` for workspace/UI command wiring; `--desktop` for window mode,
  geometry, resizing, and desktop-fence behavior.

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
