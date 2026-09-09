#ifndef NOVA_BATCH_TASKS_UI_H
#define NOVA_BATCH_TASKS_UI_H

#include <windows.h>

#define WM_NOVA_BATCH_EVENT (WM_APP+40)

BOOL batch_tasks_open(HWND owner);
BOOL batch_tasks_handle_event(LPARAM parameter);
void batch_tasks_language_changed(void);
void batch_tasks_shutdown(void);
HWND batch_tasks_window(void);

#endif
