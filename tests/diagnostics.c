#define _WIN32_WINNT 0x0601
#define __USE_MINGW_ANSI_STDIO 1
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform/app_log.h"
#include "../src/platform/storage.h"
#include "../src/platform/batch_runner.h"
BOOL nova_english=FALSE;
static int calls;
static BOOL execute_fake(const wchar_t *command,const wchar_t *directory,HANDLE cancel,DWORD *code,DWORD *error,wchar_t *output,DWORD capacity){
    (void)command;(void)directory;(void)cancel;assert(capacity>32);wcscpy(output,L"NOVA_DIAGNOSTIC_PRIVATE");
    *code=calls++?7:0;*error=*code?ERROR_ACCESS_DENIED:0;return *error==0;
}
int wmain(void){
    wchar_t temporary[MAX_PATH],root[MAX_PATH],path[MAX_PATH];assert(GetTempPathW(MAX_PATH,temporary));assert(GetTempFileNameW(temporary,L"nlg",0,root));assert(DeleteFileW(root)&&CreateDirectoryW(root,NULL));assert(nova_log_init(root));
    SetLastError(12345);NovaLogSpan parent=nova_log_begin("fixture","parent",0),child=nova_log_begin("fixture","child",parent.id);
    assert(GetLastError()==12345&&parent.id!=child.id);assert(!nova_log_end(child,FALSE,ERROR_CANCELLED)&&GetLastError()==12345);assert(nova_log_end(parent,TRUE,0)&&GetLastError()==12345);
    assert(store_open(root));assert(store_set(L"app",L"Fixture",L"Private",L"NOVA_DIAGNOSTIC_PRIVATE"));
    assert(store_begin()&&store_set_int(L"app",L"Fixture",L"Value",1));assert(!store_end(FALSE));
    swprintf(path,MAX_PATH,L"%ls\\runtime.json",root);HANDLE locked=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(locked!=INVALID_HANDLE_VALUE);
    assert(store_begin()&&store_set_int(L"files",L"Manager",L"Layout",1)&&store_set_int(L"app",L"Fixture",L"Value",2));assert(!store_end(TRUE));CloseHandle(locked);
    assert(store_int(L"app",L"Fixture",L"Value",0)==0&&store_int(L"files",L"Manager",L"Layout",0)==0);assert(store_backup());store_close();
    BatchTask *task=calloc(1,sizeof(*task));assert(task);task->id=42;wcscpy(task->name,L"Diagnostic fixture");wcscpy(task->command,L"NOVA_DIAGNOSTIC_PRIVATE");
    swprintf(task->directories[0],BATCH_DIRECTORY_CAP,L"%ls\\a",root);swprintf(task->directories[1],BATCH_DIRECTORY_CAP,L"%ls\\b",root);task->directory_count=2;
    BatchResult result=batch_runner_task(task,NULL,NULL,NULL,execute_fake);assert(result.success==1&&result.failed==1);
    HANDLE cancel=CreateEventW(NULL,TRUE,TRUE,NULL);assert(cancel);result=batch_runner_task(task,cancel,NULL,NULL,execute_fake);assert(result.skipped==2&&calls==2);CloseHandle(cancel);free(task);
    DWORD exit_code=0,error=0;wchar_t output[BATCH_OUTPUT_CAP];assert(batch_runner_command(L"echo NOVA_DIAGNOSTIC_PRIVATE & exit /b 7",root,NULL,&exit_code,&error,output,BATCH_OUTPUT_CAP)&&exit_code==7&&wcsstr(output,L"NOVA_DIAGNOSTIC_PRIVATE"));
    assert(!batch_runner_command(L"",root,NULL,&exit_code,&error,output,BATCH_OUTPUT_CAP)&&error==ERROR_INVALID_PARAMETER);
    nova_log_close();swprintf(path,MAX_PATH,L"%ls\\logs\\nova.log",root);HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(file!=INVALID_HANDLE_VALUE);
    char *log=calloc(256*1024,1);DWORD n=0;assert(log&&ReadFile(file,log,256*1024-1,&n,NULL)&&n<256*1024-1);CloseHandle(file);
    const char *events[]={"event=runtime_loaded","event=publication_prepared","event=document_published","event=document_restored","event=rollback ","action=backup","event=task_plan","event=step_result","event=step_skipped","event=process_started","event=command_result"};
    for(unsigned i=0;i<sizeof(events)/sizeof(events[0]);i++)assert(strstr(log,events[i]));
    char relationship[96];snprintf(relationship,sizeof(relationship),"op=%ld parent=%ld action=child",child.id,parent.id);assert(strstr(log,relationship));
    assert(!strstr(log,"NOVA_DIAGNOSTIC_PRIVATE"));char utf[MAX_PATH*4];assert(WideCharToMultiByte(CP_UTF8,0,root,-1,utf,sizeof(utf),NULL,NULL));assert(!strstr(log,utf));
    free(log);puts("PASS real storage commit/rollback/restore, batch execution/failure/skip, command exit, operation correlation and private-value exclusion (temporary state only)");return 0;
}
