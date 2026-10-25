#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
/* Older MinGW defaults to MSVCRT formatting without C99 integer specifiers. */
#define __USE_MINGW_ANSI_STDIO 1
#include "sync_internal.h"
#include <openssl/evp.h>
#include <shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static BOOL command(SyncRun *r,const char *action,const char *key,const char *value,SyncReply *reply){char *text=sy_fields(action,key,value,NULL,NULL);
    BOOL ok=text&&sy_rpc(r,text,NULL,0,reply);
    free(text);
    return ok;
    }
static BOOL cache_path(SyncRun *r,const char *hash,wchar_t *path){
    if(!hash||strlen(hash)!=64||strspn(hash,"0123456789abcdef")!=64)return sy_fail(r,SYNC_PROTOCOL);
    wchar_t name[80];
    return (sync_wide(hash,name,80)&&sy_join(path,r->cache,name))||sy_fail(r,SYNC_PATH);
}
static BOOL target_path(SyncRun *r,const char *relative,wchar_t *path){
    wchar_t name[SYNC_PATH_CAP];
    if(!sync_wide(relative,name,SYNC_PATH_CAP)||!sync_relative_path(name)||!sy_join(path,r->request->pair.directory,name))return sy_fail(r,SYNC_PATH);
    for(wchar_t *p=path;*p;p++)if(*p==L'/')*p=L'\\';
    wchar_t prefix[SYNC_PATH_CAP];
    lstrcpyW(prefix,path);
    for(wchar_t *p=prefix+3;*p;p++)if(*p==L'\\'){*p=0;
        DWORD a=GetFileAttributesW(prefix);
        *p=L'\\';
        if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))return sy_fail(r,SYNC_PATH);
        }
    DWORD a=GetFileAttributesW(path);
    return a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_REPARSE_POINT)||sy_fail(r,SYNC_PATH);
}
static BOOL probe(SyncRun *r,const wchar_t *path,char hash[65],uint64_t *size,uint64_t *mtime,BOOL *exists){
    DWORD a=GetFileAttributesW(path);
    *exists=a!=INVALID_FILE_ATTRIBUTES;
    if(!*exists){DWORD e=GetLastError();
        hash[0]=0;
        *size=*mtime=0;
        return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND||sy_fail(r,SYNC_PATH);
        }
    return sync_hash_file(path,hash,size,mtime,r->request->cancel)||sy_fail(r,sy_cancel(r)?SYNC_CANCELLED:SYNC_PATH);
}
static BOOL pairs_array(SyncRun *r,const char *manifest,sqlite3_stmt **out){
    if(sqlite3_prepare_v2(r->db,"SELECT json_extract(value,'$.hash'),json_extract(value,'$.size') FROM json_each(?1,'$.chunks')",-1,out,NULL)!=SQLITE_OK)return sy_fail(r,SYNC_PROTOCOL);
    sqlite3_bind_text(*out,1,manifest,-1,SQLITE_STATIC);
    return TRUE;
}
static char *metadata(SyncRun *r,const char *relative,const char *parent,const char *sha,uint64_t size,uint64_t mtime,const char *chunks,BOOL deleting){
    unsigned char random[16];
    sqlite3_randomness(16,random);
    char id[33],role[2]={(char)('A'+r->request->settings.role),0},number[64];
    for(int i=0;i<16;i++)snprintf(id+i*2,3,"%02x",random[i]);
    char *dataset=sync_utf8(r->request->pair.dataset);
    SyncBuffer b={0};
    BOOL ok=dataset&&sync_buffer_add(&b,"{\"chunker\":\"gear-v1-128k-512k-2m\",\"chunks\":")&&sync_buffer_add(&b,chunks)&&sync_buffer_add(&b,",\"dataset\":")&&sync_buffer_string(&b,dataset)&&sync_buffer_add(&b,",\"format\":\"chunksync-v2\",\"id\":")&&sync_buffer_string(&b,id)&&sync_buffer_add(&b,",\"kind\":")&&sync_buffer_string(&b,deleting?"DELETE":"PUT");
    snprintf(number,sizeof(number),",\"mtime_ns\":%llu,\"origin\":",(unsigned long long)mtime);
    ok=ok&&sync_buffer_add(&b,number)&&sync_buffer_string(&b,role)&&sync_buffer_add(&b,",\"parent\":")&&(parent?sync_buffer_string(&b,parent):sync_buffer_add(&b,"null"))&&sync_buffer_add(&b,",\"path\":")&&sync_buffer_string(&b,relative)&&sync_buffer_add(&b,",\"sha256\":")&&(sha?sync_buffer_string(&b,sha):sync_buffer_add(&b,"null"));
    snprintf(number,sizeof(number),",\"size\":%llu}",(unsigned long long)size);
    ok=ok&&sync_buffer_add(&b,number);
    free(dataset);
    if(!ok){free(b.data);
        sy_fail(r,SYNC_STORAGE);
        return NULL;
        }return b.data;
}
static BOOL outbox_store(SyncRun *r,const char *manifest,uint64_t size,uint64_t mtime){
    char *id=sync_json_string(r->db,manifest,"$.id"),*dataset=sync_utf8(r->request->pair.dataset);
    sqlite3_stmt *s=NULL;
    BOOL ok=id&&dataset&&sqlite3_prepare_v2(r->db,"INSERT INTO outbox VALUES (?1,?2,?3,?4,?5)",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,id,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,dataset,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,3,manifest,-1,SQLITE_STATIC);
        sqlite3_bind_int64(s,4,(sqlite3_int64)size);
        sqlite3_bind_int64(s,5,(sqlite3_int64)mtime);
        ok=sqlite3_step(s)==SQLITE_DONE;
        }sqlite3_finalize(s);
    free(id);
    free(dataset);
    return ok||sy_fail(r,SYNC_STORAGE);
}
static char *snapshot_file(SyncRun *r,const wchar_t *path,const char *relative,const char *parent){
    uint64_t size,mtime;
    if(!sy_meta(path,&size,&mtime)){sy_fail(r,SYNC_PATH);
        return NULL;
        }
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(file==INVALID_HANDLE_VALUE){sy_fail(r,SYNC_PATH);
        return NULL;
        }
    unsigned char *input=malloc(SYNC_PACKET_MAX),*block=malloc(SYNC_CHUNK_MAX);
    EVP_MD_CTX *sha=EVP_MD_CTX_new();
    SyncBuffer chunks={0};
    SyncChunker chunker={0};
    size_t block_size=0;
    uint64_t done=0;
    BOOL first=TRUE;
    BOOL ok=input&&block&&sha&&EVP_DigestInit_ex(sha,EVP_sha256(),NULL)==1&&sync_buffer_add(&chunks,"[");
    while(ok&&!sy_cancel(r)){
        DWORD got=0;
        if(!ReadFile(file,input,SYNC_PACKET_MAX,&got,NULL)){ok=FALSE;
            break;
            }if(!got)break;
        ok=EVP_DigestUpdate(sha,input,got)==1;
        for(DWORD i=0;i<got&&ok;i++){
            block[block_size++]=input[i];
            if(sync_chunk_boundary(&chunker,input[i],r->gear)){
                char hash[65]={0},item[128];
                ok=sy_cache(r,block,block_size,hash);
                snprintf(item,sizeof(item),"%s{\"hash\":\"%s\",\"size\":%zu}",first?"":",",hash,block_size);
                ok=ok&&sync_buffer_add(&chunks,item);
                first=FALSE;
                block_size=0;
            }
        }
        done+=got;
        sy_progress(r,SYNC_SCAN,path,done,size);
    }
    if(ok&&block_size){char hash[65]={0},item[128];
        ok=sy_cache(r,block,block_size,hash);
        snprintf(item,sizeof(item),"%s{\"hash\":\"%s\",\"size\":%zu}",first?"":",",hash,block_size);
        ok=ok&&sync_buffer_add(&chunks,item);
        }
    unsigned char digest[32];
    unsigned int length=0;
    char hex[65];
    if(ok)ok=done==size&&EVP_DigestFinal_ex(sha,digest,&length)==1&&length==32&&sync_buffer_add(&chunks,"]");
    if(ok)for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    char *manifest=ok&&!sy_cancel(r)?metadata(r,relative,parent,hex,size,mtime,chunks.data,FALSE):NULL;
    if(manifest&&!outbox_store(r,manifest,size,mtime)){free(manifest);
        manifest=NULL;
        }
    CloseHandle(file);
    EVP_MD_CTX_free(sha);
    free(input);
    free(block);
    free(chunks.data);
    if(!manifest&&r->result->failure==SYNC_OK)sy_fail(r,SYNC_CACHE);
    return manifest;
}
static BOOL head_save(SyncRun *r,const char *manifest,uint64_t size,int64_t mtime){
    const char *fields[]={"$.dataset","$.path_key","$.path","$.id"};
    char *v[4]={0};
    BOOL ok=TRUE;
    for(int i=0;i<4;i++){v[i]=sync_json_string(r->db,manifest,fields[i]);
        if(!v[i])ok=FALSE;
        }
    sqlite3_stmt *s=NULL;
    ok=ok&&sqlite3_prepare_v2(r->db,"INSERT OR REPLACE INTO files VALUES (?1,?2,?3,?4,?5,?6,?7)",-1,&s,NULL)==SQLITE_OK;
    if(ok){for(int i=0;i<4;i++)sqlite3_bind_text(s,i+1,v[i],-1,SQLITE_STATIC);
        sqlite3_bind_text(s,5,manifest,-1,SQLITE_STATIC);
        sqlite3_bind_int64(s,6,(sqlite3_int64)size);
        sqlite3_bind_int64(s,7,mtime);
        ok=sqlite3_step(s)==SQLITE_DONE;
        }sqlite3_finalize(s);
    for(int i=0;i<4;i++)free(v[i]);
    return ok||sy_fail(r,SYNC_STORAGE);
}
static BOOL publish(SyncRun *r,const char *original){
    SyncReply plan={0},response={0};
    char *request=sync_json_patch(r->db,"{\"action\":\"prepare\",\"native\":true}","$.manifest",original,TRUE);
    BOOL ok=request&&sy_rpc(r,request,NULL,0,&plan);
    free(request);
    if(!ok)return FALSE;
    char *manifest=sync_json_string(r->db,plan.json,"$.manifest"),*id=sync_json_string(r->db,original,"$.id"),*state=sync_json_string(r->db,plan.json,"$.state");
    if(!manifest)manifest=strdup(original);
    char *key=sync_json_string(r->db,manifest,"$.path_key");
    if(!key||!id||!state){free(key);
        free(id);
        free(state);
        free(manifest);
        sy_reply_free(&plan);
        return sy_fail(r,SYNC_PROTOCOL);
        }free(key);
    const char *update[]={manifest,id};
    ok=sy_statement(r,"UPDATE outbox SET manifest=?1 WHERE id=?2",update,2);
    r->result->stats.reused+=(uint64_t)sync_json_int(r->db,plan.json,"$.receiver_reused_bytes",0);
    sqlite3_stmt *items=NULL;
    unsigned char *data=malloc(SYNC_PACKET_MAX);
    ok=ok&&data;
    if(ok&&!strcmp(state,"STAGING")){
        ok=sqlite3_prepare_v2(r->db,"SELECT json_extract(value,'$.hash'),json_extract(value,'$.size') FROM json_each(?1,'$.needed')",-1,&items,NULL)==SQLITE_OK;
        if(ok)sqlite3_bind_text(items,1,plan.json,-1,SQLITE_STATIC);
        while(ok&&sqlite3_step(items)==SQLITE_ROW){
            const char *hash=(const char*)sqlite3_column_text(items,0);
            uint64_t total=(uint64_t)sqlite3_column_int64(items,1);
            wchar_t path[SYNC_PATH_CAP];
            char actual[65];
            uint64_t n=0;
            ok=cache_path(r,hash,path)&&sync_hash_file(path,actual,&n,NULL,r->request->cancel)&&n==total&&!strcmp(actual,hash);
            if(!ok){sy_fail(r,SYNC_CACHE);
                break;
                }
            request=sy_fields("put","job",id,"hash",hash);
            ok=request&&sy_rpc(r,request,NULL,0,&response);
            if(!ok){free(request);
                break;
                }uint64_t offset=(uint64_t)sync_json_int(r->db,response.json,"$.offset",-1);
            sy_reply_free(&response);
            r->result->stats.resumed+=offset;
            HANDLE f=ok?CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL):INVALID_HANDLE_VALUE;
            if(f==INVALID_HANDLE_VALUE)ok=FALSE;
            int resets=0;
            while(ok&&offset<total&&!sy_cancel(r)){
                sy_progress(r,SYNC_UPLOAD,path,offset,total);
                LARGE_INTEGER position;
                position.QuadPart=(LONGLONG)offset;
                DWORD got=0;
                ok=SetFilePointerEx(f,position,NULL,FILE_BEGIN)&&ReadFile(f,data,(DWORD)min((uint64_t)SYNC_PACKET_MAX,total-offset),&got,NULL)&&got>0;
                char number[32];
                snprintf(number,sizeof(number),"%llu",(unsigned long long)offset);
                char *packet=sync_json_patch(r->db,request,"$.offset",number,TRUE);
                ok=ok&&packet&&sy_rpc(r,packet,data,got,&response);
                free(packet);
                if(ok){offset=(uint64_t)sync_json_int(r->db,response.json,"$.offset",-1);
                    if(sync_json_int(r->db,response.json,"$.hash_mismatch",0)&&++resets>1)ok=sy_fail(r,SYNC_CACHE);
                    if(offset>total)ok=sy_fail(r,SYNC_PROTOCOL);
                    }sy_reply_free(&response);
            }
            if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
            free(request);
            if(sy_cancel(r))ok=FALSE;
        }
    }
    sqlite3_finalize(items);
    free(data);
    if(ok)ok=command(r,"commit","job",id,&response);
    sy_reply_free(&response);
    if(ok){
        uint64_t size=(uint64_t)sync_json_int(r->db,manifest,"$.size",0),mtime=(uint64_t)sync_json_int(r->db,manifest,"$.mtime_ns",0);
        const char *args[]={id};
        ok=sy_exec(r,"BEGIN IMMEDIATE")&&head_save(r,manifest,size,(int64_t)mtime)&&sy_statement(r,"DELETE FROM outbox WHERE id=?1",args,1)&&sy_exec(r,"COMMIT");
        if(!ok)sqlite3_exec(r->db,"ROLLBACK",NULL,NULL,NULL);
        else r->result->stats.published++;
    }
    free(manifest);
    free(id);
    free(state);
    sy_reply_free(&plan);
    return ok&&!sy_cancel(r);
}
static BOOL cached(SyncRun *r,const char *hash,uint64_t size){wchar_t path[SYNC_PATH_CAP];
    char hex[65];
    uint64_t n;
    return cache_path(r,hash,path)&&sync_hash_file(path,hex,&n,NULL,r->request->cancel)&&n==size&&!strcmp(hex,hash);
    }
