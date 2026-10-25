#define _WIN32_WINNT 0x0601
/* Match the native sync client's C99 integer formatting on older MinGW. */
#define __USE_MINGW_ANSI_STDIO 1
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform/sync_client.h"
#include "../src/platform/sync_json.h"
#include "../third_party/sqlite/sqlite3.h"
BOOL nova_english=FALSE;
static wchar_t root[MAX_PATH];
static BOOL recycle_fixture(void *context,const wchar_t *path,uint64_t size,uint64_t mtime){(void)context;(void)size;(void)mtime;assert(!wcsncmp(path,root,wcslen(root)));wchar_t backup[MAX_PATH];swprintf(backup,MAX_PATH,L"%ls\\recovery.bin",root);return MoveFileExW(path,backup,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);}
static void directory(wchar_t *out,const wchar_t *parent,const wchar_t *name){swprintf(out,SYNC_PATH_CAP,L"%ls\\%ls",parent,name);assert(CreateDirectoryW(out,NULL)||GetLastError()==ERROR_ALREADY_EXISTS);}
static void setup(SyncRequest *r,int role,const wchar_t *host,int port,const char *pin){
    memset(r,0,sizeof(*r));sync_settings_default(&r->settings);r->settings.role=role;r->settings.registered=true;swprintf(r->settings.node_id,SYNC_ID_CAP,L"fixture-%c",L'A'+role);swprintf(r->settings.name,SYNC_NAME_CAP,L"Native fixture %c",L'A'+role);
    wcscpy(r->settings.project,L"/fixture project'");wcscpy(r->settings.broker,L"/fixture project'/runtime/broker");r->login.server.id=1;r->login.server.port=port;lstrcpyW(r->login.server.host,host);wcscpy(r->login.server.user,L"fixture");wcscpy(r->login.server.directory,L"/");wcscpy(r->login.secret,L"fixture-password");strcpy(r->login.fingerprint,pin);
    wchar_t local[SYNC_PATH_CAP],slot[4]={(wchar_t)('A'+role),0};directory(local,root,slot);directory(r->state_directory,local,L"state");directory(r->pair.directory,local,L"files");wcscpy(r->pair.id,L"fixture-pair");wcscpy(r->pair.dataset,L"fixture-files");wcscpy(r->pair.name,L"Synthetic files");wcscpy(r->pair.group,L"Synthetic files");wcscpy(r->pair.excludes,L".git;node_modules;.nova-sync-*");r->pair.paused=true;r->pair.sync_deletes=true;r->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);assert(r->cancel);r->recycle=recycle_fixture;
}
static SyncResult action(SyncRequest *r,int what){SyncResult result={0};wcscpy(r->login.secret,L"fixture-password");BOOL ok=sync_client_run(r,what,&result);if(!ok)fwprintf(stderr,L"native sync failure %d Windows=%lu SSH=%ls provider=%ls\n",result.failure,result.native_error,result.ssh.error,result.detail);return result;}
static void source_file(const wchar_t *path){HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);assert(file!=INVALID_HANDLE_VALUE);unsigned char buffer[65536];uint32_t random=721;
    for(int part=0;part<64;part++){for(size_t i=0;i<sizeof(buffer);i++){random^=random<<13;random^=random>>17;random^=random<<5;buffer[i]=(unsigned char)(random>>16);}DWORD wrote=0;assert(WriteFile(file,buffer,sizeof(buffer),&wrote,NULL)&&wrote==sizeof(buffer));}assert(FlushFileBuffers(file));CloseHandle(file);}
