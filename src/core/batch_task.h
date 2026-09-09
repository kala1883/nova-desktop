#ifndef NOVA_BATCH_TASK_H
#define NOVA_BATCH_TASK_H

#include <wchar.h>

#define BATCH_DIRECTORY_LIMIT 24
#define BATCH_DIRECTORY_CAP 260
#define BATCH_COMMAND_CAP 2048
#define BATCH_TASK_LIMIT 16
#define BATCH_NAME_CAP 64
#define BATCH_SEQUENTIAL 0
#define BATCH_PARALLEL 1

typedef struct {
    wchar_t name[BATCH_NAME_CAP];
    int mode;
    wchar_t command[BATCH_COMMAND_CAP];
    int directory_count;
    wchar_t directories[BATCH_DIRECTORY_LIMIT][BATCH_DIRECTORY_CAP];
} BatchTask;

typedef struct {
    int count;
    BatchTask tasks[BATCH_TASK_LIMIT];
} BatchTaskList;

/* Returns 1 when added, 0 for a duplicate, -1 for invalid/full/overlong input. */
int batch_task_add_directory(BatchTask *task,const wchar_t *directory);
/* Returns 1 when removed and 0 for an invalid index. */
int batch_task_remove_directory(BatchTask *task,int index);
int batch_task_is_valid(const BatchTask *task);

#endif
