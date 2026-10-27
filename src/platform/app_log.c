#define _WIN32_WINNT 0x0601
#define __USE_MINGW_ANSI_STDIO 1
#include "app_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

/* A process-wide lock covers append and rotation across UI and worker threads.
   No persistent file handle, heap allocation, helper thread or network output. */
#define LOG_PATH_CAP 2048
#define LOG_LIMIT (2*1024*1024)
static SRWLOCK log_lock=SRWLOCK_INIT;
static wchar_t log_path[LOG_PATH_CAP],backup_path[LOG_PATH_CAP];
static LONG operation_sequence;
NovaLogSpan nova_log_begin(const char *component,const char *action,LONG parent){
    DWORD saved=GetLastError();NovaLogSpan span={InterlockedIncrement(&operation_sequence),GetTickCount64(),component,action};
    nova_log_write(NOVA_LOG_INFO,component,"op_begin",L"op=%ld parent=%ld action=%hs",span.id,parent,action);
    SetLastError(saved);return span;
}
BOOL nova_log_end(NovaLogSpan span,BOOL ok,DWORD code){
    nova_log_write(ok?NOVA_LOG_INFO:code==ERROR_CANCELLED?NOVA_LOG_WARN:NOVA_LOG_ERROR,span.component,"op_end",
        L"op=%ld action=%hs ok=%d code=%lu elapsed_ms=%llu",span.id,span.action,ok,code,(unsigned long long)(GetTickCount64()-span.started));
    return ok;
}
static BOOL plain_file(const wchar_t *path){
    DWORD a=GetFileAttributesW(path);
    if(a==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    return !(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
BOOL nova_log_init(const wchar_t *directory){
    DWORD saved=GetLastError();BOOL ok=FALSE;
    AcquireSRWLockExclusive(&log_lock);
    log_path[0]=backup_path[0]=0;
    wchar_t folder[LOG_PATH_CAP];
    if(directory&&directory[0]&&wcslen(directory)<LOG_PATH_CAP-32){
        swprintf(folder,LOG_PATH_CAP,L"%ls\\logs",directory);
        if(CreateDirectoryW(folder,NULL)||GetLastError()==ERROR_ALREADY_EXISTS){
            DWORD a=GetFileAttributesW(folder);
            if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT)){
                swprintf(log_path,LOG_PATH_CAP,L"%ls\\nova.log",folder);
                swprintf(backup_path,LOG_PATH_CAP,L"%ls\\nova.previous.log",folder);
                ok=plain_file(log_path)&&plain_file(backup_path);
            }
        }
    }
    if(!ok)log_path[0]=backup_path[0]=0;
    ReleaseSRWLockExclusive(&log_lock);SetLastError(saved);return ok;
}
void nova_log_close(void){
    DWORD saved=GetLastError();AcquireSRWLockExclusive(&log_lock);
    log_path[0]=backup_path[0]=0;
    ReleaseSRWLockExclusive(&log_lock);SetLastError(saved);
}
static void single_line(char *s){
    for(;*s;s++)if((unsigned char)*s<32||*s==127)*s=' ';
}
void nova_log_write(NovaLogLevel level,const char *component,const char *event,const wchar_t *format,...){
    DWORD saved=GetLastError();
    wchar_t message[1024]={0};va_list args;va_start(args,format);
    int formatted=vswprintf(message,1024,format,args);va_end(args);message[1023]=0;
    if(formatted<0)wcscpy(message,L"record_truncated=1");
    char utf[4096],record[4608],source[48],name[64];
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,message,-1,utf,sizeof(utf),NULL,NULL)){SetLastError(saved);return;}
    snprintf(source,sizeof(source),"%s",component?component:"app");snprintf(name,sizeof(name),"%s",event?event:"event");
    single_line(utf);single_line(source);single_line(name);
    SYSTEMTIME now;GetSystemTime(&now);
    const char *severity=level==NOVA_LOG_ERROR?"ERROR":level==NOVA_LOG_WARN?"WARN":"INFO";
    int length=snprintf(record,sizeof(record),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu level=%s component=%s event=%s %s\r\n",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        GetCurrentProcessId(),GetCurrentThreadId(),severity,source,name,utf);
    AcquireSRWLockExclusive(&log_lock);
    if(log_path[0]&&length>0&&(size_t)length<sizeof(record)&&plain_file(log_path)&&plain_file(backup_path)){
        HANDLE f=CreateFileW(log_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(f!=INVALID_HANDLE_VALUE){
            LARGE_INTEGER size,end;end.QuadPart=0;
            BOOL ok=GetFileSizeEx(f,&size);
            if(ok&&size.QuadPart+length>LOG_LIMIT){
                CloseHandle(f);f=INVALID_HANDLE_VALUE;
                if(MoveFileExW(log_path,backup_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                    f=CreateFileW(log_path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
                ok=f!=INVALID_HANDLE_VALUE;
            }
            DWORD written=0;
            if(ok&&SetFilePointerEx(f,end,NULL,FILE_END)&&WriteFile(f,record,(DWORD)length,&written,NULL)&&written==(DWORD)length&&level==NOVA_LOG_ERROR)FlushFileBuffers(f);
            if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
        }
    }
    ReleaseSRWLockExclusive(&log_lock);SetLastError(saved);
}
