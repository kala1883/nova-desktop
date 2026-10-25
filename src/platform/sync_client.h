#ifndef NOVA_SYNC_CLIENT_H
#define NOVA_SYNC_CLIENT_H
#include "ssh_client.h"
#include "../core/sync_model.h"
typedef struct sqlite3 sqlite3;
typedef enum SyncFailure { SYNC_OK,SYNC_CANCELLED,SYNC_STORAGE,SYNC_PATH,SYNC_CACHE,SYNC_PROTOCOL,SYNC_CONFLICT,SYNC_REMOTE,SYNC_NETWORK,SYNC_DELETE,SYNC_MOVED,SYNC_FROZEN } SyncFailure;
typedef enum SyncPhase { SYNC_CONNECT,SYNC_SCAN,SYNC_UPLOAD,SYNC_DOWNLOAD,SYNC_APPLY,SYNC_CONFIRM,SYNC_FINISH,SYNC_DEPLOY,SYNC_FREEZE,SYNC_COPY_SERVER,SYNC_VERIFY_SERVER,SYNC_CUTOVER } SyncPhase;
typedef struct SyncStats { uint64_t sent,received,reused,resumed,reclaimed; int published,downloaded,unchanged; } SyncStats;
typedef struct SyncProgress { SyncPhase phase; uint64_t done,total; wchar_t path[SYNC_PATH_CAP]; SyncStats stats; } SyncProgress;
typedef void (*SyncProgressFn)(void *context,const SyncProgress *progress);
typedef BOOL (*SyncRecycleFn)(void *context,const wchar_t *path,uint64_t size,uint64_t mtime);
typedef struct SyncRequest {
    SyncSettings settings; SyncPair pair; SshLogin login; wchar_t state_directory[SYNC_PATH_CAP];
    HANDLE cancel; SyncProgressFn progress; SyncRecycleFn recycle;void *context;
    SshLogin target_login;
    wchar_t target_project[SYNC_PATH_CAP],target_broker[SYNC_PATH_CAP];
    int quota_gib,reserve_gib;
} SyncRequest;
typedef struct SyncResult { SyncFailure failure;DWORD native_error;SshResult ssh;SyncStats stats;char *cloud,*route;wchar_t detail[1024];
    BOOL target_error,installed,switched,pending_migration;
    wchar_t server_instance[SYNC_ID_CAP],server_phase[32];
} SyncResult;
enum { SYNC_JOB_REFRESH=1,SYNC_JOB_REGISTER,SYNC_JOB_RUN,SYNC_JOB_BIND,SYNC_JOB_UNBIND };
enum { SYNC_JOB_SERVER_PROBE=10,SYNC_JOB_SERVER_DEPLOY,SYNC_JOB_SERVER_ATTACH,SYNC_JOB_SERVER_MIGRATE,SYNC_JOB_SERVER_RESTORE };
BOOL sync_server_run(SyncRequest *request,int action,SyncResult *result);
BOOL sync_server_pending(SyncRequest *request);
BOOL sync_server_commit_local(const wchar_t *state_directory);
BOOL sync_client_run(SyncRequest *request,int action,SyncResult *result);
void sync_result_dispose(SyncResult *result);
/* Bounded codec helpers are public for deterministic, offline fixture tests. */
char *sync_json_string(sqlite3 *db,const char *document,const char *path);
int64_t sync_json_int(sqlite3 *db,const char *document,const char *path,int64_t fallback);
BOOL sync_hash_file(const wchar_t *path,char hex[65],uint64_t *size,uint64_t *mtime,HANDLE cancel);
#endif
