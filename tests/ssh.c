#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <assert.h>
#include "log_capture.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/platform/storage.h"
BOOL nova_english=FALSE;
static void rules(void){
    int64_t id=0;const wchar_t *path=NULL;wchar_t value[SSH_PATH_CAP];char data[128];
    assert(ssh_location_format(value,SSH_PATH_CAP,123,L"/Case sensitive/中文"));assert(ssh_location_parse(value,&id,&path)&&id==123&&!wcscmp(path,L"/Case sensitive/中文"));
    assert(!ssh_location_parse(L"nova-ssh:-1:/",NULL,NULL));assert(!ssh_location_parse(L"nova-ssh:9223372036854775808:/",NULL,NULL));assert(!ssh_location_parse(L"nova-ssh:1:relative",NULL,NULL));
    assert(ssh_path_join(value,SSH_PATH_CAP,L"/",L"a'b $()"));assert(!wcscmp(value,L"/a'b $()"));assert(!ssh_path_join(value,SSH_PATH_CAP,L"/",L".."));assert(!ssh_path_join(value,SSH_PATH_CAP,L"/",L"a/b"));
    assert(ssh_directory_payload((const unsigned char*)"/a'\n",4,data,sizeof(data)));assert(!strcmp(data,"\\0057\\0141\\0047\\0012\n"));assert(!ssh_directory_payload((const unsigned char*)"/x",2,data,4));assert(!ssh_directory_payload((const unsigned char*)"/\0",2,data,sizeof(data)));
    SshServers *servers=calloc(1,sizeof(*servers));assert(servers);servers->count=1;servers->items[0].id=123;servers->items[0].port=22;wcscpy(servers->items[0].name,L"Test");wcscpy(servers->items[0].host,L"test.example.invalid");wcscpy(servers->items[0].user,L"deploy");wcscpy(servers->items[0].directory,L"/");assert(ssh_servers_valid(servers));
    servers->items[0].port=65536;assert(!ssh_servers_valid(servers));servers->items[0].port=22;servers->count=2;servers->items[1]=servers->items[0];assert(!ssh_servers_valid(servers));free(servers);
    puts("PASS SSH identity, limits, POSIX case-sensitive paths and stdin directory framing");
}
int wmain(void){
    rules();wchar_t temp[MAX_PATH],root[MAX_PATH],json[MAX_PATH];GetTempPathW(MAX_PATH,temp);swprintf(root,MAX_PATH,L"%lsNovaSsh-%lu",temp,GetCurrentProcessId());assert(CreateDirectoryW(root,NULL));assert(nova_log_init(root));
    SshServers *servers=calloc(1,sizeof(*servers)),*loaded=calloc(1,sizeof(*loaded));assert(servers&&loaded&&store_open(root));assert(store_load_ssh_servers(loaded)&&loaded->count==0);
    servers->count=1;SshServer *s=&servers->items[0];s->id=123;s->port=2222;wcscpy(s->name,L"中文连接");wcscpy(s->host,L"test.example.invalid");wcscpy(s->user,L"deploy");wcscpy(s->directory,L"/Case sensitive");assert(store_save_ssh_servers(servers));store_close();assert(store_open(root));assert(store_load_ssh_servers(loaded)&&loaded->count==1&&!memcmp(s,&loaded->items[0],sizeof(*s)));
    servers->items[0].port=0;assert(!store_save_ssh_servers(servers));assert(store_load_ssh_servers(loaded)&&loaded->items[0].port==2222);
    assert(store_set_int(L"ssh_servers",L"Servers",L"Version",2));store_close();assert(!store_open(root));
    swprintf(json,MAX_PATH,L"%ls\\runtime.json",root);assert(GetFileAttributesW(json)!=INVALID_FILE_ATTRIBUTES);
    const wchar_t *names[]={L"runtime.json",L"runtime.base.json",L"runtime.seed.json",L"runtime.backup.json",L"local.json",L"local.backup.json",L"nova.migration.json"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){swprintf(json,MAX_PATH,L"%ls\\%ls",root,names[i]);DeleteFileW(json);}
    {const char *events[]={"kind=ssh_servers","action=save_ssh_servers","event=collection_saved"};check_diagnostics(root,events,3);}
    assert(RemoveDirectoryW(root));free(servers);free(loaded);
    puts("PASS transactional SSH profiles, Unicode roundtrip, invalid save rollback and newer-version rejection");return 0;
}
