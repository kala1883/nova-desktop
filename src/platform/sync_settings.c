#include "sync_settings.h"
#include "storage.h"
#include "../../third_party/sqlite/sqlite3.h"
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <errno.h>
static const wchar_t *scope=L"sync";
static BOOL read_value(const wchar_t *section,const wchar_t *key,wchar_t *out,int cap){return store_get(scope,section,key,L"",out,cap)&&out[0];
    }
BOOL sync_settings_load(SyncSettings *s){
    sync_settings_default(s);
    wchar_t version[32];
    if(!store_get(scope,L"Manager",L"Version",L"",version,32))return FALSE;
    if(!version[0])return TRUE;
    if(wcscmp(version,L"1"))return FALSE;
    if(!store_get(scope,L"Manager",L"Instance",L"",s->server_instance,SYNC_ID_CAP))return FALSE;
    s->count=store_int(scope,L"Manager",L"Count",-1);
    s->role=store_int(scope,L"Manager",L"Role",-1);
    s->interval=store_int(scope,L"Manager",L"Interval",-1);
    int registered=store_int(scope,L"Manager",L"Registered",-1);
    if(registered<0||registered>1)return FALSE;
    s->registered=registered==1;
    wchar_t id[32],*end=NULL;
    if(!read_value(L"Manager",L"Server",id,32))return FALSE;
    errno=0;
    s->server_id=wcstoll(id,&end,10);
    if(!end||*end||errno)return FALSE;
    if(!store_get(scope,L"Manager",L"Node",L"",s->node_id,SYNC_ID_CAP)||!store_get(scope,L"Manager",L"Name",L"",s->name,SYNC_NAME_CAP)||
       !read_value(L"Manager",L"Project",s->project,SYNC_PATH_CAP)||!read_value(L"Manager",L"Broker",s->broker,SYNC_PATH_CAP)||s->count<0||s->count>SYNC_PAIR_LIMIT)return FALSE;
    for(int i=0;i<s->count;i++){
        wchar_t section[32];
        swprintf(section,32,L"Pair%d",i);
        SyncPair *p=&s->pairs[i];
        if(!read_value(section,L"Id",p->id,SYNC_ID_CAP)||!read_value(section,L"Dataset",p->dataset,SYNC_ID_CAP)||!read_value(section,L"Name",p->name,SYNC_NAME_CAP)||
           !read_value(section,L"Group",p->group,SYNC_NAME_CAP)||!read_value(section,L"Path",p->directory,SYNC_PATH_CAP)||!store_get(scope,section,L"Excludes",L"",p->excludes,SYNC_FILTER_CAP))return FALSE;
        int paused=store_int(scope,section,L"Paused",-1),deletes=store_int(scope,section,L"Deletes",-1);
        if(paused<0||paused>1||deletes<0||deletes>1)return FALSE;
        p->paused=paused!=0;
        p->sync_deletes=deletes!=0;
    }
    return sync_settings_valid(s);
}
BOOL sync_settings_save(const SyncSettings *s){
    if(!sync_settings_valid(s)||!store_begin())return FALSE;
    BOOL ok=store_clear(scope)&&store_set_int(scope,L"Manager",L"Version",1)&&store_set_int(scope,L"Manager",L"Count",s->count)&&
        store_set_int(scope,L"Manager",L"Role",s->role)&&store_set_int(scope,L"Manager",L"Interval",s->interval)&&store_set_int(scope,L"Manager",L"Registered",s->registered);
    ok=ok&&store_set(scope,L"Manager",L"Instance",s->server_instance);
    wchar_t id[32];
    swprintf(id,32,L"%lld",(long long)s->server_id);
    ok=ok&&store_set(scope,L"Manager",L"Server",id)&&store_set(scope,L"Manager",L"Node",s->node_id)&&store_set(scope,L"Manager",L"Name",s->name)&&store_set(scope,L"Manager",L"Project",s->project)&&store_set(scope,L"Manager",L"Broker",s->broker);
    for(int i=0;i<s->count&&ok;i++){
        wchar_t section[32];
        swprintf(section,32,L"Pair%d",i);
        const SyncPair *p=&s->pairs[i];
        ok=store_set(scope,section,L"Id",p->id)&&store_set(scope,section,L"Dataset",p->dataset)&&store_set(scope,section,L"Name",p->name)&&store_set(scope,section,L"Group",p->group)&&
            store_set(scope,section,L"Path",p->directory)&&store_set(scope,section,L"Excludes",p->excludes)&&store_set_int(scope,section,L"Paused",p->paused)&&store_set_int(scope,section,L"Deletes",p->sync_deletes);
    }
    return store_end(ok);
}
static BOOL json_wide(sqlite3 *db,const char *json,const char *path,wchar_t *out,int cap){
    sqlite3_stmt *s=NULL;
    BOOL ok=sqlite3_prepare_v2(db,"SELECT json_extract(?1,?2)",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,path,-1,SQLITE_STATIC);
        ok=sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_TEXT&&sqlite3_column_bytes16(s,0)<cap*(int)sizeof(wchar_t);
        }
    if(ok){const wchar_t *value=sqlite3_column_text16(s,0);
        ok=value&&wcslen(value)==(size_t)sqlite3_column_bytes16(s,0)/sizeof(wchar_t);
        if(ok)lstrcpyW(out,value);
        }
    sqlite3_finalize(s);
    return ok;
}
BOOL sync_settings_import(const wchar_t *file,SyncSettings *out){
    HANDLE f=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(f==INVALID_HANDLE_VALUE)return FALSE;
    LARGE_INTEGER size;
    BOOL ok=GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<1024*1024;
    char *json=ok?calloc((size_t)size.QuadPart+1,1):NULL;
    DWORD n=0;
    ok=ok&&json&&ReadFile(f,json,(DWORD)size.QuadPart,&n,NULL)&&n==(DWORD)size.QuadPart;
    CloseHandle(f);
    if(ok&&n>=3&&!memcmp(json,"\xef\xbb\xbf",3)){memmove(json,json+3,n-2);
        n-=3;
        }
    sqlite3 *db=NULL;
    SyncSettings *next=calloc(1,sizeof(*next));
    ok=ok&&json&&strlen(json)==n&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,json,(int)n,NULL,0)>0&&next&&sqlite3_open(":memory:",&db)==SQLITE_OK;
    if(ok){
        sync_settings_default(next);
        sqlite3_stmt *statement=NULL;
        ok=sqlite3_prepare_v2(db,"SELECT json_valid(?1) AND json_type(?1,'$.folders')='array' AND json_type(?1,'$.identity.registered') IN ('true','false') AND (json_type(?1,'$.version') IS NULL OR json_extract(?1,'$.version')=1),json_array_length(?1,'$.folders'),json_extract(?1,'$.identity.registered')",-1,&statement,NULL)==SQLITE_OK;
        if(ok){sqlite3_bind_text(statement,1,json,-1,SQLITE_STATIC);
            ok=sqlite3_step(statement)==SQLITE_ROW&&sqlite3_column_int(statement,0)==1;
            next->count=sqlite3_column_int(statement,1);
            next->registered=sqlite3_column_int(statement,2)!=0;
            }sqlite3_finalize(statement);
        wchar_t role[8]={0};
        ok=ok&&next->count>=0&&next->count<=SYNC_PAIR_LIMIT&&json_wide(db,json,"$.identity.node_id",next->node_id,SYNC_ID_CAP)&&json_wide(db,json,"$.identity.name",next->name,SYNC_NAME_CAP)&&json_wide(db,json,"$.identity.role",role,8)&&(!wcscmp(role,L"A")||!wcscmp(role,L"B"))&&json_wide(db,json,"$.connection.remote_project",next->project,SYNC_PATH_CAP)&&json_wide(db,json,"$.connection.remote_root",next->broker,SYNC_PATH_CAP);
        next->role=role[0]==L'B';
        wchar_t hint[32];
        if(ok&&json_wide(db,json,"$.nova_server_id",hint,32)){wchar_t *end=NULL;
            errno=0;
            long long id=wcstoll(hint,&end,10);
            ok=hint[0]&&end&&!*end&&!errno&&id>0;
            if(ok)next->server_id=id;
            }
        for(int i=0;i<next->count&&ok;i++){
            char path[96];
            SyncPair *pair=&next->pairs[i];
            const char *keys[]={"id","dataset","name","group_name","path"};
            wchar_t *dest[]={pair->id,pair->dataset,pair->name,pair->group,pair->directory};
            const int caps[]={SYNC_ID_CAP,SYNC_ID_CAP,SYNC_NAME_CAP,SYNC_NAME_CAP,SYNC_PATH_CAP};
            for(int j=0;j<5&&ok;j++){snprintf(path,sizeof(path),"$.folders[%d].%s",i,keys[j]);
                ok=json_wide(db,json,path,dest[j],caps[j]);
                }
            pair->paused=true;
            snprintf(path,sizeof(path),"$.folders[%d].sync_deletes",i);
            statement=NULL;
            if(ok)ok=sqlite3_prepare_v2(db,"SELECT json_type(?1,?2),json_extract(?1,?2)",-1,&statement,NULL)==SQLITE_OK;
            if(ok){sqlite3_bind_text(statement,1,json,-1,SQLITE_STATIC);
                sqlite3_bind_text(statement,2,path,-1,SQLITE_STATIC);
                ok=sqlite3_step(statement)==SQLITE_ROW;
                const char *type=ok?(const char*)sqlite3_column_text(statement,0):NULL;
                ok=type&&(!strcmp(type,"true")||!strcmp(type,"false"));
                if(ok)pair->sync_deletes=sqlite3_column_int(statement,1)!=0;
                }sqlite3_finalize(statement);
            snprintf(path,sizeof(path),"$.folders[%d].excludes",i);
            statement=NULL;
            if(ok)ok=sqlite3_prepare_v2(db,"SELECT type,value FROM json_each(?1,?2)",-1,&statement,NULL)==SQLITE_OK;
            if(ok){sqlite3_bind_text(statement,1,json,-1,SQLITE_STATIC);
                sqlite3_bind_text(statement,2,path,-1,SQLITE_STATIC);
                }
            while(ok&&sqlite3_step(statement)==SQLITE_ROW){const char *type=(const char*)sqlite3_column_text(statement,0);
                const wchar_t *pattern=sqlite3_column_text16(statement,1);
                int length=sqlite3_column_bytes16(statement,1);
                size_t used=wcslen(pair->excludes);
                ok=type&&!strcmp(type,"text")&&pattern&&length>=0&&wcslen(pattern)==(size_t)length/sizeof(wchar_t)&&!wcschr(pattern,L';')&&used+wcslen(pattern)+2<SYNC_FILTER_CAP;
                if(ok){if(used)wcscat(pair->excludes,L";");
                    wcscat(pair->excludes,pattern);
                    }}
            sqlite3_finalize(statement);
        }
        ok=ok&&sync_settings_valid(next);
        if(ok)*out=*next;
    }
    if(db)sqlite3_close(db);
    free(next);
    free(json);
    return ok;
}
