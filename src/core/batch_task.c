#include "batch_task.h"
#include <string.h>
#include <wctype.h>
#include <wchar.h>
#include <limits.h>
#include <stdio.h>

int batch_task_target(long long id,wchar_t *target,size_t capacity){
    if(id<=0||!target||capacity<30)return 0;
    int n=swprintf(target,capacity,L"nova-batch:%lld",id);return n>0&&(size_t)n<capacity;
}
long long batch_task_target_id(const wchar_t *target){
    if(!target||wcsncmp(target,BATCH_TARGET_PREFIX,11)!=0)return 0;
    const wchar_t *p=target+11;long long id=0;if(!*p)return 0;
    while(*p){if(*p<L'0'||*p>L'9'||id>(LLONG_MAX-(*p-L'0'))/10)return 0;id=id*10+(*p++-L'0');}
    return id;
}

static size_t normalized_length(const wchar_t *directory){
    size_t length=wcslen(directory);
    while(length>3&&(directory[length-1]==L'\\'||directory[length-1]==L'/'))length--;
    return length;
}

int batch_task_queue_add(BatchTaskQueue *queue,const BatchTask *value){
    if(!queue||!batch_task_is_valid(value)||value->id<=0||!value->directory_count||!value->command[0]||queue->count<0||queue->count>BATCH_TASK_LIMIT)return -1;
    if(queue->active_id==value->id)return 0;
    for(int i=0;i<queue->count;i++)if(queue->items[i].id==value->id)return 0;
    if(queue->count+(queue->active_id!=0)>=BATCH_TASK_LIMIT)return -1;
    queue->items[queue->count++]=*value;return 1;
}
int batch_task_queue_take(BatchTaskQueue *queue,BatchTask *value){
    if(!queue||!value||queue->active_id||queue->count<1||queue->count>BATCH_TASK_LIMIT)return 0;
    *value=queue->items[0];queue->active_id=value->id;
    memmove(&queue->items[0],&queue->items[1],(size_t)(--queue->count)*sizeof(BatchTask));return 1;
}
void batch_task_queue_cancel(BatchTaskQueue *queue){if(queue)queue->count=0;}

static int same_directory(const wchar_t *left,const wchar_t *right){
    while(*left&&*right){if(towlower(*left++)!=towlower(*right++))return 0;}return *left==*right;
}

int batch_task_add_directory(BatchTask *task,const wchar_t *directory){
    if(!task||!directory||task->directory_count<0||task->directory_count>BATCH_DIRECTORY_LIMIT)return -1;
    size_t length=normalized_length(directory);
    if(!length||length>=BATCH_DIRECTORY_CAP||task->directory_count==BATCH_DIRECTORY_LIMIT)return -1;
    wchar_t normalized[BATCH_DIRECTORY_CAP];
    wmemcpy(normalized,directory,length);normalized[length]=0;
    for(int i=0;i<task->directory_count;i++)if(same_directory(task->directories[i],normalized))return 0;
    wcscpy(task->directories[task->directory_count++],normalized);return 1;
}

int batch_task_remove_directory(BatchTask *task,int index){
    if(!task||task->directory_count<0||task->directory_count>BATCH_DIRECTORY_LIMIT||index<0||index>=task->directory_count)return 0;
    memmove(&task->directories[index],&task->directories[index+1],(size_t)(task->directory_count-index-1)*sizeof(task->directories[0]));
    task->directory_count--;task->directories[task->directory_count][0]=0;return 1;
}

int batch_task_is_valid(const BatchTask *task){
    if(!task||task->directory_count<0||task->directory_count>BATCH_DIRECTORY_LIMIT)return 0;
    if(task->mode!=BATCH_SEQUENTIAL&&task->mode!=BATCH_PARALLEL)return 0;
    if(!wmemchr(task->name,0,BATCH_NAME_CAP))return 0;
    if(!wmemchr(task->command,0,BATCH_COMMAND_CAP))return 0;
    for(int i=0;i<task->directory_count;i++){
        if(!wmemchr(task->directories[i],0,BATCH_DIRECTORY_CAP))return 0;
        size_t length=wcslen(task->directories[i]);if(!length||length>=BATCH_DIRECTORY_CAP)return 0;
        for(int j=0;j<i;j++)if(same_directory(task->directories[i],task->directories[j]))return 0;
    }
    return 1;
}
