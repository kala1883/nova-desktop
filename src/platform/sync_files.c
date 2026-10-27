#define _WIN32_WINNT 0x0601
#include "sync_internal.h"
#include <openssl/evp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
BOOL sy_join(wchar_t *out,const wchar_t *root,const wchar_t *name){
    size_t a=wcslen(root),b=wcslen(name);
    if(a+b+2>=SYNC_PATH_CAP)return FALSE;
    swprintf(out,SYNC_PATH_CAP,L"%ls\\%ls",root,name);
    return TRUE;
}
BOOL sy_mkdirs(const wchar_t *path){
    wchar_t copy[SYNC_PATH_CAP];
    if(wcslen(path)>=SYNC_PATH_CAP||!path[0])return FALSE;
    lstrcpyW(copy,path);
    for(wchar_t *p=copy+3;;p++)if(*p==L'\\'||!*p){wchar_t end=*p;
        *p=0;
        DWORD attributes=GetFileAttributesW(copy);
        if(attributes==INVALID_FILE_ATTRIBUTES){if(!CreateDirectoryW(copy,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
            attributes=GetFileAttributesW(copy);
            }
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return FALSE;
        *p=end;
        if(!end)break;
        }
    return TRUE;
}
BOOL sy_regular(const wchar_t *path){DWORD attr=GetFileAttributesW(path);
    return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    }
BOOL sy_meta(const wchar_t *path,uint64_t *size,uint64_t *mtime){
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExW(path,GetFileExInfoStandard,&data)||(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return FALSE;
    *size=((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
    uint64_t ticks=((uint64_t)data.ftLastWriteTime.dwHighDateTime<<32)|data.ftLastWriteTime.dwLowDateTime;
    if(ticks<116444736000000000ULL||ticks-116444736000000000ULL>INT64_MAX/100)return FALSE;
    *mtime=(ticks-116444736000000000ULL)*100;
    return TRUE;
}
BOOL sync_hash_file(const wchar_t *path,char out[65],uint64_t *size,uint64_t *mtime,HANDLE cancel){
    uint64_t initial,nanos;
    if(!sy_meta(path,&initial,&nanos))return FALSE;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    EVP_MD_CTX *ctx=EVP_MD_CTX_new();
    unsigned char *buffer=malloc(128*1024),digest[32];
    unsigned int n=0;
    uint64_t total=0;
    BOOL ok=ctx&&buffer&&EVP_DigestInit_ex(ctx,EVP_sha256(),NULL)==1;
    while(ok){if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){ok=FALSE;
            break;
            }DWORD got=0;
        if(!ReadFile(file,buffer,128*1024,&got,NULL)){ok=FALSE;
            break;
            }if(!got)break;
        total+=got;
        ok=EVP_DigestUpdate(ctx,buffer,got)==1;
        }
    if(ok)ok=total==initial&&EVP_DigestFinal_ex(ctx,digest,&n)==1&&n==32;
    if(ok){for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);
        if(size)*size=total;
        if(mtime)*mtime=nanos;
        }
    EVP_MD_CTX_free(ctx);
    free(buffer);
    CloseHandle(file);
    return ok;
}
BOOL sy_cache(SyncRun *r,const void *data,size_t size,char hash[65]){
    if(!sync_sha(data,size,hash))return sy_fail(r,SYNC_CACHE);
    wchar_t name[80],target[SYNC_PATH_CAP];
    if(!sync_wide(hash,name,80)||!sy_join(target,r->cache,name))return sy_fail(r,SYNC_PATH);
    if(GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES){char actual[65];
        uint64_t n;
        return (sync_hash_file(target,actual,&n,NULL,r->request->cancel)&&n==size&&!strcmp(actual,hash))||sy_fail(r,SYNC_CACHE);
        }
    wchar_t temporary[SYNC_PATH_CAP];
    unsigned int nonce;
    sqlite3_randomness(sizeof(nonce),&nonce);
    if(swprintf(temporary,SYNC_PATH_CAP,L"%ls.%08x.tmp",target,nonce)<0)return sy_fail(r,SYNC_PATH);
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return sy_fail(r,SYNC_CACHE);
    DWORD written=0;
    BOOL ok=WriteFile(file,data,(DWORD)size,&written,NULL)&&written==size&&FlushFileBuffers(file);
    CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok)DeleteFileW(temporary);
    return ok||sy_fail(r,SYNC_CACHE);
}
BOOL sy_exec(SyncRun *r,const char *sql){return sqlite3_exec(r->db,sql,NULL,NULL,NULL)==SQLITE_OK||sy_fail(r,SYNC_STORAGE);
    }
BOOL sy_statement(SyncRun *r,const char *sql,const char **values,int count){
    sqlite3_stmt *s=NULL;
    BOOL ok=sqlite3_prepare_v2(r->db,sql,-1,&s,NULL)==SQLITE_OK;
    for(int i=0;ok&&i<count;i++)ok=sqlite3_bind_text(s,i+1,values[i],-1,SQLITE_TRANSIENT)==SQLITE_OK;
    if(ok)ok=sqlite3_step(s)==SQLITE_DONE;
    sqlite3_finalize(s);
    return ok||sy_fail(r,SYNC_STORAGE);
}
char *sy_query(SyncRun *r,const char *sql,const char *a,const char *b){
    sqlite3_stmt *s=NULL;
    char *out=NULL;
    if(sqlite3_prepare_v2(r->db,sql,-1,&s,NULL)!=SQLITE_OK){sy_fail(r,SYNC_STORAGE);
        return NULL;
        }
    if(a)sqlite3_bind_text(s,1,a,-1,SQLITE_STATIC);
    if(b)sqlite3_bind_text(s,2,b,-1,SQLITE_STATIC);
    int rc=sqlite3_step(s);
    if(rc==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_TEXT)out=strdup((const char*)sqlite3_column_text(s,0));
    else if(rc!=SQLITE_DONE&&rc!=SQLITE_ROW)sy_fail(r,SYNC_STORAGE);
    sqlite3_finalize(s);
    return out;
}
BOOL sy_index_open(SyncRun *r){
    wchar_t path[SYNC_PATH_CAP];
    if(!sy_mkdirs(r->request->state_directory)||!sy_join(r->cache,r->request->state_directory,L"chunks")||!sy_join(r->partial,r->request->state_directory,L"downloads")||!sy_mkdirs(r->cache)||!sy_mkdirs(r->partial)||!sy_join(path,r->request->state_directory,L"client.sqlite3"))return sy_fail(r,SYNC_PATH);
    DWORD attr=GetFileAttributesW(path);
    if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))return sy_fail(r,SYNC_PATH);
    char *utf=sync_utf8(path);
    BOOL ok=utf&&sqlite3_open(utf,&r->db)==SQLITE_OK;
    free(utf);
    if(!ok)return sy_fail(r,SYNC_STORAGE);
    sqlite3_busy_timeout(r->db,30000);
    sqlite3_limit(r->db,SQLITE_LIMIT_LENGTH,SYNC_JSON_MAX);
    sqlite3_stmt *version=NULL;
    ok=sqlite3_prepare_v2(r->db,"PRAGMA user_version",-1,&version,NULL)==SQLITE_OK;
    if(ok)ok=sqlite3_step(version)==SQLITE_ROW&&sqlite3_column_int(version,0)>=0&&sqlite3_column_int(version,0)<=1;
    sqlite3_finalize(version);
    if(!ok)return sy_fail(r,SYNC_STORAGE);
    ok=sy_exec(r,"PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON; BEGIN IMMEDIATE;"
        "CREATE TABLE IF NOT EXISTS files(dataset TEXT NOT NULL,path_key TEXT NOT NULL,path TEXT NOT NULL,job TEXT NOT NULL,manifest TEXT NOT NULL,stat_size INTEGER NOT NULL,stat_mtime INTEGER NOT NULL,PRIMARY KEY(dataset,path_key));"
        "CREATE TABLE IF NOT EXISTS outbox(id TEXT PRIMARY KEY,dataset TEXT NOT NULL,manifest TEXT NOT NULL,source_size INTEGER NOT NULL,source_mtime INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS inbox(id TEXT PRIMARY KEY,dataset TEXT NOT NULL,manifest TEXT NOT NULL,mhash TEXT NOT NULL,phase TEXT NOT NULL);");
    if(ok)ok=sy_exec(r,"CREATE TABLE IF NOT EXISTS versions(id TEXT PRIMARY KEY,dataset TEXT NOT NULL,path TEXT NOT NULL,manifest TEXT NOT NULL);");
    const char *tables[]={"files","outbox","inbox"};
    const char *columns[]={"dataset,path_key,path,job,manifest,stat_size,stat_mtime","id,dataset,manifest,source_size,source_mtime","id,dataset,manifest,mhash,phase"};
    for(int i=0;i<3&&ok;i++){
        char sql[128];
        snprintf(sql,sizeof(sql),"SELECT group_concat(name,',') FROM pragma_table_info('%s')",tables[i]);
        char *actual=sy_query(r,sql,NULL,NULL);
        ok=actual&&!strcmp(actual,columns[i]);
        free(actual);
    }
    if(ok)ok=sy_exec(r,"PRAGMA user_version=1; COMMIT;");
    if(!ok){sqlite3_exec(r->db,"ROLLBACK",NULL,NULL,NULL);
        return sy_fail(r,SYNC_STORAGE);
        }
    sqlite3_stmt *counts=NULL;
    if(sqlite3_prepare_v2(r->db,"SELECT (SELECT count(*) FROM files),(SELECT count(*) FROM outbox),(SELECT count(*) FROM inbox)",-1,&counts,NULL)==SQLITE_OK&&sqlite3_step(counts)==SQLITE_ROW)
        nova_log_write(NOVA_LOG_INFO,"sync","index_open",L"run=%ld existing=%d files=%lld outbox=%lld inbox=%lld",r->log_id,attr!=INVALID_FILE_ATTRIBUTES,
            (long long)sqlite3_column_int64(counts,0),(long long)sqlite3_column_int64(counts,1),(long long)sqlite3_column_int64(counts,2));
    sqlite3_finalize(counts);
    return TRUE;
}
