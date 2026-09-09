#include "batch_task.h"
#include <string.h>
#include <wctype.h>
#include <wchar.h>

static size_t normalized_length(const wchar_t *directory){
    size_t length=wcslen(directory);
    while(length>3&&(directory[length-1]==L'\\'||directory[length-1]==L'/'))length--;
    return length;
}

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
