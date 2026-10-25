#ifndef NOVA_SYNC_INTERNAL_H
#define NOVA_SYNC_INTERNAL_H
#include "sync_json.h"
#include "../../third_party/sqlite/sqlite3.h"
typedef struct SyncRun {
    SyncRequest *request;SyncResult *result;sqlite3 *db;SshClient *ssh;
    wchar_t cache[SYNC_PATH_CAP],partial[SYNC_PATH_CAP];uint64_t gear[256];ULONGLONG progress_tick;
    char accepted_local[65]; /* Evolving working hash during one explicit version choice. */
} SyncRun;
typedef struct SyncReply { char *json;unsigned char *data;size_t size; } SyncReply;
BOOL sy_fail(SyncRun *r,SyncFailure code);
BOOL sy_cancel(SyncRun *r);
void sy_progress(SyncRun *r,SyncPhase phase,const wchar_t *path,uint64_t done,uint64_t total);
BOOL sy_join(wchar_t *out,const wchar_t *root,const wchar_t *name);
BOOL sy_mkdirs(const wchar_t *path);
BOOL sy_regular(const wchar_t *path);
BOOL sy_meta(const wchar_t *path,uint64_t *size,uint64_t *mtime);
BOOL sy_cache(SyncRun *r,const void *data,size_t size,char hash[65]);
BOOL sy_rpc(SyncRun *r,const char *request,const void *data,size_t size,SyncReply *reply);
void sy_reply_free(SyncReply *reply);
BOOL sy_exec(SyncRun *r,const char *sql);
BOOL sy_statement(SyncRun *r,const char *sql,const char **values,int count);
char *sy_query(SyncRun *r,const char *sql,const char *a,const char *b);
char *sy_fields(const char *action,const char *a,const char *av,const char *b,const char *bv);
BOOL sy_index_open(SyncRun *r);
BOOL sy_sync(SyncRun *r);
#endif
