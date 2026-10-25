#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform/sync_client.h"
#include "../src/platform/sync_settings.h"
#include "../third_party/sqlite/sqlite3.h"
BOOL nova_english=TRUE;
static wchar_t base[SYNC_PATH_CAP];
static SyncResult run(SyncRequest *r,int action){
    wcscpy(r->login.secret,L"fixture-password");wcscpy(r->target_login.secret,L"fixture-password");
    SyncResult result={0};BOOL ok=sync_client_run(r,action,&result);
    if(!ok)fwprintf(stderr,L"Expected/test server action %d result %d, resumed=%llu: %ls %ls\n",action,result.failure,(unsigned long long)result.stats.resumed,result.detail,result.ssh.error);
    return result;
}
static void folder(wchar_t *out,const wchar_t *parent,const wchar_t *leaf){swprintf(out,SYNC_PATH_CAP,L"%ls\\%ls",parent,leaf);assert(CreateDirectoryW(out,NULL)||GetLastError()==ERROR_ALREADY_EXISTS);}
static void request(SyncRequest *r,int role,int port,const char *pin){
    sync_settings_default(&r->settings);r->settings.role=role;r->settings.server_id=1;
    swprintf(r->settings.node_id,SYNC_ID_CAP,L"admin-fixture-%c",L'A'+role);swprintf(r->settings.name,SYNC_NAME_CAP,L"Fixture device %c",L'A'+role);
    wcscpy(r->settings.project,L"/fixture source'");wcscpy(r->settings.broker,L"/fixture source'/runtime/broker");
    r->login.server.id=1;r->login.server.port=port;wcscpy(r->login.server.host,L"127.0.0.1");wcscpy(r->login.server.user,L"fixture");wcscpy(r->login.server.directory,L"/");strcpy(r->login.fingerprint,pin);
    r->target_login=r->login;r->target_login.server.id=2;wcscpy(r->target_login.server.name,L"Fixture destination");
    wcscpy(r->target_project,r->settings.project);wcscpy(r->target_broker,r->settings.broker);r->quota_gib=1;r->reserve_gib=0;
    wchar_t local[SYNC_PATH_CAP],slot[2]={(wchar_t)(L'A'+role),0};folder(local,base,slot);folder(r->state_directory,local,L"state");folder(r->pair.directory,local,L"files");
    wcscpy(r->pair.id,L"management-pair");wcscpy(r->pair.dataset,L"management-data");wcscpy(r->pair.name,L"Management test files");wcscpy(r->pair.group,L"Management test files");wcscpy(r->pair.excludes,L".git;.nova-sync-*");r->pair.paused=true;
    r->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);assert(r->cancel);
}
static void file(const wchar_t *path,int seed){
    HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);assert(h!=INVALID_HANDLE_VALUE);unsigned char bytes[65536];uint32_t n=(uint32_t)seed;
    for(int part=0;part<16;part++){for(size_t i=0;i<sizeof(bytes);i++){n^=n<<13;n^=n>>17;n^=n<<5;bytes[i]=(unsigned char)(n>>16);}DWORD wrote;assert(WriteFile(h,bytes,sizeof(bytes),&wrote,NULL)&&wrote==sizeof(bytes));}FlushFileBuffers(h);CloseHandle(h);
}
static void attach(SyncRequest *r,SyncResult *result){
    assert(result->failure==SYNC_OK&&result->switched&&result->server_instance[0]);
    r->settings.server_id=r->target_login.server.id;r->login=r->target_login;
    wcscpy(r->settings.project,r->target_project);wcscpy(r->settings.broker,r->target_broker);wcscpy(r->settings.server_instance,result->server_instance);
    assert(sync_server_commit_local(r->state_directory));sync_result_dispose(result);
}
int wmain(int argc,wchar_t **argv){
    setvbuf(stdout,NULL,_IONBF,0);assert(argc==4);wcscpy(base,argv[3]);char pin[65];assert(WideCharToMultiByte(CP_UTF8,0,argv[2],-1,pin,65,NULL,NULL));assert(ssh_runtime_acquire());
    SyncRequest *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a&&b);request(a,0,_wtoi(argv[1]),pin);request(b,1,_wtoi(argv[1]),pin);
    SyncResult result=run(a,SYNC_JOB_SERVER_DEPLOY);attach(a,&result);a->settings.server_id=1;a->login.server.id=1;
    result=run(a,SYNC_JOB_REGISTER);assert(result.failure==SYNC_OK);sync_result_dispose(&result);a->settings.registered=true;
    wcscpy(b->settings.server_instance,a->settings.server_instance);result=run(b,SYNC_JOB_REGISTER);assert(result.failure==SYNC_OK);sync_result_dispose(&result);b->settings.registered=true;
    wchar_t first[SYNC_PATH_CAP],second[SYNC_PATH_CAP],received[SYNC_PATH_CAP];swprintf(first,SYNC_PATH_CAP,L"%ls\\ready.bin",a->pair.directory);swprintf(second,SYNC_PATH_CAP,L"%ls\\partial.bin",a->pair.directory);
    file(first,902);result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    file(second,715);result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_NETWORK);sync_result_dispose(&result);
    wcscpy(a->target_project,L"/fixture destination'");wcscpy(a->target_broker,L"/fixture destination'/runtime/broker");a->target_login.server.id=2;
    result=run(a,SYNC_JOB_SERVER_MIGRATE);assert(result.failure==SYNC_NETWORK&&result.pending_migration);sync_result_dispose(&result);
    result=run(b,SYNC_JOB_RUN);assert(result.failure==SYNC_FROZEN);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_MIGRATE);assert(result.failure==SYNC_NETWORK&&result.pending_migration&&result.stats.resumed>=SYNC_PACKET_MAX);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_RESTORE);assert(result.failure!=SYNC_OK);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_MIGRATE);attach(a,&result);assert(!wcscmp(a->settings.node_id,L"admin-fixture-A"));
    result=run(b,SYNC_JOB_RUN);assert(result.failure==SYNC_MOVED&&result.route);sync_result_dispose(&result);
    wcscpy(b->target_project,a->settings.project);wcscpy(b->target_broker,a->settings.broker);b->target_login.server.id=2;
    result=run(b,SYNC_JOB_SERVER_ATTACH);attach(b,&result);assert(!wcscmp(b->settings.node_id,L"admin-fixture-B"));
    result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK&&result.stats.resumed>=SYNC_PACKET_MAX);sync_result_dispose(&result);
    result=run(b,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    char ha[65],hb[65];swprintf(received,SYNC_PATH_CAP,L"%ls\\ready.bin",b->pair.directory);assert(sync_hash_file(first,ha,NULL,NULL,NULL)&&sync_hash_file(received,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    swprintf(received,SYNC_PATH_CAP,L"%ls\\partial.bin",b->pair.directory);assert(sync_hash_file(second,ha,NULL,NULL,NULL)&&sync_hash_file(received,hb,NULL,NULL,NULL)&&!strcmp(ha,hb));
    file(first,903);result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);result=run(b,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_PROBE);assert(result.failure==SYNC_OK&&result.installed);sqlite3 *db=NULL;assert(sqlite3_open(":memory:",&db)==SQLITE_OK);assert(sync_json_int(db,result.cloud,"$.stored_bytes",-1)==0);sqlite3_close(db);sync_result_dispose(&result);
    /* A nonempty destination must be refused without freezing the current source. */
    wcscpy(a->target_project,L"/fixture source'");wcscpy(a->target_broker,L"/fixture source'/runtime/broker");a->target_login.server.id=1;
    result=run(a,SYNC_JOB_SERVER_MIGRATE);assert(result.failure==SYNC_CONFLICT);sync_result_dispose(&result);result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    wcscpy(a->target_project,L"/fixture occupied'");wcscpy(a->target_broker,L"/fixture occupied'/runtime/broker");a->target_login.server.id=4;
    result=run(a,SYNC_JOB_SERVER_DEPLOY);assert(result.failure==SYNC_REMOTE&&!result.switched);sync_result_dispose(&result);
    wcscpy(a->target_project,L"/fixture rollback'");wcscpy(a->target_broker,L"/fixture rollback'/runtime/broker");a->target_login.server.id=3;
    result=run(a,SYNC_JOB_SERVER_MIGRATE);assert(result.failure==SYNC_NETWORK&&result.pending_migration);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_RESTORE);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_RUN);assert(result.failure==SYNC_OK);sync_result_dispose(&result);
    result=run(a,SYNC_JOB_SERVER_ATTACH);assert(result.failure==SYNC_FROZEN&&!result.switched);sync_result_dispose(&result);
    CloseHandle(a->cancel);CloseHandle(b->cancel);SecureZeroMemory(a,sizeof(*a));SecureZeroMemory(b,sizeof(*b));free(a);free(b);ssh_runtime_release();
    puts("PASS native embedded deployment, registration, READY/STAGING queues, migration interruptions, 128 KiB resume, cutover recovery, peer follow, retained lineage, source restoration, unknown/nonempty-target refusal and resumed ordinary sync");return 0;
}
