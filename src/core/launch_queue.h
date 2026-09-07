#ifndef NOVA_LAUNCH_QUEUE_H
#define NOVA_LAUNCH_QUEUE_H
#include "workspace.h"
typedef struct { AppItem items[MAX_APPS];int count,next,failed,running; } LaunchQueue;
typedef int (*LaunchCallback)(const AppItem *item,void *context);
int launch_queue_begin(LaunchQueue *queue,const Workspace *workspace);
/* At most one external launch per step. Callback success means dispatch accepted. */
int launch_queue_step(LaunchQueue *queue,LaunchCallback launch,void *context);
void launch_queue_cancel(LaunchQueue *queue);
#endif
