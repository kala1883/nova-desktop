# Notices and rights

Copyright © 2026 Jie. Except for the third-party material identified below,
NOVA Desktop's source code, documentation, and project assets are available
under the [MIT License](LICENSE).

## Name and identity

The license permits redistribution and modification. It does not grant a right
to imply that a modified build is an official release or that its authors are
endorsed by the NOVA Desktop maintainers. Forks should clearly identify
material changes and avoid misleading users about their origin.

## Third-party material

- SQLite 3.53.4 is included in `third_party/sqlite` and statically linked.
  SQLite is dedicated to the public domain. Provenance and source hashes are
  recorded in `third_party/sqlite/README.md`.
- libssh2 1.11.1 (BSD-3-Clause) is vendored and statically linked. OpenSSL
  libcrypto (Apache-2.0) is statically linked from the build toolchain. Complete
  redistribution licenses are in `THIRD_PARTY_NOTICES.txt`, included in packages.
- libvterm 0.3.3 (MIT) is vendored and statically linked for the native SSH
  terminal. Provenance and hashes are in `third_party/libvterm/README.md`;
  its license and the wcwidth notice are also included in packages.
- zlib (zlib license) is statically linked from the build toolchain for sync
  payload compression; its full license is included in `THIRD_PARTY_NOTICES.txt`.
- Windows, Win32, PowerShell, and related product names are trademarks of
  Microsoft Corporation. NOVA Desktop is not affiliated with or endorsed by
  Microsoft.
- Q-Dir is referenced only to describe a familiar multi-pane file-management
  workflow. NOVA Desktop contains its own implementation and is not affiliated
  with or endorsed by Q-Dir or its publisher.

## User content and privacy

NOVA Desktop stores preferences, saved tasks, workspace metadata and file-manager
session state locally in `config/nova.json` (the repository's config folder when
run inside a checkout, or the executable's config folder for a standalone package).
Old AppData SQLite/INI files are read only during first migration and preserved.
Adding an item records its path; it
does not copy, move, upload, or claim rights in the user's files. The project
includes no analytics or advertising SDK. User-enabled synchronization stages
file content on the selected SSH server; acknowledged chunks are reclaimed.
The local native manager controls registration, folder pairing and automatic
transfers. Private identity, pair settings and chunk journals stay in Git-ignored
`config/local.json` and `config/sync/`; local historical chunks are retained.
The primary JSON is available to Git; reviewing or publishing it may disclose
saved names, paths and command text. NOVA transfers files only for requested SSH operations or synchronization pairs
the user enables.
Explicit SSH/SFTP operations transfer selected files to the chosen server and run
commands there only when requested. Passwords, key passphrases and key paths are
not saved in JSON. Save/Connect remembers login secrets and key paths in the current
user's Windows Credential Manager on this computer. Key contents are not copied;
Forget login or removing the profile deletes its saved login. Host pins remain
separate entries in the same user's Credential Manager.

For the Chinese version, see [NOTICE.zh-CN.md](NOTICE.zh-CN.md).

Transient UI state is stored separately in Git-ignored config/local.json.
