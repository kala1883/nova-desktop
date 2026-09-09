#ifndef NOVA_BATCH_TASKS_UI_H
#define NOVA_BATCH_TASKS_UI_H

#include <windows.h>
#include "../core/batch_task.h"

#define WM_NOVA_BATCH_EVENT (WM_APP+40)
#define WM_NOVA_BATCH_SHORTCUT (WM_APP+41)
#define WM_NOVA_BATCH_CHANGED (WM_APP+42)

BOOL batch_tasks_open(HWND owner);
BOOL batch_tasks_handle_event(LPARAM parameter);
void batch_tasks_language_changed(void);
void batch_tasks_shutdown(void);
HWND batch_tasks_window(void);
BOOL batch_tasks_lookup(long long id,BatchTask *value);
BOOL batch_tasks_launch(HWND owner,long long id);
BOOL batch_tasks_busy(void);

#endif
