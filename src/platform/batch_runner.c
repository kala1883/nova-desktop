#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "batch_runner.h"
#include "../core/batch_task.h"
typedef struct {
    const BatchTask *task;
    HANDLE cancel;
    BatchProgress progress;
    void *context;
    BatchExecute execute;
    int index,state;
} DirectoryRun;
static DWORD WINAPI run_directory(void *parameter){
    DirectoryRun *run=parameter;
    if(WaitForSingleObject(run->cancel,0)==WAIT_OBJECT_0){
        run->state=BATCH_EVENT_SKIPPED;
        if(run->progress)run->progress(run->context,BATCH_EVENT_SKIPPED,run->index,0,0);
        return 0;
    }
    if(run->progress)run->progress(run->context,BATCH_EVENT_STARTED,run->index,0,0);
    DWORD code=(DWORD)-1,error=0;
    BOOL ok=run->execute(run->task->command,run->task->directories[run->index],&code,&error);
    if(!ok&&!error)error=ERROR_GEN_FAILURE;
    run->state=ok&&!code?1:2;
    if(run->progress)run->progress(run->context,BATCH_EVENT_RESULT,run->index,code,error);
    return 0;
}
BatchResult batch_runner_task(const BatchTask *task,HANDLE cancel,BatchProgress progress,void *context,BatchExecute execute){
    BatchResult result={0};
    if(!batch_task_is_valid(task))return result;
    DirectoryRun runs[BATCH_DIRECTORY_LIMIT]={0};
    HANDLE threads[BATCH_DIRECTORY_LIMIT]={0};
    for(int i=0;i<task->directory_count;i++){
        runs[i]=(DirectoryRun){task,cancel,progress,context,execute?execute:batch_runner_command,i,0};
        if(task->mode==BATCH_PARALLEL){
            threads[i]=CreateThread(NULL,0,run_directory,&runs[i],0,NULL);
            if(!threads[i]){
                DWORD error=GetLastError();runs[i].state=2;
                if(progress)progress(context,BATCH_EVENT_RESULT,i,(DWORD)-1,error);
            }
        }else run_directory(&runs[i]);
    }
    for(int i=0;i<task->directory_count;i++){
        if(threads[i]){WaitForSingleObject(threads[i],INFINITE);CloseHandle(threads[i]);}
        if(runs[i].state==BATCH_EVENT_SKIPPED)result.skipped++;
        else if(runs[i].state==1)result.success++;
        else result.failed++;
    }
    return result;
}

static BOOL absolute_folder(const wchar_t *value){
    BOOL drive=((value[0]>=L'A'&&value[0]<=L'Z')||(value[0]>=L'a'&&value[0]<=L'z'))&&value[1]==L':'&&(value[2]==L'\\'||value[2]==L'/');
    return drive||(value[0]==L'\\'&&value[1]==L'\\');
}

BOOL batch_runner_find_git(wchar_t *path,DWORD capacity,DWORD *error){
    if(error)*error=ERROR_SUCCESS;
    if(!path||capacity<2){if(error)*error=ERROR_INVALID_PARAMETER;return FALSE;}
    DWORD needed=GetEnvironmentVariableW(L"PATH",NULL,0);
    if(!needed){if(error)*error=GetLastError();return FALSE;}
    wchar_t *search=malloc((size_t)needed*sizeof(wchar_t)),*safe=calloc((size_t)needed+1,sizeof(wchar_t));
    if(!search||!safe){free(search);free(safe);if(error)*error=ERROR_NOT_ENOUGH_MEMORY;return FALSE;}
    DWORD copied=GetEnvironmentVariableW(L"PATH",search,needed);
    if(!copied||copied>=needed){DWORD code=GetLastError();free(search);free(safe);if(error)*error=code?code:ERROR_BAD_ENVIRONMENT;return FALSE;}
    wchar_t *segment=search;BOOL quoted=FALSE;
    for(wchar_t *cursor=search;;cursor++){
        if(*cursor==L'"')quoted=!quoted;
        if((*cursor==L';'&&!quoted)||!*cursor){
            wchar_t delimiter=*cursor;*cursor=0;while(*segment==L' '||*segment==L'\t'||*segment==L'"')segment++;
            wchar_t *end=segment+wcslen(segment);while(end>segment&&(end[-1]==L' '||end[-1]==L'\t'||end[-1]==L'"'))*--end=0;
            if(*segment&&absolute_folder(segment)){
                if(*safe)wcscat(safe,L";");
                wcscat(safe,segment);
            }
            if(!delimiter)break;
            segment=cursor+1;
        }
    }
    DWORD result=*safe?SearchPathW(safe,L"git.exe",NULL,capacity,path,NULL):0;free(search);free(safe);
    if(!result||result>=capacity){if(error)*error=result>=capacity?ERROR_INSUFFICIENT_BUFFER:ERROR_FILE_NOT_FOUND;path[0]=0;return FALSE;}
    return TRUE;
}

BOOL batch_runner_command(const wchar_t *text,const wchar_t *directory,DWORD *exit_code,DWORD *error){
    if(exit_code)*exit_code=(DWORD)-1;
    if(error)*error=ERROR_SUCCESS;
    if(!text||!*text||wcslen(text)>=BATCH_COMMAND_CAP||!directory||!*directory){if(error)*error=ERROR_INVALID_PARAMETER;return FALSE;}
    wchar_t git_path[MAX_PATH];
    UINT length=GetSystemDirectoryW(git_path,MAX_PATH);
    if(!length||length>MAX_PATH-9){if(error)*error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    wcscat(git_path,L"\\cmd.exe");
    DWORD attributes=GetFileAttributesW(directory);
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){if(error)*error=attributes==INVALID_FILE_ATTRIBUTES?GetLastError():ERROR_DIRECTORY;return FALSE;}
    wchar_t command[BATCH_COMMAND_CAP+MAX_PATH+32];
    int written=swprintf(command,sizeof(command)/sizeof(command[0]),L"\"%ls\" /d /s /c \"%ls\"",git_path,text);
    if(written<0||(size_t)written>=sizeof(command)/sizeof(command[0])){if(error)*error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION process={0};
    HANDLE job=CreateJobObjectW(NULL,NULL);
    if(!job){if(error)*error=GetLastError();return FALSE;}
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){if(error)*error=GetLastError();CloseHandle(job);return FALSE;}
    if(!CreateProcessW(git_path,command,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,directory,&startup,&process)){
        if(error)*error=GetLastError();
        CloseHandle(job);return FALSE;
    }
    if(!AssignProcessToJobObject(job,process.hProcess)){
        if(error)*error=GetLastError();
        TerminateProcess(process.hProcess,ERROR_ACCESS_DENIED);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);return FALSE;
    }
    if(ResumeThread(process.hThread)==(DWORD)-1){
        if(error)*error=GetLastError();
        TerminateProcess(process.hProcess,ERROR_INVALID_FUNCTION);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);return FALSE;
    }
    CloseHandle(process.hThread);
    DWORD wait=WaitForSingleObject(process.hProcess,INFINITE),code=(DWORD)-1;
    BOOL ok=wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code);
    if(!ok&&error)*error=GetLastError();
    if(exit_code)*exit_code=code;
    CloseHandle(process.hProcess);CloseHandle(job);return ok;
}
