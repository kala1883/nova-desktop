#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
enum { RUN_NONE, RUN_SUCCEEDED, RUN_FAILED, RUN_SKIPPED, RUN_CANCELLED };

typedef struct {
    HANDLE read;
    char tail[8192];
    DWORD length;
} OutputReader;

static BOOL cancellation_requested(HANDLE cancel){return cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0;}

static DWORD WINAPI read_output(void *parameter){
    OutputReader *reader=parameter;
    char chunk[1024];DWORD count=0;
    while(ReadFile(reader->read,chunk,sizeof(chunk),&count,NULL)&&count){
        DWORD discard=reader->length+count>sizeof(reader->tail)?reader->length+count-(DWORD)sizeof(reader->tail):0;
        if(discard){memmove(reader->tail,reader->tail+discard,reader->length-discard);reader->length-=discard;}
        memcpy(reader->tail+reader->length,chunk,count);reader->length+=count;
    }
    return 0;
}

static void clean_output(wchar_t *value){
    wchar_t *read=value,*write=value;BOOL spaced=TRUE;
    while(*read){
        wchar_t ch=*read++;
        if(ch==L'\r'||ch==L'\n'||ch==L'\t'||ch<L' '){if(!spaced)*write++=L' ';spaced=TRUE;}
        else{*write++=ch;spaced=FALSE;}
    }
    while(write>value&&write[-1]==L' ')write--;
    *write=0;
}

static void decode_output(const OutputReader *reader,wchar_t *output,DWORD capacity){
    if(!output||!capacity)return;
    output[0]=0;if(!reader->length)return;
    const char *bytes=reader->tail;int length=(int)reader->length,skip=0;
    int needed=0;
    int attempts=reader->length==sizeof(reader->tail)?4:1;
    for(skip=0;skip<attempts&&skip<length;skip++){
        needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes+skip,length-skip,NULL,0);
        if(needed)break;
    }
    UINT code_page=CP_UTF8;DWORD flags=MB_ERR_INVALID_CHARS;
    if(!needed){skip=0;code_page=CP_OEMCP;flags=0;needed=MultiByteToWideChar(code_page,flags,bytes,length,NULL,0);}
    if(!needed)return;
    wchar_t decoded[8193];
    int written=MultiByteToWideChar(code_page,flags,bytes+skip,length-skip,decoded,(int)(sizeof(decoded)/sizeof(decoded[0]))-1);
    if(!written&&code_page==CP_UTF8)written=MultiByteToWideChar(CP_OEMCP,0,bytes+skip,length-skip,decoded,(int)(sizeof(decoded)/sizeof(decoded[0]))-1);
    if(written<=0)return;
    decoded[written]=0;
    const wchar_t *tail=decoded;
    if((DWORD)written>=capacity)tail=decoded+written-(capacity-1);
    lstrcpynW(output,tail,(int)capacity);
    clean_output(output);
}

