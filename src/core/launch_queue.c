#include "launch_queue.h"
#include <string.h>
int launch_queue_begin(LaunchQueue *q,const Workspace *w){
    if(!q||!w||q->running||w->app_count<1||w->app_count>MAX_APPS)return 0;
    q->count=w->app_count;q->next=0;q->failed=0;q->running=1;
    memcpy(q->items,w->apps,(size_t)q->count*sizeof(AppItem));return 1;
}
int launch_queue_step(LaunchQueue *q,LaunchCallback launch,void *context){
    if(!q||!q->running||!launch)return 0;
    if(!launch(&q->items[q->next++],context))q->failed++;
    if(q->next==q->count)q->running=0;
    return q->running;
}
void launch_queue_cancel(LaunchQueue *q){if(q)q->running=0;}
