#ifndef NOVA_SYNC_MANAGER_H
#define NOVA_SYNC_MANAGER_H
#include <windows.h>
BOOL sync_manager_open(HWND owner);
void sync_manager_startup(HWND owner);
HWND sync_manager_window(void);
void sync_manager_language_changed(void);
void sync_manager_shutdown(void);
#endif
