#define _WIN32_WINNT 0x0601
/* Older MinGW defaults to MSVCRT formatting without C99 integer specifiers. */
#define __USE_MINGW_ANSI_STDIO 1
#include "sync_internal.h"
#include "../i18n.h"
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Admin {
    SyncRun wire;
    const wchar_t *project,*root;
    BOOL target,provision;
} Admin;
static const char *source_names[]={"sync_server.py","chunksync/__init__.py","chunksync/common.py","chunksync/wire.py","chunksync/broker.py","chunksync/control.py"};

static BOOL admin_error(Admin *a,SyncFailure failure,const wchar_t *zh,const wchar_t *en){
    a->wire.result->target_error=a->target;
    lstrcpynW(a->wire.result->detail,nova_text(zh,en),1024);
    return sy_fail(&a->wire,failure);
}
static const unsigned char *resource(int id,DWORD *length){
    HRSRC entry=FindResourceW(NULL,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!entry)return NULL;
    *length=SizeofResource(NULL,entry);
    return LockResource(LoadResource(NULL,entry));
}
static char *patch(sqlite3 *db,char *original,const char *key,const char *value,BOOL object){
    char *next=original&&value?sync_json_patch(db,original,key,value,object):NULL;
    free(original);
    return next;
}
static char *merge(sqlite3 *db,const char *first,const char *second){
    sqlite3_stmt *s=NULL;
    char *value=NULL;
    if(sqlite3_prepare_v2(db,"SELECT json_patch(?1,?2)",-1,&s,NULL)==SQLITE_OK){
        sqlite3_bind_text(s,1,first,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,second?second:"{}",-1,SQLITE_STATIC);
        if(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_TEXT)value=strdup((const char*)sqlite3_column_text(s,0));
    }sqlite3_finalize(s);
    return value;
}
static BOOL rpc(Admin *a,const char *action,const char *fields,const void *bytes,size_t size,SyncReply *reply){
    char *project=sync_utf8(a->project),*root=sync_utf8(a->root);
    char *request=project&&root?sy_fields(action,"project",project,"root",root):NULL;
    free(project);
    free(root);
    char *with=request?merge(a->wire.db,request,fields):NULL;
    free(request);
    with=patch(a->wire.db,with,"$.action",action,FALSE);
    BOOL ok=with&&sy_rpc(&a->wire,with,bytes,size,reply);
    free(with);
    if(!ok)a->wire.result->target_error=a->target;
    return ok;
}
static BOOL opened(Admin *a,SshLogin *login){
    sy_progress(&a->wire,SYNC_CONNECT,a->project,0,0);
    a->wire.ssh=ssh_client_connect(login,a->wire.request->cancel,&a->wire.result->ssh);
    SecureZeroMemory(login->secret,sizeof(login->secret));
    if(!a->wire.ssh){a->wire.result->target_error=a->target;
        return sy_fail(&a->wire,SYNC_NETWORK);
        }
    DWORD length=0;
    const unsigned char *source=resource(305,&length);
    if(!source||length>1024*1024)return admin_error(a,SYNC_STORAGE,L"程序缺少内置服务端资源，请使用完整新版 EXE。",L"Embedded server source is missing; use a complete current EXE.");
    size_t capacity=4*((length+2)/3)+1;
    char *encoded=malloc(capacity);
    if(!encoded)return sy_fail(&a->wire,SYNC_STORAGE);
    int n=EVP_EncodeBlock((unsigned char*)encoded,source,(int)length);
    BOOL ok=n>0&&ssh_client_bootstrap_open(a->wire.ssh,encoded,(size_t)n,a->provision,&a->wire.result->ssh);
    free(encoded);
    if(!ok){a->wire.result->target_error=a->target;
        return sy_fail(&a->wire,SYNC_NETWORK);
        }return TRUE;
}
static BOOL text_field(sqlite3 *db,const char *json,const char *key,wchar_t *out,int cap){
    char *value=sync_json_string(db,json,key);
    BOOL ok=value&&sync_wide(value,out,cap);
    free(value);
    return ok;
}
static BOOL probe(Admin *a,char **out){
    SyncReply reply={0};
    BOOL ok=rpc(a,"probe",NULL,NULL,0,&reply);
    if(ok){*out=reply.json;
        reply.json=NULL;
        }sy_reply_free(&reply);
    return ok;
}
static BOOL deploy(Admin *a){
    SyncBuffer list={0};
    BOOL ok=sync_buffer_add(&list,"[");
    for(int i=0;i<6&&ok;i++){
        DWORD size=0;
        const unsigned char *bytes=resource(300+i,&size);
        char hash[65],number[160];
        ok=bytes&&sync_sha(bytes,size,hash);
        snprintf(number,sizeof(number),"%s{\"name\":",i?",":"");
        ok=ok&&sync_buffer_add(&list,number)&&sync_buffer_string(&list,source_names[i]);
        snprintf(number,sizeof(number),",\"size\":%lu,\"sha256\":\"%s\"}",(unsigned long)size,ok?hash:"");
        ok=ok&&sync_buffer_add(&list,number);
    }
    ok=ok&&sync_buffer_add(&list,"]");
    char token[65]={0};
    ok=ok&&sync_sha(list.data,list.size,token);
    char *fields=ok?sy_fields("unused","token",token,NULL,NULL):NULL;
    fields=patch(a->wire.db,fields,"$.files",list.data,TRUE);
    free(list.data);
    SyncReply reply={0};
    if(fields)ok=rpc(a,"install_begin",fields,NULL,0,&reply);
    else ok=FALSE;
    free(fields);
    sy_reply_free(&reply);
    for(int i=0;i<6&&ok&&!sy_cancel(&a->wire);i++){
        DWORD length=0;
        const unsigned char *bytes=resource(300+i,&length);
        fields=sy_fields("unused","token",token,"name",source_names[i]);
        ok=fields&&rpc(a,"install_put",fields,NULL,0,&reply);
        uint64_t offset=ok?(uint64_t)sync_json_int(a->wire.db,reply.json,"$.offset",-1):0;
        sy_reply_free(&reply);
        if(offset>length)ok=sy_fail(&a->wire,SYNC_PROTOCOL);
        while(ok&&offset<length&&!sy_cancel(&a->wire)){
            sy_progress(&a->wire,SYNC_DEPLOY,a->project,(uint64_t)i*length+offset,(uint64_t)6*length);
            char number[32];
            snprintf(number,sizeof(number),"%llu",(unsigned long long)offset);
            char *packet=sync_json_patch(a->wire.db,fields,"$.offset",number,TRUE);
            size_t count=(size_t)min((uint64_t)SYNC_PACKET_MAX,(uint64_t)length-offset);
            ok=packet&&rpc(a,"install_put",packet,bytes+offset,count,&reply);
            free(packet);
            uint64_t next=ok?(uint64_t)sync_json_int(a->wire.db,reply.json,"$.offset",-1):0;
            if(ok&&(next!=offset+count||next>length))ok=sy_fail(&a->wire,SYNC_PROTOCOL);
            offset=next;
            sy_reply_free(&reply);
        }free(fields);
    }
    if(sy_cancel(&a->wire))ok=FALSE;
    fields=sy_fields("unused","token",token,NULL,NULL);
    char number[32];
    snprintf(number,sizeof(number),"%d",a->wire.request->quota_gib?a->wire.request->quota_gib:15);
    fields=patch(a->wire.db,fields,"$.quota_gib",number,TRUE);
    snprintf(number,sizeof(number),"%d",a->wire.request->reserve_gib);
    fields=patch(a->wire.db,fields,"$.reserve_gib",number,TRUE);
    if(ok)ok=fields&&rpc(a,"install_commit",fields,NULL,0,&reply);
    free(fields);
    sy_reply_free(&reply);
    return ok;
}
static BOOL identity(Admin *a,const char *status,BOOL attaching){
    SyncSettings *s=&a->wire.request->settings;
    char *phase=sync_json_string(a->wire.db,status,"$.control.phase");
    BOOL ok=phase&&!strcmp(phase,"ACTIVE");
    free(phase);
    if(!ok)return attaching?admin_error(a,SYNC_FROZEN,L"目标尚未启用；请继续未完成的迁移。",L"Destination is not active; resume its pending migration."):FALSE;
    if(!s->registered)return TRUE;
    char *node=sync_utf8(s->node_id);
    sqlite3_stmt *query=NULL;
    ok=node&&sqlite3_prepare_v2(a->wire.db,"SELECT count(*) FROM json_each(?1,'$.devices') WHERE json_extract(value,'$.role')=?2 AND json_extract(value,'$.node_id')=?3",-1,&query,NULL)==SQLITE_OK;
    char role[2]={(char)('A'+s->role),0};
    if(ok){sqlite3_bind_text(query,1,status,-1,SQLITE_STATIC);
        sqlite3_bind_text(query,2,role,-1,SQLITE_STATIC);
        sqlite3_bind_text(query,3,node,-1,SQLITE_STATIC);
        ok=sqlite3_step(query)==SQLITE_ROW&&sqlite3_column_int(query,0)==1;
        }
    sqlite3_finalize(query);
    free(node);
    if(ok&&s->server_instance[0]){
        char *expected=sync_utf8(s->server_instance),*actual=sync_json_string(a->wire.db,status,"$.control.instance");
        ok=expected&&actual&&!strcmp(expected,actual);
        if(!ok&&expected){query=NULL;
            if(sqlite3_prepare_v2(a->wire.db,"SELECT count(*) FROM json_each(?1,'$.control.ancestors') WHERE value=?2",-1,&query,NULL)==SQLITE_OK){sqlite3_bind_text(query,1,status,-1,SQLITE_STATIC);
                sqlite3_bind_text(query,2,expected,-1,SQLITE_STATIC);
                ok=sqlite3_step(query)==SQLITE_ROW&&sqlite3_column_int(query,0)==1;
                }
            sqlite3_finalize(query);
        }free(expected);
        free(actual);
    }
    if(!ok&&attaching)return admin_error(a,SYNC_CONFLICT,L"目标不包含本机原有同步身份或版本链。请使用“迁移并切换”，现有配置未更改。",L"Destination lacks this device's identity or lineage. Use Migrate and switch; current settings are preserved.");
    return ok;
}
static BOOL journal_path(SyncRequest *r,wchar_t path[SYNC_PATH_CAP]){return sy_join(path,r->state_directory,L"server-migration.json");
    }
