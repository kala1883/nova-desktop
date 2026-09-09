#ifndef NOVA_BATCH_RUNNER_H
#define NOVA_BATCH_RUNNER_H

#include <windows.h>
#include "../core/batch_task.h"

enum { BATCH_EVENT_STARTED=1, BATCH_EVENT_RESULT, BATCH_EVENT_SKIPPED };
typedef BOOL (*BatchExecute)(const wchar_t *,const wchar_t *,DWORD *,DWORD *);
/* Callback may run concurrently in parallel mode. It must not touch UI state. */
typedef void (*BatchProgress)(void *context,int event,int index,DWORD exit_code,DWORD error);
typedef struct { int success,failed,skipped; } BatchResult;
BatchResult batch_runner_task(const BatchTask *task,HANDLE cancel,BatchProgress progress,void *context,BatchExecute execute);

BOOL batch_runner_find_git(wchar_t *path,DWORD capacity,DWORD *error);
BOOL batch_runner_command(const wchar_t *text,const wchar_t *directory,DWORD *exit_code,DWORD *error);

#endif
