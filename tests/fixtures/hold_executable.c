#define _WIN32_WINNT 0x0601
#include <windows.h>

/* A disposable executable image lock; never loads NOVA or user settings. */
int wmain(int argc,wchar_t **argv){
    if(argc!=3)return 2;
    HANDLE ready=CreateFileW(argv[1],GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(ready==INVALID_HANDLE_VALUE)return 3;
    CloseHandle(ready);
    for(int i=0;i<300&&GetFileAttributesW(argv[2])==INVALID_FILE_ATTRIBUTES;i++)Sleep(100);
    return 0;
}