static BOOL seed(SyncRun *r,const wchar_t *path,const char *manifest){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(f==INVALID_HANDLE_VALUE)return sy_fail(r,SYNC_PATH);
    sqlite3_stmt *s=NULL;
    unsigned char *data=malloc(SYNC_CHUNK_MAX);
    BOOL ok=data&&pairs_array(r,manifest,&s);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){const char *expected=(const char*)sqlite3_column_text(s,0);
        uint64_t size=(uint64_t)sqlite3_column_int64(s,1);
        DWORD n=0;
        char hash[65];
        ok=size>0&&size<=SYNC_CHUNK_MAX&&ReadFile(f,data,(DWORD)size,&n,NULL)&&n==size&&sy_cache(r,data,(size_t)size,hash)&&!strcmp(hash,expected)&&!sy_cancel(r);
        }
    sqlite3_finalize(s);
    free(data);
    CloseHandle(f);
    return ok||sy_fail(r,SYNC_CACHE);
}
static BOOL download(SyncRun *r,const char *job,const char *hash,uint64_t total,const wchar_t *work){
    if(total>SYNC_CHUNK_MAX)return sy_fail(r,SYNC_PROTOCOL);
    wchar_t target[SYNC_PATH_CAP];
    if(!cache_path(r,hash,target))return FALSE;
    if(GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES){if(!cached(r,hash,total))return sy_fail(r,SYNC_CACHE);
        r->result->stats.reused+=total;
        return TRUE;
        }
    wchar_t name[80],part[SYNC_PATH_CAP];
    swprintf(name,80,L"%hs.part",hash);
    if(!sy_join(part,r->partial,name))return sy_fail(r,SYNC_PATH);
    DWORD attributes=GetFileAttributesW(part);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return sy_fail(r,SYNC_PATH);
    HANDLE file=CreateFileW(part,GENERIC_WRITE|GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE)return sy_fail(r,SYNC_CACHE);
    LARGE_INTEGER size;
    BOOL ok=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&(uint64_t)size.QuadPart<=total;
    uint64_t offset=ok?(uint64_t)size.QuadPart:0;
    r->result->stats.resumed+=offset;
    SetFilePointerEx(file,size,NULL,FILE_BEGIN);
    char *base=sy_fields("get","job",job,"hash",hash);
    while(ok&&offset<total&&!sy_cancel(r)){
        sy_progress(r,SYNC_DOWNLOAD,work,offset,total);
        char number[32];
        snprintf(number,sizeof(number),"%llu",(unsigned long long)offset);
        char *text=base?sync_json_patch(r->db,base,"$.offset",number,TRUE):NULL;
        SyncReply reply={0};
        ok=text&&sy_rpc(r,text,NULL,0,&reply);
        free(text);
        if(ok){DWORD n=0;
            ok=sync_json_int(r->db,reply.json,"$.offset",-1)==(int64_t)offset&&reply.size>0&&offset+reply.size<=total&&WriteFile(file,reply.data,(DWORD)reply.size,&n,NULL)&&n==reply.size&&FlushFileBuffers(file);
            if(ok)offset+=reply.size;
            }sy_reply_free(&reply);
    }
    free(base);
    CloseHandle(file);
    if(sy_cancel(r))return FALSE;
    char actual[65];
    uint64_t n;
    ok=ok&&sync_hash_file(part,actual,&n,NULL,r->request->cancel)&&n==total&&!strcmp(actual,hash);
    if(ok)ok=MoveFileExW(part,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    return ok||sy_fail(r,SYNC_CACHE);
}
static BOOL acknowledge(SyncRun *r,const char *manifest,const char *mhash){
    sqlite3_stmt *s=NULL;
    BOOL ok=pairs_array(r,manifest,&s);
    while(ok&&sqlite3_step(s)==SQLITE_ROW)ok=cached(r,(const char*)sqlite3_column_text(s,0),(uint64_t)sqlite3_column_int64(s,1));
    sqlite3_finalize(s);
    if(!ok)return sy_fail(r,SYNC_CACHE);
    char *id=sync_json_string(r->db,manifest,"$.id"),*text=id?sy_fields("ack","job",id,"mhash",mhash):NULL;
    SyncReply response={0};
    ok=text&&sy_rpc(r,text,NULL,0,&response);
    free(text);
    if(ok){r->result->stats.reclaimed+=(uint64_t)sync_json_int(r->db,response.json,"$.gc.removed_bytes",0);
        const char *values[]={id};
        ok=sy_statement(r,"DELETE FROM inbox WHERE id=?1",values,1);
        }free(id);
    sy_reply_free(&response);
    return ok;
}
static BOOL receive_one(SyncRun *r,const char *manifest,const char *mhash){
    char verified[65];
    if(!manifest||!mhash||strlen(mhash)!=64||!sync_json_valid(r->db,manifest)||!sync_sha(manifest,strlen(manifest),verified)||strcmp(verified,mhash))return sy_fail(r,SYNC_PROTOCOL);
    char *id=sync_json_string(r->db,manifest,"$.id"),*dataset=sync_json_string(r->db,manifest,"$.dataset"),*relative=sync_json_string(r->db,manifest,"$.path"),*key=sync_json_string(r->db,manifest,"$.path_key"),*kind=sync_json_string(r->db,manifest,"$.kind"),*expected=sync_json_string(r->db,manifest,"$.sha256"),*parent=sync_json_string(r->db,manifest,"$.parent");
    char *current=NULL,*previous=NULL,*old_id=NULL,*old_hash=NULL,*phase=NULL;
    BOOL ok=id&&dataset&&relative&&key&&kind;
    char *local_dataset=sync_utf8(r->request->pair.dataset);
    ok=ok&&local_dataset&&!strcmp(dataset,local_dataset);
    free(local_dataset);
    wchar_t path[SYNC_PATH_CAP];
    if(ok)ok=target_path(r,relative,path);
    if(ok)phase=sy_query(r,"SELECT phase FROM inbox WHERE id=?1",id,NULL);
    if(ok&&phase&&!strcmp(phase,"APPLIED")){ok=acknowledge(r,manifest,mhash);
        goto cleanup;
        }
    if(ok){previous=sy_query(r,"SELECT manifest FROM files WHERE dataset=?1 AND path_key=?2",dataset,key);
        if(previous){old_id=sync_json_string(r->db,previous,"$.id");
            old_hash=sync_json_string(r->db,previous,"$.sha256");
            }ok=(!parent&&!old_id)||(parent&&old_id&&!strcmp(parent,old_id));
        if(!ok)sy_fail(r,SYNC_CONFLICT);
        }
    BOOL exists=FALSE,already=FALSE;
    uint64_t actual_size=0,actual_time=0,total=(uint64_t)sync_json_int(r->db,manifest,"$.size",-1);
    char actual[65]={0};
    if(ok)ok=probe(r,path,actual,&actual_size,&actual_time,&exists);
    if(ok){already=phase&&!strcmp(phase,"READY")&&((expected&&exists&&!strcmp(actual,expected)&&actual_size==total)||(!expected&&!exists));
        BOOL adopted=!previous&&expected&&exists&&!strcmp(actual,expected)&&actual_size==total;
        if(!already&&!adopted&&!((old_hash&&exists&&!strcmp(actual,old_hash))||(!old_hash&&!exists)))ok=sy_fail(r,SYNC_CONFLICT);
        if(ok&&adopted){ok=seed(r,path,manifest);
            already=ok;
            }
        if(ok&&old_hash&&exists&&!strcmp(actual,old_hash))ok=seed(r,path,previous);
    }
    if(ok&&!strcmp(kind,"DELETE")&&!r->request->pair.sync_deletes)ok=sy_fail(r,SYNC_DELETE);
    if(ok){const char *values[]={id,dataset,manifest,mhash};
        ok=sy_statement(r,"INSERT OR IGNORE INTO inbox VALUES (?1,?2,?3,?4,'DOWNLOADING')",values,4);
        }
    sqlite3_stmt *chunks=NULL;
    if(ok)ok=pairs_array(r,manifest,&chunks);
    while(ok&&sqlite3_step(chunks)==SQLITE_ROW)ok=download(r,id,(const char*)sqlite3_column_text(chunks,0),(uint64_t)sqlite3_column_int64(chunks,1),path);
    sqlite3_finalize(chunks);
    if(ok){const char *values[]={id};
        ok=sy_statement(r,"UPDATE inbox SET phase='READY' WHERE id=?1",values,1);
        }
    if(ok&&!already&&!strcmp(kind,"PUT")){
        sy_progress(r,SYNC_APPLY,path,0,total);
        wchar_t directory[SYNC_PATH_CAP],temporary[SYNC_PATH_CAP];
        lstrcpyW(directory,path);
        wchar_t *slash=wcsrchr(directory,L'\\');
        if(!slash)ok=FALSE;
        else *slash=0;
        wchar_t name[112];
        swprintf(name,112,L".nova-sync-%hs.tmp",id);
        ok=ok&&sy_mkdirs(directory)&&sy_join(temporary,directory,name);
        DWORD attr=ok?GetFileAttributesW(temporary):0;
        if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))ok=FALSE;
        HANDLE file=ok?CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL):INVALID_HANDLE_VALUE;
        ok=ok&&file!=INVALID_HANDLE_VALUE;
        unsigned char *buffer=malloc(SYNC_PACKET_MAX);
        chunks=NULL;
        if(ok)ok=buffer&&pairs_array(r,manifest,&chunks);
        while(ok&&sqlite3_step(chunks)==SQLITE_ROW){wchar_t cached_path[SYNC_PATH_CAP];
            ok=cache_path(r,(const char*)sqlite3_column_text(chunks,0),cached_path);
            HANDLE input=ok?CreateFileW(cached_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL):INVALID_HANDLE_VALUE;
            ok=ok&&input!=INVALID_HANDLE_VALUE;
            while(ok&&!sy_cancel(r)){DWORD got=0,wrote=0;
                if(!ReadFile(input,buffer,SYNC_PACKET_MAX,&got,NULL)){ok=FALSE;
                    break;
                    }if(!got)break;
                ok=WriteFile(file,buffer,got,&wrote,NULL)&&wrote==got;
                }if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
            if(sy_cancel(r))ok=FALSE;
        }
        sqlite3_finalize(chunks);
        free(buffer);
        if(file!=INVALID_HANDLE_VALUE){if(ok)ok=FlushFileBuffers(file);
            CloseHandle(file);
            }
        char final[65],now[65];
        uint64_t count,t;
        BOOL now_exists=FALSE;
        if(ok)ok=sync_hash_file(temporary,final,&count,NULL,r->request->cancel)&&count==total&&expected&&!strcmp(final,expected)&&probe(r,path,now,&count,&t,&now_exists)&&now_exists==exists&&(!exists||!strcmp(now,actual));
        if(ok)ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok&&r->result->failure==SYNC_OK)sy_fail(r,SYNC_PATH);
    }else if(ok&&!already&&!strcmp(kind,"DELETE")&&exists){ok=r->request->recycle&&r->request->recycle(r->request->context,path,actual_size,actual_time);
        if(!ok)sy_fail(r,SYNC_DELETE);
        }
    if(ok){
        uint64_t size=0,mtime=0;
        BOOL present=FALSE;
        char final[65];
        ok=probe(r,path,final,&size,&mtime,&present);
        BOOL stable=ok&&expected&&present&&!strcmp(expected,final)&&size==total;
        const char *values[]={id};
        ok=ok&&sy_exec(r,"BEGIN IMMEDIATE")&&head_save(r,manifest,stable?size:0,stable?(int64_t)mtime:-1)&&sy_statement(r,"UPDATE inbox SET phase='APPLIED' WHERE id=?1",values,1)&&sy_exec(r,"COMMIT");
        if(!ok)sqlite3_exec(r->db,"ROLLBACK",NULL,NULL,NULL);
        if(ok){sy_progress(r,SYNC_CONFIRM,path,total,total);
            ok=acknowledge(r,manifest,mhash);
            if(ok)r->result->stats.downloaded++;
            }
    }
