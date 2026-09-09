#ifndef NOVA_STORAGE_H
#define NOVA_STORAGE_H
#include <windows.h>
#include "../core/batch_task.h"
#include "../core/workspace.h"
/* Single UI-thread connection; SQLite is embedded, never a service. */
BOOL store_open(const wchar_t *directory);
void store_close(void);
BOOL store_ready(void);
const wchar_t *store_error(void);
BOOL store_begin(void);
BOOL store_end(BOOL success);
BOOL store_get(const wchar_t *scope,const wchar_t *section,const wchar_t *key,const wchar_t *fallback,wchar_t *value,int capacity);
int store_int(const wchar_t *scope,const wchar_t *section,const wchar_t *key,int fallback);
BOOL store_set(const wchar_t *scope,const wchar_t *section,const wchar_t *key,const wchar_t *value);
BOOL store_set_int(const wchar_t *scope,const wchar_t *section,const wchar_t *key,int value);
BOOL store_clear(const wchar_t *scope);
long long store_new_id(void);
int store_load_workspaces(Workspace *spaces); /* -1 error, 0 new database */
BOOL store_load_items(Workspace *workspace);
BOOL store_save_workspaces(Workspace *spaces,int count,int active,BOOL pinned);
BOOL store_load_batch_task(BatchTask *task);
BOOL store_save_batch_task(const BatchTask *task);
BOOL store_load_batch_tasks(BatchTaskList *tasks);
BOOL store_save_batch_tasks(BatchTaskList *tasks);
BOOL store_backup(void);
#endif
