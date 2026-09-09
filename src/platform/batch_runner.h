#ifndef NOVA_BATCH_RUNNER_H
#define NOVA_BATCH_RUNNER_H

#include <windows.h>
#include "../core/batch_task.h"

/* Keep enough of the command tail to make failures actionable without allowing
   an unbounded child process to consume application memory. */
#define BATCH_OUTPUT_CAP 1024

enum { BATCH_EVENT_STARTED=1, BATCH_EVENT_RESULT, BATCH_EVENT_SKIPPED, BATCH_EVENT_CANCELLED };
typedef BOOL (*BatchExecute)(const wchar_t *,const wchar_t *,HANDLE,DWORD *,DWORD *,wchar_t *,DWORD);
/* Callback may run concurrently in parallel mode. It must not touch UI state. */
typedef void (*BatchProgress)(void *context,int event,int index,DWORD exit_code,DWORD error,const wchar_t *output);
typedef struct { int success,failed,skipped,cancelled; } BatchResult;
BatchResult batch_runner_task(const BatchTask *task,HANDLE cancel,BatchProgress progress,void *context,BatchExecute execute);

BOOL batch_runner_find_git(wchar_t *path,DWORD capacity,DWORD *error);
BOOL batch_runner_command(const wchar_t *text,const wchar_t *directory,HANDLE cancel,DWORD *exit_code,DWORD *error,wchar_t *output,DWORD output_capacity);

#endif