cleanup:
    free(current);
    free(previous);
    free(old_id);
    free(old_hash);
    free(phase);
    free(id);
    free(dataset);
    free(relative);
    free(key);
    free(kind);
    free(expected);
    free(parent);
    return ok;
}
static BOOL receive_all(SyncRun *r){
    char *dataset=sync_utf8(r->request->pair.dataset);
    char *pending=dataset?sy_query(r,"SELECT json_group_array(json_object('manifest',json(manifest),'mhash',mhash)) FROM inbox WHERE dataset=?1",dataset,NULL):NULL;
    BOOL ok=dataset&&pending;
    sqlite3_stmt *s=NULL;
    if(ok)ok=sqlite3_prepare_v2(r->db,"SELECT json_extract(value,'$.manifest'),json_extract(value,'$.mhash') FROM json_each(?1)",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,pending,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW)ok=receive_one(r,(const char*)sqlite3_column_text(s,0),(const char*)sqlite3_column_text(s,1));
    sqlite3_finalize(s);
    free(pending);
    SyncReply response={0};
    if(ok)ok=command(r,"pending","dataset",dataset,&response);
    free(dataset);
    s=NULL;
    if(ok)ok=sqlite3_prepare_v2(r->db,"SELECT json_extract(value,'$.manifest'),json_extract(value,'$.mhash') FROM json_each(?1,'$.jobs')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,response.json,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW)ok=receive_one(r,(const char*)sqlite3_column_text(s,0),(const char*)sqlite3_column_text(s,1));
    sqlite3_finalize(s);
    sy_reply_free(&response);
    return ok;
}
static BOOL push_file(SyncRun *r,const wchar_t *path,const wchar_t *relative,BOOL deleting){
    char *name=sync_utf8(relative),*dataset=sync_utf8(r->request->pair.dataset);
    if(!name||!dataset){free(name);
        free(dataset);
        return sy_fail(r,SYNC_PATH);
        }
    char *head=sy_query(r,"SELECT manifest FROM files WHERE dataset=?1 AND path=?2",dataset,name);
    char *parent=head?sync_json_string(r->db,head,"$.id"):NULL;
    uint64_t size=0,mtime=0;
    BOOL ok=TRUE;
    char *manifest=NULL;
    if(deleting){char *old=head?sync_json_string(r->db,head,"$.kind"):NULL;
        if(old&&!strcmp(old,"PUT")){manifest=metadata(r,name,parent,NULL,0,0,"[]",TRUE);
            ok=manifest&&outbox_store(r,manifest,0,0);
            }free(old);
        }
    else{
        ok=sy_meta(path,&size,&mtime);
        char *saved=sy_query(r,"SELECT json_object('size',stat_size,'mtime',stat_mtime) FROM files WHERE dataset=?1 AND path=?2",dataset,name);
        if(ok&&saved&&sync_json_int(r->db,saved,"$.size",-1)==(int64_t)size&&sync_json_int(r->db,saved,"$.mtime",-1)==(int64_t)mtime)r->result->stats.unchanged++;
        else if(ok)manifest=snapshot_file(r,path,name,parent);
        free(saved);
        if(!ok)sy_fail(r,SYNC_PATH);
    }
    if(manifest){ok=publish(r,manifest);
        free(manifest);
        }else if(r->result->failure!=SYNC_OK)ok=FALSE;
    free(parent);
    free(head);
    free(name);
    free(dataset);
    return ok;
}
static BOOL excluded(SyncRun *r,const wchar_t *relative,const wchar_t *name){return !wcscmp(name,L".git")||PathMatchSpecW(relative,r->request->pair.excludes)||PathMatchSpecW(name,r->request->pair.excludes)||!wcsncmp(name,L".nova-sync-",11);
    }
static BOOL scan(SyncRun *r,const wchar_t *directory,const wchar_t *relative,int depth,int *count){
    if(depth>32)return sy_fail(r,SYNC_PATH);
    wchar_t mask[SYNC_PATH_CAP];
    if(!sy_join(mask,directory,L"*"))return sy_fail(r,SYNC_PATH);
    WIN32_FIND_DATAW data;
    HANDLE find=FindFirstFileW(mask,&data);
    if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND||sy_fail(r,SYNC_PATH);
    BOOL ok=TRUE;
    do{
        if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L"..")||(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))continue;
        wchar_t path[SYNC_PATH_CAP],name[SYNC_PATH_CAP];
        if(relative[0]){int n=swprintf(name,SYNC_PATH_CAP,L"%ls/%ls",relative,data.cFileName);
            if(n<0||n>=SYNC_PATH_CAP){ok=FALSE;
                break;
                }}else lstrcpyW(name,data.cFileName);
        if(excluded(r,name,data.cFileName))continue;
        if(!sy_join(path,directory,data.cFileName)||!sync_relative_path(name)){ok=FALSE;
            break;
            }if(PathIsPrefixW(r->request->state_directory,path))continue;
        if(++*count>100000||sy_cancel(r)){ok=FALSE;
            break;
            }ok=(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?scan(r,path,name,depth+1,count):push_file(r,path,name,FALSE);
    }while(ok&&FindNextFileW(find,&data));
    FindClose(find);
    return ok||(r->result->failure!=SYNC_OK?FALSE:sy_fail(r,SYNC_PATH));
}
static BOOL bind_pair(SyncRun *r){
    char *dataset=sync_utf8(r->request->pair.dataset),*name=sync_utf8(r->request->pair.group),*path=sync_utf8(r->request->pair.directory);
    SyncReply response={0};
    char *text=dataset&&name?sy_fields("dataset_create","dataset",dataset,"name",name):NULL;
    BOOL ok=text&&sy_rpc(r,text,NULL,0,&response);
    free(text);
    sy_reply_free(&response);
    text=dataset&&name?sy_fields("bind","dataset",dataset,"name",name):NULL;
    char *with_path=text&&path?sync_json_patch(r->db,text,"$.path",path,FALSE):NULL;
    free(text);
    if(ok)ok=with_path&&sy_rpc(r,with_path,NULL,0,&response);
    free(with_path);
    sy_reply_free(&response);
    free(dataset);
    free(name);
    free(path);
    return ok;
}
BOOL sy_sync(SyncRun *r){
    if(!bind_pair(r))return FALSE;
    char *dataset=sync_utf8(r->request->pair.dataset);
    char *outbox=dataset?sy_query(r,"SELECT json_group_array(json(manifest)) FROM outbox WHERE dataset=?1",dataset,NULL):NULL;
    free(dataset);
    sqlite3_stmt *s=NULL;
    BOOL ok=outbox&&sqlite3_prepare_v2(r->db,"SELECT value FROM json_each(?1)",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,outbox,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW)ok=publish(r,(const char*)sqlite3_column_text(s,0));
    sqlite3_finalize(s);
    free(outbox);
    if(ok)ok=receive_all(r);
    int count=0;
    if(ok)ok=scan(r,r->request->pair.directory,L"",0,&count);
    if(ok&&r->request->pair.sync_deletes){dataset=sync_utf8(r->request->pair.dataset);
        char *known=dataset?sy_query(r,"SELECT json_group_array(path) FROM files WHERE dataset=?1",dataset,NULL):NULL;
        free(dataset);
        s=NULL;
        ok=known&&sqlite3_prepare_v2(r->db,"SELECT value FROM json_each(?1)",-1,&s,NULL)==SQLITE_OK;
        if(ok)sqlite3_bind_text(s,1,known,-1,SQLITE_STATIC);
        while(ok&&sqlite3_step(s)==SQLITE_ROW){wchar_t path[SYNC_PATH_CAP],relative[SYNC_PATH_CAP];
            const char *p=(const char*)sqlite3_column_text(s,0);
            ok=target_path(r,p,path)&&sync_wide(p,relative,SYNC_PATH_CAP);
            if(ok&&GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)&&!excluded(r,relative,relative))ok=push_file(r,path,relative,TRUE);
            }sqlite3_finalize(s);
        free(known);
    }
    return ok&&!sy_cancel(r);
}
BOOL sync_client_run(SyncRequest *request,int action,SyncResult *result){
    if(action>=SYNC_JOB_SERVER_PROBE)return sync_server_run(request,action,result);
    memset(result,0,sizeof(*result));
    SyncRun r={0};
    r.request=request;
    r.result=result;
    BOOL ok=sy_index_open(&r);
    for(int i=0;i<256&&ok;i++){unsigned char data=(unsigned char)i,digest[32];
        unsigned int n=0;
        ok=EVP_Digest(&data,1,digest,&n,EVP_sha256(),NULL)==1&&n==32;
        for(int j=0;j<8;j++)r.gear[i]|=(uint64_t)digest[j]<<(8*j);
        }
    if(ok){sy_progress(&r,SYNC_CONNECT,NULL,0,0);
        r.ssh=ssh_client_connect(&request->login,request->cancel,&result->ssh);
        SecureZeroMemory(request->login.secret,sizeof(request->login.secret));
        ok=r.ssh!=NULL;
        if(!ok)sy_fail(&r,result->ssh.cancelled?SYNC_CANCELLED:SYNC_NETWORK);
        }
    if(ok)ok=ssh_client_protocol_open(r.ssh,request->settings.project,request->settings.broker,&result->ssh);
    SyncReply reply={0};
    if(ok&&action==SYNC_JOB_REGISTER){char *name=sync_utf8(request->settings.name);
        ok=name&&command(&r,"register","name",name,&reply);
        free(name);
        sy_reply_free(&reply);
        }
    if(ok&&action==SYNC_JOB_RUN)ok=sy_sync(&r);
    if(ok&&action==SYNC_JOB_BIND)ok=bind_pair(&r);
    if(ok&&action==SYNC_JOB_UNBIND){char *dataset=sync_utf8(request->pair.dataset);
        ok=dataset&&command(&r,"unbind","dataset",dataset,&reply);
        free(dataset);
        sy_reply_free(&reply);
        }
    if(ok)ok=command(&r,action==SYNC_JOB_REFRESH&&!request->settings.registered?"ping":"status",NULL,NULL,&reply);
    if(ok){result->cloud=reply.json;
        reply.json=NULL;
        sy_progress(&r,SYNC_FINISH,NULL,0,0);
        }sy_reply_free(&reply);
    if(!ok&&result->failure==SYNC_OK)result->failure=SYNC_NETWORK;
    if(r.ssh)ssh_client_close(r.ssh);
    if(r.db)sqlite3_close(r.db);
    SecureZeroMemory(request->login.secret,sizeof(request->login.secret));
    return ok;
}
void sync_result_dispose(SyncResult *r){ssh_result_dispose(&r->ssh);
    free(r->cloud);
    free(r->route);
    memset(r,0,sizeof(*r));
    }