static char *read_journal(SyncRequest *r){
    wchar_t path[SYNC_PATH_CAP];
    if(!journal_path(r,path))return NULL;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(f==INVALID_HANDLE_VALUE)return NULL;
    LARGE_INTEGER size;
    DWORD got=0;
    char *value=NULL;
    if(GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<32768){value=calloc((size_t)size.QuadPart+1,1);
        if(!value||!ReadFile(f,value,(DWORD)size.QuadPart,&got,NULL)||got!=(DWORD)size.QuadPart){free(value);
            value=NULL;
            }}
    CloseHandle(f);
    return value;
}
static BOOL save_journal(Admin *a,const char *json){
    wchar_t path[SYNC_PATH_CAP],temporary[SYNC_PATH_CAP];
    if(!sy_mkdirs(a->wire.request->state_directory)||!journal_path(a->wire.request,path)||!sy_join(temporary,a->wire.request->state_directory,L".server-migration.tmp"))return sy_fail(&a->wire,SYNC_PATH);
    DWORD attributes=GetFileAttributesW(temporary);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return sy_fail(&a->wire,SYNC_PATH);
    HANDLE f=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    DWORD wrote=0;
    BOOL ok=f!=INVALID_HANDLE_VALUE&&WriteFile(f,json,(DWORD)strlen(json),&wrote,NULL)&&wrote==strlen(json)&&FlushFileBuffers(f);
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    return (ok&&MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))||sy_fail(&a->wire,SYNC_STORAGE);
}
BOOL sync_server_commit_local(const wchar_t *directory){
    wchar_t source[SYNC_PATH_CAP],target[SYNC_PATH_CAP];
    if(!sy_join(source,directory,L"server-migration.json")||!sy_join(target,directory,L"server-migration.completed.json"))return FALSE;
    if(GetFileAttributesW(source)==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    return MoveFileExW(source,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
BOOL sync_server_pending(SyncRequest *r){
    char *json=read_journal(r);
    if(!json)return FALSE;
    sqlite3 *db=NULL;
    BOOL ok=sqlite3_open(":memory:",&db)==SQLITE_OK&&sync_json_valid(db,json)&&sync_json_int(db,json,"$.version",0)==1;
    if(ok){r->target_login.server.id=sync_json_int(db,json,"$.target_id",0);
        ok=r->target_login.server.id>0&&text_field(db,json,"$.project",r->target_project,SYNC_PATH_CAP)&&text_field(db,json,"$.root",r->target_broker,SYNC_PATH_CAP);
        wchar_t instance[SYNC_ID_CAP];
        if(ok&&r->settings.server_id==r->target_login.server.id&&!wcscmp(r->settings.project,r->target_project)&&!wcscmp(r->settings.broker,r->target_broker)&&text_field(db,json,"$.instance",instance,SYNC_ID_CAP)&&!wcscmp(instance,r->settings.server_instance)){
            sync_server_commit_local(r->state_directory);
            ok=FALSE;
        }
    }if(db)sqlite3_close(db);
    free(json);
    return ok;
}
static char *migration_journal(Admin *a,Admin *b,const char *destination){
    SyncRequest *r=a->wire.request;
    char *value=read_journal(r);
    if(value){
        wchar_t project[SYNC_PATH_CAP],root[SYNC_PATH_CAP];
        BOOL ok=sync_json_valid(a->wire.db,value)&&sync_json_int(a->wire.db,value,"$.version",0)==1&&sync_json_int(a->wire.db,value,"$.source_id",-1)==r->login.server.id&&sync_json_int(a->wire.db,value,"$.target_id",-1)==r->target_login.server.id&&
            text_field(a->wire.db,value,"$.project",project,SYNC_PATH_CAP)&&text_field(a->wire.db,value,"$.root",root,SYNC_PATH_CAP)&&!wcscmp(project,b->project)&&!wcscmp(root,b->root);
        char *old=sync_json_string(a->wire.db,value,"$.destination");
        char *left=old?sync_json_patch(a->wire.db,old,"$.name","",FALSE):NULL;
        char *right=sync_json_patch(a->wire.db,destination,"$.name","",FALSE);
        ok=ok&&left&&right&&!strcmp(left,right);
        free(left);
        free(right);
        free(old);
        if(ok)return value;
        free(value);
        admin_error(a,SYNC_CONFLICT,L"有另一项迁移断点。请继续该迁移或恢复旧服务器后再选择目标。",L"Another migration checkpoint exists. Resume it or restore the old server before choosing a new destination.");
        return NULL;
    }
    wchar_t existing[SYNC_PATH_CAP];
    if(!journal_path(r,existing))return NULL;
    DWORD attributes=GetFileAttributesW(existing),error=GetLastError();
    if(attributes!=INVALID_FILE_ATTRIBUTES||(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)){admin_error(a,SYNC_STORAGE,L"迁移断点不可读取，原记录保留。",L"Migration checkpoint cannot be read; original data preserved.");
        return NULL;
        }
    unsigned char random[16];
    sqlite3_randomness(16,random);
    char token[33],number[48];
    for(int i=0;i<16;i++)snprintf(token+i*2,3,"%02x",random[i]);
    value=sy_fields("migration","token",token,NULL,NULL);
    value=patch(a->wire.db,value,"$.version","1",TRUE);
    snprintf(number,sizeof(number),"%lld",(long long)r->login.server.id);
    value=patch(a->wire.db,value,"$.source_id",number,TRUE);
    snprintf(number,sizeof(number),"%lld",(long long)r->target_login.server.id);
    value=patch(a->wire.db,value,"$.target_id",number,TRUE);
    char *p=sync_utf8(b->project),*q=sync_utf8(b->root),*instance=sync_json_string(a->wire.db,destination,"$.instance");
    value=patch(a->wire.db,value,"$.project",p,FALSE);
    value=patch(a->wire.db,value,"$.root",q,FALSE);
    value=patch(a->wire.db,value,"$.instance",instance,FALSE);
    value=patch(a->wire.db,value,"$.destination",destination,TRUE);
    free(p);
    free(q);
    free(instance);
    if(!value||!save_journal(a,value)){free(value);
        return NULL;
        }return value;
}
static BOOL copy_file(Admin *a,Admin *b,const char *token,const char *name,uint64_t total,uint64_t *done,uint64_t all){
    char *fields=sy_fields("unused","token",token,"name",name);
    SyncReply info={0},reply={0};
    BOOL ok=fields&&rpc(a,"export_info",fields,NULL,0,&info);
    char *hash=ok?sync_json_string(a->wire.db,info.json,"$.sha256"):NULL;
    ok=ok&&hash&&strlen(hash)==64&&sync_json_int(a->wire.db,info.json,"$.size",-1)==(int64_t)total;
    char number[32];
    snprintf(number,sizeof(number),"%llu",(unsigned long long)total);
    char *put=fields?sync_json_patch(a->wire.db,fields,"$.size",number,TRUE):NULL;
    put=patch(a->wire.db,put,"$.sha256",hash,FALSE);
    if(ok)ok=put&&rpc(b,"import_put",put,NULL,0,&reply);
    uint64_t offset=ok?(uint64_t)sync_json_int(a->wire.db,reply.json,"$.offset",-1):0;
    if(ok&&offset>total)ok=sy_fail(&b->wire,SYNC_PROTOCOL);
    b->wire.result->stats.resumed+=offset;
    sy_reply_free(&reply);
    while(ok&&offset<total&&!sy_cancel(&a->wire)){
        sy_progress(&a->wire,SYNC_COPY_SERVER,a->project,*done+offset,all);
        snprintf(number,sizeof(number),"%llu",(unsigned long long)offset);
        char *get=sync_json_patch(a->wire.db,fields,"$.offset",number,TRUE);
        ok=get&&rpc(a,"export_get",get,NULL,0,&reply);
        free(get);
        if(ok)ok=sync_json_int(a->wire.db,reply.json,"$.offset",-1)==(int64_t)offset&&reply.size>0&&offset+reply.size<=total;
        size_t count=reply.size;
        char *packet=sync_json_patch(a->wire.db,put,"$.offset",number,TRUE);
        SyncReply received={0};
        if(ok)ok=packet&&rpc(b,"import_put",packet,reply.data,count,&received);
        if(ok)ok=sync_json_int(a->wire.db,received.json,"$.offset",-1)==(int64_t)(offset+count);
        free(packet);
        sy_reply_free(&received);
        sy_reply_free(&reply);
        offset+=count;
    }
    *done+=total;
    free(hash);
    free(fields);
    free(put);
    sy_reply_free(&info);
    return ok&&!sy_cancel(&a->wire);
}
static char *destination(Admin *b,const char *status){
    SyncRequest *r=b->wire.request;
    const SshLogin *login=&r->target_login;
    char *instance=sync_json_string(b->wire.db,status,"$.control.instance"),*host=sync_utf8(login->server.host),*user=sync_utf8(login->server.user),*name=sync_utf8(login->server.name);
    char *value=host&&user?sy_fields("destination","host",host,"user",user):NULL;
    char number[32];
    snprintf(number,sizeof(number),"%d",login->server.port);
    value=patch(b->wire.db,value,"$.port",number,TRUE);
    value=patch(b->wire.db,value,"$.instance",instance,FALSE);
    value=patch(b->wire.db,value,"$.name",name,FALSE);
    host= (free(host),sync_utf8(b->project));
    user=(free(user),sync_utf8(b->root));
    value=patch(b->wire.db,value,"$.project",host,FALSE);
    value=patch(b->wire.db,value,"$.root",user,FALSE);
    free(instance);
    free(host);
    free(user);
    free(name);
    return value;
}
static BOOL migrate(Admin *a,Admin *b){
    SyncRequest *r=a->wire.request;
    SyncResult *result=a->wire.result;
    if(!wcscmp(r->login.server.host,r->target_login.server.host)&&r->login.server.port==r->target_login.server.port&&!wcscmp(r->login.server.user,r->target_login.server.user)&&!wcscmp(a->root,b->root))return admin_error(a,SYNC_CONFLICT,L"源和目标是同一个队列。",L"Source and destination are the same queue.");
    char *target=NULL,*source=NULL;
    BOOL ok=probe(b,&target);
    if(ok){char *device=sync_json_string(b->wire.db,target,"$.devices[0]"),*dataset=sync_json_string(b->wire.db,target,"$.datasets[0]"),*checkpoint=read_journal(r);
        if((device||dataset)&&!checkpoint)ok=admin_error(b,SYNC_CONFLICT,L"目标已有同步记录，不能覆盖。请选择空的目标队列。",L"Destination contains synchronization records. Choose an empty destination queue.");
        free(device);
        free(dataset);
        free(checkpoint);
    }
    if(ok&&!sync_json_int(b->wire.db,target,"$.installed",0)){ok=deploy(b);
        free(target);
        target=NULL;
        if(ok)ok=probe(b,&target);
        }
    /* Upgrade the source to the embedded gate-aware release before freezing it. */
    if(ok)ok=deploy(a);
    if(ok)ok=probe(a,&source);
    if(ok){char *origin=sync_json_string(a->wire.db,source,"$.control.instance"),*target_instance=sync_json_string(a->wire.db,target,"$.control.instance");
        if(origin&&target_instance&&!strcmp(origin,target_instance))ok=admin_error(a,SYNC_CONFLICT,L"源和目标具有同一服务器身份，拒绝迁移到自身或克隆。",L"Source and destination have the same server identity; refusing self/clone migration.");
        free(origin);
        free(target_instance);
    }
    char *dest=ok?destination(b,target):NULL;
    char *journal=dest?migration_journal(a,b,dest):NULL;
    if(journal){free(dest);
        dest=sync_json_string(a->wire.db,journal,"$.destination");
        }
    char *token=journal?sync_json_string(a->wire.db,journal,"$.token"):NULL;
    char *fields=token?sy_fields("unused","token",token,NULL,NULL):NULL;
    fields=patch(a->wire.db,fields,"$.destination",dest,TRUE);
    SyncReply snapshot={0},reply={0};
    if(ok)ok=token&&fields;
    if(ok){result->pending_migration=TRUE;
        sy_progress(&a->wire,SYNC_FREEZE,a->project,0,0);
        ok=rpc(a,"export_begin",fields,NULL,0,&snapshot);
        }
    char *src=ok?sync_json_string(a->wire.db,snapshot.json,"$.control"):NULL;
    char *entries=ok?sync_json_string(a->wire.db,snapshot.json,"$.files"):NULL;
    char *phase=ok?sync_json_string(a->wire.db,snapshot.json,"$.control.phase"):NULL;
    char *import_fields=token?sy_fields("unused","token",token,NULL,NULL):NULL;
    import_fields=patch(a->wire.db,import_fields,"$.source",src,TRUE);
    import_fields=patch(a->wire.db,import_fields,"$.files",entries,TRUE);
    if(ok)ok=import_fields&&rpc(b,"import_begin",import_fields,NULL,0,&reply);
    sy_reply_free(&reply);
    if(ok&&phase&&!strcmp(phase,"EXPORTING")){
        sqlite3_stmt *s=NULL;
        ok=sqlite3_prepare_v2(a->wire.db,"SELECT json_extract(value,'$.name'),json_extract(value,'$.size') FROM json_each(?1)",-1,&s,NULL)==SQLITE_OK;
        if(ok)sqlite3_bind_text(s,1,entries,-1,SQLITE_STATIC);
        uint64_t done=0,total=(uint64_t)sync_json_int(a->wire.db,snapshot.json,"$.bytes",0);
        int rc=SQLITE_DONE;
        while(ok&&(rc=sqlite3_step(s))==SQLITE_ROW){const char *name=(const char*)sqlite3_column_text(s,0);
            int64_t size=sqlite3_column_int64(s,1);
            ok=name&&size>=0&&copy_file(a,b,token,name,(uint64_t)size,&done,total);
            }
        if(ok&&rc!=SQLITE_DONE)ok=sy_fail(&a->wire,SYNC_STORAGE);
        sqlite3_finalize(s);
        sy_progress(&a->wire,SYNC_VERIFY_SERVER,b->project,done,total);
        if(ok)ok=rpc(b,"import_prepare",import_fields,NULL,0,&reply);
        sy_reply_free(&reply);
        char *instance=sync_json_string(a->wire.db,dest,"$.instance");
        char *commit=token?sy_fields("unused","token",token,"instance",instance?instance:""):NULL;
        free(instance);
        if(ok){sy_progress(&a->wire,SYNC_CUTOVER,b->project,0,0);
            ok=commit&&rpc(a,"export_commit",commit,NULL,0,&reply);
            }free(commit);
        sy_reply_free(&reply);
    }
    char *previous=src?sync_json_string(a->wire.db,src,"$.instance"):NULL;
    char *activate=token?sy_fields("unused","token",token,"previous",previous?previous:""):NULL;
    if(ok)ok=activate&&rpc(b,"import_activate",activate,NULL,0,&reply);
    if(ok){result->cloud=reply.json;
        reply.json=NULL;
        ok=identity(b,result->cloud,TRUE);
        result->switched=ok;
        result->installed=TRUE;
        if(ok)ok=text_field(b->wire.db,result->cloud,"$.control.instance",result->server_instance,SYNC_ID_CAP);
    }
    free(activate);
    free(previous);
    free(source);
    free(target);
    free(dest);
    free(journal);
    free(token);
    free(fields);
    free(src);
    free(entries);
    free(phase);
    free(import_fields);
    sy_reply_free(&snapshot);
    sy_reply_free(&reply);
    return ok;
}
BOOL sync_server_run(SyncRequest *request,int action,SyncResult *result){
    memset(result,0,sizeof(*result));
    Admin a={0},b={0};
    a.wire.request=b.wire.request=request;
    a.wire.result=b.wire.result=result;
    a.project=request->settings.project;
    a.root=request->settings.broker;
    b.project=request->target_project;
    b.root=request->target_broker;
    b.target=TRUE;
    b.provision=action==SYNC_JOB_SERVER_DEPLOY||action==SYNC_JOB_SERVER_MIGRATE;
    BOOL ok=sqlite3_open(":memory:",&a.wire.db)==SQLITE_OK;
    b.wire.db=a.wire.db;
    if(ok&&action==SYNC_JOB_SERVER_RESTORE){
        char *journal=read_journal(request);
        BOOL valid=journal&&sync_json_valid(a.wire.db,journal)&&sync_json_int(a.wire.db,journal,"$.version",0)==1&&sync_json_int(a.wire.db,journal,"$.source_id",-1)==request->login.server.id;
        char *token=valid?sync_json_string(a.wire.db,journal,"$.token"):NULL;
        SyncReply reply={0};
        char *fields=token?sy_fields("unused","token",token,NULL,NULL):NULL;
        ok=token&&opened(&a,&request->login)&&rpc(&a,"export_abort",fields,NULL,0,&reply);
        if(ok&&!sync_server_commit_local(request->state_directory))ok=sy_fail(&a.wire,SYNC_STORAGE);
        free(journal);
        free(token);
        free(fields);
        sy_reply_free(&reply);
    }else if(ok&&action==SYNC_JOB_SERVER_MIGRATE){
        ok=opened(&b,&request->target_login)&&opened(&a,&request->login)&&migrate(&a,&b);
    }else if(ok){
        /* Single-server actions use target settings and target credentials. */
        ok=opened(&b,&request->target_login);
        if(ok&&action==SYNC_JOB_SERVER_DEPLOY)ok=deploy(&b);
        if(ok)ok=probe(&b,&result->cloud);
        if(ok){result->installed=sync_json_int(b.wire.db,result->cloud,"$.installed",0)!=0;
            text_field(b.wire.db,result->cloud,"$.control.phase",result->server_phase,32);
            text_field(b.wire.db,result->cloud,"$.control.instance",result->server_instance,SYNC_ID_CAP);
            if(!result->installed&&action==SYNC_JOB_SERVER_ATTACH)ok=admin_error(&b,SYNC_REMOTE,L"服务器尚未部署，请点“一键部署”。",L"Server is not deployed; choose One-click deploy.");
            if(ok&&action==SYNC_JOB_SERVER_ATTACH){ok=identity(&b,result->cloud,TRUE);
                result->switched=ok;
                }
            if(ok&&action==SYNC_JOB_SERVER_DEPLOY&&result->installed&&identity(&b,result->cloud,FALSE))result->switched=TRUE;
        }
    }
    if(!ok&&result->failure==SYNC_OK)result->failure=sy_cancel(&a.wire)?SYNC_CANCELLED:SYNC_PROTOCOL;
    if(a.wire.ssh)ssh_client_close(a.wire.ssh);
    if(b.wire.ssh)ssh_client_close(b.wire.ssh);
    if(a.wire.db)sqlite3_close(a.wire.db);
    SecureZeroMemory(request->login.secret,sizeof(request->login.secret));
    SecureZeroMemory(request->target_login.secret,sizeof(request->target_login.secret));
    return ok;
}