static void write_text(const wchar_t *path,const char *text){HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD n=0;assert(f!=INVALID_HANDLE_VALUE&&WriteFile(f,text,(DWORD)strlen(text),&n,NULL)&&n==strlen(text)&&FlushFileBuffers(f));CloseHandle(f);}
static void choose_version(SyncRequest *r,SyncResult *checked,const wchar_t *file,int choice){
    sqlite3 *db=NULL;assert(sqlite3_open(":memory:",&db)==SQLITE_OK);assert(checked->changes);
    assert(sync_json_int(db,checked->changes,"$[0].mtime",0)>0&&sync_json_int(db,checked->changes,"$[0].remote_mtime",0)>0);
    char *remote=sync_json_string(db,checked->changes,"$[0].remote_version");assert(remote&&strlen(remote)<sizeof(r->expected_remote));strcpy(r->expected_remote,remote);free(remote);
    assert(sync_hash_file(file,r->expected_local,NULL,NULL,NULL));wcscpy(r->resolve_path,L"中文.bin");r->resolution=choice;sqlite3_close(db);
}
int wmain(int argc,wchar_t **argv){
    assert(argc==5);lstrcpyW(root,argv[4]);assert(GetFileAttributesW(root)&FILE_ATTRIBUTE_DIRECTORY);char pin[80];assert(WideCharToMultiByte(CP_UTF8,0,argv[3],-1,pin,sizeof(pin),NULL,NULL));assert(ssh_runtime_acquire());
    SyncRequest *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a&&b);setup(a,0,argv[1],_wtoi(argv[2]),pin);setup(b,1,argv[1],_wtoi(argv[2]),pin);SyncResult result=action(a,SYNC_JOB_REGISTER);assert(result.failure==SYNC_OK);sync_result_dispose(&result);result=action(b,SYNC_JOB_REGISTER);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    wchar_t pa[SYNC_PATH_CAP],pb[SYNC_PATH_CAP];swprintf(pa,SYNC_PATH_CAP,L"%ls\\中文.bin",a->pair.directory);swprintf(pb,SYNC_PATH_CAP,L"%ls\\中文.bin",b->pair.directory);source_file(pa);
    wchar_t legacy[SYNC_PATH_CAP];swprintf(legacy,SYNC_PATH_CAP,L"%ls\\A\\legacy-server.flag",root);write_text(legacy,"old broker response");
    result=action(a,SYNC_JOB_CHECK);assert(result.failure==SYNC_UPGRADE&&result.stats.sent==0&&result.stats.published==0);sync_result_dispose(&result);
    result=action(a,SYNC_JOB_PUSH);assert(result.failure==SYNC_UPGRADE&&result.stats.sent==0&&result.stats.published==0);sync_result_dispose(&result);assert(DeleteFileW(legacy));
    puts("PASS legacy server CHECK/PUSH refusal identifies required server update before uploading content");
    result=action(a,SYNC_JOB_RUN);assert(result.failure==SYNC_NETWORK);sync_result_dispose(&result);
    result=action(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&result.stats.resumed>=SYNC_PACKET_MAX);printf("PASS native upload resumed at %llu bytes\n",(unsigned long long)result.stats.resumed);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_RUN);assert(result.failure==SYNC_NETWORK);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&result.stats.resumed>=SYNC_PACKET_MAX);printf("PASS native download resumed at %llu bytes\n",(unsigned long long)result.stats.resumed);sync_result_dispose(&result);
    char ha[65],hb[65];uint64_t size;assert(sync_hash_file(pa,ha,&size,NULL,NULL)&&sync_hash_file(pb,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    HANDLE edit=CreateFileW(pa,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);assert(edit!=INVALID_HANDLE_VALUE);LARGE_INTEGER position;position.QuadPart=2*1024*1024;assert(SetFilePointerEx(edit,position,NULL,FILE_BEGIN));DWORD written=0;assert(WriteFile(edit,"a tiny native edit",18,&written,NULL)&&written==18);FlushFileBuffers(edit);CloseHandle(edit);
    result=action(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&result.stats.sent<size/2&&result.stats.reused>size/2);printf("PASS native delta upload %llu of %llu bytes\n",(unsigned long long)result.stats.sent,(unsigned long long)size);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&result.stats.received<size/2);sync_result_dispose(&result);assert(sync_hash_file(pa,ha,&size,NULL,NULL)&&sync_hash_file(pb,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    assert(DeleteFileW(pa));result=action(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);result=action(b,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&GetFileAttributesW(pb)==INVALID_FILE_ATTRIBUTES);sync_result_dispose(&result);wchar_t recovered[MAX_PATH];swprintf(recovered,MAX_PATH,L"%ls\\recovery.bin",root);assert(GetFileAttributesW(recovered)!=INVALID_FILE_ATTRIBUTES);
    write_text(pa,"initial published content");
    result=action(a,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK&&result.pushes==1&&result.stats.sent==0&&result.stats.published==0);sync_result_dispose(&result);
    result=action(a,SYNC_JOB_PUSH);assert(result.failure==SYNC_OK&&result.changes&&result.pushes==0);sync_result_dispose(&result);
    wchar_t local_only[SYNC_PATH_CAP];swprintf(local_only,SYNC_PATH_CAP,L"%ls\\local-only.txt",b->pair.directory);write_text(local_only,"must not upload during pull");
    result=action(b,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK&&result.pulls==1&&result.pushes==1);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_PULL);assert(result.failure==SYNC_OK&&result.stats.published==0&&result.changes&&result.pushes==1&&result.pulls==0);sync_result_dispose(&result);assert(DeleteFileW(local_only));
    SyncRequest *c=calloc(1,sizeof(*c));assert(c);setup(c,2,argv[1],_wtoi(argv[2]),pin);
    result=action(c,SYNC_JOB_REGISTER);assert(result.failure==SYNC_OK&&result.registered_role==2);sync_result_dispose(&result);
    result=action(c,SYNC_JOB_BIND);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=action(c,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK&&result.waiting==1);sync_result_dispose(&result);
    result=action(a,SYNC_JOB_PUSH);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=action(c,SYNC_JOB_PULL);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    wchar_t pc[SYNC_PATH_CAP];swprintf(pc,SYNC_PATH_CAP,L"%ls\\中文.bin",c->pair.directory);assert(sync_hash_file(pa,ha,NULL,NULL,NULL)&&sync_hash_file(pc,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    puts("PASS read-only status inspection, directional transfer, automatic third-device registration and late subscription after cloud reclamation");
    write_text(pa,"server edit one");result=action(a,SYNC_JOB_PUSH);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    write_text(pb,"keep this local edit");result=action(b,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK&&result.conflicts==1);choose_version(b,&result,pb,1);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_RUN);assert(result.failure==SYNC_CONFLICT);sync_result_dispose(&result);
    assert(sync_hash_file(pb,hb,NULL,NULL,NULL)&&!strcmp(hb,b->expected_local));
    result=action(b,SYNC_JOB_RESOLVE);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=action(a,SYNC_JOB_PULL);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=action(c,SYNC_JOB_PULL);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    assert(sync_hash_file(pa,ha,NULL,NULL,NULL)&&sync_hash_file(pb,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    write_text(pa,"server wins this time");result=action(a,SYNC_JOB_PUSH);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    write_text(pb,"local backup needed");result=action(b,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK&&result.conflicts==1);choose_version(b,&result,pb,2);sync_result_dispose(&result);
    write_text(pb,"changed after inspection");result=action(b,SYNC_JOB_RESOLVE);assert(result.failure==SYNC_CONFLICT);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK);choose_version(b,&result,pb,2);sync_result_dispose(&result);
    wchar_t race_flag[SYNC_PATH_CAP];swprintf(race_flag,SYNC_PATH_CAP,L"%ls\\B\\change-before-receive.flag",root);write_text(race_flag,"fixture-only race");
    result=action(b,SYNC_JOB_RESOLVE);assert(result.failure==SYNC_CONFLICT);sync_result_dispose(&result);assert(sync_hash_file(pb,hb,NULL,NULL,NULL)&&strcmp(hb,b->expected_local));
    result=action(b,SYNC_JOB_CHECK);assert(result.failure==SYNC_OK);choose_version(b,&result,pb,2);sync_result_dispose(&result);
    result=action(b,SYNC_JOB_RESOLVE);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    assert(sync_hash_file(pa,ha,NULL,NULL,NULL)&&sync_hash_file(pb,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    wchar_t recovery[SYNC_PATH_CAP];swprintf(recovery,SYNC_PATH_CAP,L"%ls\\recovery\\%hs\\中文.bin",b->state_directory,b->expected_local);assert(sync_hash_file(recovery,hb,NULL,NULL,NULL)&&!strcmp(hb,b->expected_local));
    result=action(c,SYNC_JOB_PULL);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    puts("PASS local/server conflict choices, stale inspection refusal, recovery copies and version propagation to three devices");
    CloseHandle(a->cancel);CloseHandle(b->cancel);CloseHandle(c->cancel);SecureZeroMemory(a,sizeof(*a));SecureZeroMemory(b,sizeof(*b));SecureZeroMemory(c,sizeof(*c));free(a);free(b);free(c);ssh_runtime_release();puts("PASS native SHA-256, Python Gear compatibility, incremental offline queue, resume, acknowledgements and deletion callback (loopback only)");return 0;
}
