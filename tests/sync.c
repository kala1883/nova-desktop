#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "../src/core/sync_model.h"
#include "../src/platform/sync_settings.h"
#include "../src/platform/sync_client.h"
#include "../src/platform/sync_internal.h"
#include "../src/platform/sync_json.h"
#include "../src/platform/storage.h"
#include "../third_party/sqlite/sqlite3.h"
BOOL nova_english=FALSE;
static DWORD contents(const wchar_t *file,char *data,DWORD cap){HANDLE h=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(h!=INVALID_HANDLE_VALUE);DWORD n=0;assert(ReadFile(h,data,cap-1,&n,NULL));data[n]=0;CloseHandle(h);return n;}
static void fixture_pair(SyncPair *p){memset(p,0,sizeof(*p));wcscpy(p->id,L"fixture-pair");wcscpy(p->dataset,L"fixture-data");wcscpy(p->name,L"Synthetic pair");wcscpy(p->group,L"Synthetic group");wcscpy(p->directory,L"C:\\fixture-only");wcscpy(p->excludes,L".git;node_modules");p->paused=true;}
static void model_tests(void){
    SyncSettings *s=malloc(sizeof(*s)),*before=malloc(sizeof(*before));assert(s&&before);sync_settings_default(s);assert(sync_settings_valid(s));SyncPair p;fixture_pair(&p);assert(sync_pair_set(s,0,&p)==0);*before=*s;assert(sync_pair_set(s,1,&p)<0&&!memcmp(s,before,sizeof(*s)));
    wcscpy(p.id,L"fixture-second");wcscpy(p.dataset,L"different-group");wcscpy(p.directory,L"c:\\FIXTURE-ONLY");assert(sync_pair_set(s,1,&p)<0&&!memcmp(s,before,sizeof(*s)));assert(sync_relative_path(L"folder/中文.txt"));assert(!sync_relative_path(L"../escape")&&!sync_relative_path(L"/absolute")&&!sync_relative_path(L".git/index"));
    s->version=2;assert(!sync_settings_valid(s));free(before);free(s);puts("PASS sync model: bounded collection, duplicates, immutable failures, Unicode and traversal checks");
}
static void settings_tests(const wchar_t *dir){
    assert(store_open(dir));assert(store_set_int(L"app",L"Nova",L"Language",0));wchar_t shared[MAX_PATH],local[MAX_PATH];swprintf(shared,MAX_PATH,L"%ls\\runtime.json",dir);swprintf(local,MAX_PATH,L"%ls\\local.json",dir);char before[65536],after[65536],private_json[65536];DWORD length=contents(shared,before,sizeof(before));
    SyncSettings *s=malloc(sizeof(*s)),*loaded=malloc(sizeof(*loaded));assert(s&&loaded);sync_settings_default(s);wcscpy(s->node_id,L"synthetic-node");wcscpy(s->name,L"Synthetic private device");s->registered=true;fixture_pair(&s->pairs[0]);s->count=1;
    assert(sync_settings_save(s));assert(contents(shared,after,sizeof(after))==length&&!memcmp(before,after,length));contents(local,private_json,sizeof(private_json));assert(strstr(private_json,"synthetic-node")&&!strstr(after,"fixture-only"));assert(sync_settings_load(loaded)&&!memcmp(loaded,s,sizeof(*s)));
    assert(store_set_int(L"sync",L"Manager",L"Version",2));assert(!sync_settings_load(loaded));store_close();assert(store_open(dir));assert(!sync_settings_load(loaded));store_close();free(s);free(loaded);puts("PASS sync configuration: local.json only, exact reload and newer-version rejection");
}
static void json_tests(void){sqlite3 *db=NULL;assert(sqlite3_open(":memory:",&db)==SQLITE_OK);SyncBuffer b={0};assert(sync_buffer_add(&b,"{\"text\":"));assert(sync_buffer_string(&b,"quote\" newline\n UTF8 \xe4\xb8\xad"));assert(sync_buffer_add(&b,"}"));assert(sync_json_valid(db,b.data));char *text=sync_json_string(db,b.data,"$.text");assert(text&&!strcmp(text,"quote\" newline\n UTF8 \xe4\xb8\xad"));free(text);assert(!sync_json_string(db,"{\"path\":\"safe\\u0000escape\"}","$.path"));free(b.data);sqlite3_close(db);puts("PASS bounded UTF-8 JSON round trip and embedded-NUL refusal");}
static void index_tests(const wchar_t *dir){
    SyncRequest *request=calloc(1,sizeof(*request));assert(request);swprintf(request->state_directory,SYNC_PATH_CAP,L"%ls\\index",dir);SyncResult result={0};SyncRun run={0};run.request=request;run.result=&result;assert(sy_index_open(&run));
    assert(sqlite3_exec(run.db,"PRAGMA user_version=2",NULL,NULL,NULL)==SQLITE_OK);sqlite3_close(run.db);run.db=NULL;assert(!sy_index_open(&run)&&result.failure==SYNC_STORAGE);assert(sync_json_int(run.db,"{\"version\":2}","$.version",0)==2);sqlite3_stmt *v=NULL;assert(sqlite3_prepare_v2(run.db,"PRAGMA user_version",-1,&v,NULL)==SQLITE_OK&&sqlite3_step(v)==SQLITE_ROW&&sqlite3_column_int(v,0)==2);sqlite3_finalize(v);
    assert(sqlite3_exec(run.db,"PRAGMA user_version=0;ALTER TABLE files ADD COLUMN future_field TEXT",NULL,NULL,NULL)==SQLITE_OK);sqlite3_close(run.db);run.db=NULL;result.failure=SYNC_OK;assert(!sy_index_open(&run)&&result.failure==SYNC_STORAGE);v=NULL;assert(sqlite3_prepare_v2(run.db,"SELECT future_field FROM files",-1,&v,NULL)==SQLITE_OK);sqlite3_finalize(v);sqlite3_close(run.db);free(request);puts("PASS disk index: newer-version and incompatible-column refusal without overwriting tables");
}
static void import_tests(const wchar_t *dir){
    wchar_t file[MAX_PATH];swprintf(file,MAX_PATH,L"%ls\\legacy.json",dir);
    const char *json="{\"identity\":{\"node_id\":\"fixture-old-node\",\"name\":\"Retained user name\",\"role\":\"B\",\"registered\":true},\"connection\":{\"remote_project\":\"/fixture\",\"remote_root\":\"/fixture/runtime/broker\"},\"nova_server_id\":\"99\",\"folders\":[{\"id\":\"old-pair\",\"dataset\":\"old-dataset\",\"name\":\"Retained pair\",\"group_name\":\"Retained group\",\"path\":\"C:\\\\synthetic-only\",\"paused\":false,\"sync_deletes\":true,\"excludes\":[\"*.cache\",\"node_modules\"]}]}";
    HANDLE h=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);assert(h!=INVALID_HANDLE_VALUE);DWORD n;assert(WriteFile(h,"\xef\xbb\xbf",3,&n,NULL)&&WriteFile(h,json,(DWORD)strlen(json),&n,NULL));CloseHandle(h);
    SyncSettings *s=malloc(sizeof(*s));assert(s&&sync_settings_import(file,s));assert(s->registered&&s->role==1&&s->server_id==99&&!wcscmp(s->node_id,L"fixture-old-node"));assert(s->count==1&&s->pairs[0].paused&&s->pairs[0].sync_deletes&&!wcscmp(s->pairs[0].excludes,L"*.cache;node_modules"));free(s);puts("PASS legacy import: preserved identity, pair, filters, deletion policy and server hint; migration starts paused");
}
static void journal_tests(const wchar_t *dir){
    SyncRequest *r=calloc(1,sizeof(*r));assert(r);sync_settings_default(&r->settings);r->settings.server_id=1;lstrcpyW(r->state_directory,dir);
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\server-migration.json",dir);
    const char *values[]={"{\"version\":1,\"source_id\":1,\"target_id\":2,\"project\":\"/future\",\"root\":\"/future/runtime/broker\",\"instance\":\"future-instance\",\"token\":\"fixture-token\"}","{\"version\":2,\"source_id\":1,\"target_id\":2,\"project\":\"/future\",\"root\":\"/future/runtime/broker\",\"instance\":\"future-instance\",\"token\":\"fixture-token\"}"};
    for(int i=0;i<2;i++){HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD wrote;assert(h!=INVALID_HANDLE_VALUE&&WriteFile(h,values[i],(DWORD)strlen(values[i]),&wrote,NULL)&&wrote==strlen(values[i]));CloseHandle(h);assert(sync_server_pending(r)==(i==0));char after[1024];contents(path,after,sizeof(after));assert(!strcmp(after,values[i]));}
    free(r);puts("PASS migration checkpoint version: compatible resume and future-version refusal without rewriting data");
}
int wmain(void){wchar_t temp[MAX_PATH],name[MAX_PATH],root[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));assert(GetTempFileNameW(temp,L"nsy",0,name));assert(DeleteFileW(name));assert(CreateDirectoryW(name,NULL));swprintf(root,MAX_PATH,L"%ls\\settings",name);assert(CreateDirectoryW(root,NULL));model_tests();settings_tests(root);json_tests();import_tests(root);index_tests(root);journal_tests(root);puts("PASS native synchronization offline tests (temporary state only)");return 0;}