static DWORD WINAPI run_directory(void *parameter){
    DirectoryRun *run=parameter;
    if(cancellation_requested(run->cancel)){
        run->state=RUN_SKIPPED;
        if(run->progress)run->progress(run->context,BATCH_EVENT_SKIPPED,run->index,0,0,NULL);
        return 0;
    }
    if(run->progress)run->progress(run->context,BATCH_EVENT_STARTED,run->index,0,0,NULL);
    DWORD code=(DWORD)-1,error=0;
    wchar_t output[BATCH_OUTPUT_CAP]={0};
    BOOL ok=run->execute(run->task->command,run->task->directories[run->index],run->cancel,&code,&error,output,BATCH_OUTPUT_CAP);
    if(!ok&&!error)error=ERROR_GEN_FAILURE;
    if(error==ERROR_CANCELLED){
        run->state=RUN_CANCELLED;
        if(run->progress)run->progress(run->context,BATCH_EVENT_CANCELLED,run->index,code,error,output);
    }else{
        run->state=ok&&!code?RUN_SUCCEEDED:RUN_FAILED;
        if(run->progress)run->progress(run->context,BATCH_EVENT_RESULT,run->index,code,error,output);
    }
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
                if(progress)progress(context,BATCH_EVENT_RESULT,i,(DWORD)-1,error,NULL);
            }
        }else run_directory(&runs[i]);
    }
    for(int i=0;i<task->directory_count;i++){
        if(threads[i]){WaitForSingleObject(threads[i],INFINITE);CloseHandle(threads[i]);}
        if(runs[i].state==RUN_SKIPPED)result.skipped++;
        else if(runs[i].state==RUN_CANCELLED)result.cancelled++;
        else if(runs[i].state==RUN_SUCCEEDED)result.success++;
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

BOOL batch_runner_command(const wchar_t *text,const wchar_t *directory,HANDLE cancel,DWORD *exit_code,DWORD *error,wchar_t *output,DWORD output_capacity){
    if(exit_code)*exit_code=(DWORD)-1;
    if(error)*error=ERROR_SUCCESS;
    if(output&&output_capacity)output[0]=0;
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
    SECURITY_ATTRIBUTES inherit={sizeof(inherit),NULL,TRUE};
    HANDLE output_read=NULL,output_write=NULL,input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(input==INVALID_HANDLE_VALUE||!CreatePipe(&output_read,&output_write,&inherit,0)||!SetHandleInformation(output_read,HANDLE_FLAG_INHERIT,0)){
        DWORD code=GetLastError();if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);if(output_read)CloseHandle(output_read);if(output_write)CloseHandle(output_write);if(error)*error=code;return FALSE;
    }
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=input;startup.hStdOutput=output_write;startup.hStdError=output_write;
    PROCESS_INFORMATION process={0};OutputReader reader={output_read,{0},0};HANDLE reader_thread=NULL;
    HANDLE job=CreateJobObjectW(NULL,NULL);
    if(!job){if(error)*error=GetLastError();CloseHandle(input);CloseHandle(output_read);CloseHandle(output_write);return FALSE;}
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){if(error)*error=GetLastError();CloseHandle(job);CloseHandle(input);CloseHandle(output_read);CloseHandle(output_write);return FALSE;}
    if(!CreateProcessW(git_path,command,NULL,NULL,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,directory,&startup,&process)){
        if(error)*error=GetLastError();
        CloseHandle(job);CloseHandle(input);CloseHandle(output_read);CloseHandle(output_write);return FALSE;
    }
    CloseHandle(input);CloseHandle(output_write);output_write=NULL;
    reader_thread=CreateThread(NULL,0,read_output,&reader,0,NULL);
    if(!reader_thread){if(error)*error=GetLastError();TerminateProcess(process.hProcess,ERROR_NOT_ENOUGH_MEMORY);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);CloseHandle(output_read);return FALSE;}
    if(!AssignProcessToJobObject(job,process.hProcess)){
        if(error)*error=GetLastError();
        TerminateProcess(process.hProcess,ERROR_ACCESS_DENIED);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);WaitForSingleObject(reader_thread,INFINITE);CloseHandle(reader_thread);CloseHandle(output_read);return FALSE;
    }
    if(ResumeThread(process.hThread)==(DWORD)-1){
        if(error)*error=GetLastError();
        TerminateProcess(process.hProcess,ERROR_INVALID_FUNCTION);CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);WaitForSingleObject(reader_thread,INFINITE);CloseHandle(reader_thread);CloseHandle(output_read);return FALSE;
    }
    CloseHandle(process.hThread);
    HANDLE waits[2]={process.hProcess,cancel};
    DWORD wait=cancel?WaitForMultipleObjects(2,waits,FALSE,INFINITE):WaitForSingleObject(process.hProcess,INFINITE),code=(DWORD)-1;
    BOOL was_cancelled=cancel&&wait==WAIT_OBJECT_0+1;
    if(was_cancelled){
        TerminateJobObject(job,ERROR_CANCELLED);WaitForSingleObject(process.hProcess,5000);if(error)*error=ERROR_CANCELLED;
    }
    BOOL ok=!was_cancelled&&wait==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&code);
    if(!ok&&!was_cancelled&&error)*error=wait==WAIT_FAILED?GetLastError():ERROR_GEN_FAILURE;
    if(exit_code)*exit_code=code;
    CloseHandle(process.hProcess);CloseHandle(job);
    WaitForSingleObject(reader_thread,INFINITE);CloseHandle(reader_thread);decode_output(&reader,output,output_capacity);CloseHandle(output_read);return ok;
}
