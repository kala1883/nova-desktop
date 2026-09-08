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
- Windows, Win32, PowerShell, and related product names are trademarks of
  Microsoft Corporation. NOVA Desktop is not affiliated with or endorsed by
  Microsoft.
- Q-Dir is referenced only to describe a familiar multi-pane file-management
  workflow. NOVA Desktop contains its own implementation and is not affiliated
  with or endorsed by Q-Dir or its publisher.

## User content and privacy

NOVA Desktop stores workspace metadata and file-manager session state locally
in `%APPDATA%\NOVA Desktop\nova.sqlite`. Adding an item records its path; it
does not copy, move, upload, or claim rights in the user's files. The project
includes no analytics, advertising SDK, or cloud synchronization service.

For the Chinese version, see [NOTICE.zh-CN.md](NOTICE.zh-CN.md).
