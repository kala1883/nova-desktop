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
    long long id;
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
/* Reserved internal launcher target; never pass this scheme to Windows Shell. */
#define BATCH_TARGET_PREFIX L"nova-batch:"
int batch_task_target(long long id,wchar_t *target,size_t capacity);
long long batch_task_target_id(const wchar_t *target);

typedef struct {
    BatchTask items[BATCH_TASK_LIMIT];
    int count;
    long long active_id;
} BatchTaskQueue;
/* 1 queued, 0 already active/pending, -1 invalid/full. Snapshots configuration. */
int batch_task_queue_add(BatchTaskQueue *queue,const BatchTask *value);
int batch_task_queue_take(BatchTaskQueue *queue,BatchTask *value);
void batch_task_queue_cancel(BatchTaskQueue *queue);

#endif
