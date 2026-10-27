#ifndef NOVA_TEST_LOG_CAPTURE_H
#define NOVA_TEST_LOG_CAPTURE_H
#include "../src/platform/app_log.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Only call with this test's newly created temporary directory. */
static void check_diagnostics(const wchar_t *root,const char *const *events,size_t count){
    nova_log_close();char *all=calloc(4*1024*1024+1,1);assert(all);size_t length=0;
    wchar_t folder[MAX_PATH],path[MAX_PATH];swprintf(folder,MAX_PATH,L"%ls\\logs",root);
    const wchar_t *names[]={L"nova.previous.log",L"nova.log"};
    for(int i=0;i<2;i++){
        swprintf(path,MAX_PATH,L"%ls\\%ls",folder,names[i]);HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if(file==INVALID_HANDLE_VALUE){assert(GetLastError()==ERROR_FILE_NOT_FOUND);continue;}
        DWORD n=0;assert(ReadFile(file,all+length,2*1024*1024,&n,NULL));length+=n;CloseHandle(file);assert(DeleteFileW(path));
    }
    assert(length);for(size_t i=0;i<count;i++)if(!strstr(all,events[i])){fprintf(stderr,"Missing diagnostic event: %s\n",events[i]);assert(0);}
    char utf[MAX_PATH*4];assert(WideCharToMultiByte(CP_UTF8,0,root,-1,utf,sizeof(utf),NULL,NULL));assert(!strstr(all,utf)&&!strstr(all,"fixture-password")&&!strstr(all,"fixture-key-passphrase")&&!strstr(all,"NOVA_DIAGNOSTIC_PRIVATE"));
    free(all);assert(RemoveDirectoryW(folder));puts("PASS runtime diagnostics: real boundary events and private-data exclusion");
}
#endif
