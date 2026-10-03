#ifndef NOVA_FILE_MANAGER_H
#define NOVA_FILE_MANAGER_H
#include <windows.h>
BOOL file_manager_migrate_settings(const wchar_t *legacy_directory);
/* Uses the caller's STA/OLE apartment. No secondary process or Q-Dir dependency. */
HWND file_manager_open(HWND owner, const wchar_t *settings_directory);
void file_manager_hide(void);
void file_manager_update_visibility(void);
BOOL file_manager_message(MSG *message);
void file_manager_close(void);
#endif
