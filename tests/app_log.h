#include "../src/platform/app_log.h"
static DWORD WINAPI log_writer(void *context){
    int index=(int)(INT_PTR)context;
    for(int i=0;i<80;i++)nova_log_write(NOVA_LOG_INFO,"fixture","concurrent",L"writer=%d row=%d",index,i);
    return 0;
}
static void log_tests(const wchar_t *root){
    wchar_t path[MAX_PATH],backup[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\logs\\nova.log",root);swprintf(backup,MAX_PATH,L"%ls\\logs\\nova.previous.log",root);
    SetLastError(12345);assert(nova_log_init(root)&&GetLastError()==12345);
    nova_log_write(NOVA_LOG_WARN,"fixture","unicode",L"中文\r\ninjected\tline");assert(GetLastError()==12345);
    char data[65536];contents(path,data,sizeof(data));assert(strstr(data,"\xe4\xb8\xad\xe6\x96\x87  injected line\r\n")&&strstr(data,"level=WARN"));
    HANDLE threads[4];for(int i=0;i<4;i++){threads[i]=CreateThread(NULL,0,log_writer,(void*)(INT_PTR)i,0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0);for(int i=0;i<4;i++)CloseHandle(threads[i]);
    contents(path,data,sizeof(data));int lines=0;for(char *p=data;*p;p++)if(*p=='\n')lines++;assert(lines==321);
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);LARGE_INTEGER end;end.QuadPart=2*1024*1024-1;
    assert(f!=INVALID_HANDLE_VALUE&&SetFilePointerEx(f,end,NULL,FILE_BEGIN)&&SetEndOfFile(f));CloseHandle(f);
    nova_log_write(NOVA_LOG_ERROR,"fixture","rotated",L"failure=synthetic");contents(path,data,sizeof(data));assert(strstr(data,"event=rotated")&&!strstr(data,"event=concurrent"));
    WIN32_FILE_ATTRIBUTE_DATA attributes;assert(GetFileAttributesExW(backup,GetFileExInfoStandard,&attributes)&&attributes.nFileSizeLow==2*1024*1024-1);
    nova_log_close();assert(DeleteFileW(path));nova_log_write(NOVA_LOG_INFO,"fixture","disabled",L"row=0");assert(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    assert(CreateDirectoryW(path,NULL));assert(!nova_log_init(root));SetLastError(12345);nova_log_write(NOVA_LOG_INFO,"fixture","unavailable",L"row=0");assert(GetLastError()==12345);nova_log_close();
    puts("PASS local log: UTF-8, one-line records, error preservation, concurrent append, bounded rotation and unavailable/disabled sink");
}
