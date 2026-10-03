#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include "config_json.h"
#include "../i18n.h"
#include "../../third_party/sqlite/sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define JSON_LIMIT (16*1024*1024)
#define LOCAL_STATE "((scope='app' AND section='Nova' AND key IN ('Active','AlwaysOnTop','DesktopMode','SidebarCollapsed')) OR (scope='files' AND ((section IN ('Pane0','Pane1','Pane2','Pane3') AND key='Selected') OR (section='Manager' AND (key IN ('Layout','NavigationTree') OR key GLOB 'Split[0-9]*_[0-9]*')))))"
static BOOL invalid(wchar_t *error){
    lstrcpyW(error,nova_text(L"JSON 配置损坏、超出容量或来自更新版本，原文件未修改。",L"JSON configuration is damaged, exceeds capacity, or is from a newer version. The original file was not changed."));return FALSE;
}
static BOOL file_error(wchar_t *error){
    swprintf(error,320,nova_text(L"无法保存 JSON 配置（Windows %lu）。请检查权限或文件占用；原文件保留。",L"Unable to save JSON configuration (Windows %lu). Check permissions or file locks; the original file was preserved."),GetLastError());return FALSE;
}
static BOOL sql_text(sqlite3 *db,const char *sql,const char *json){
    sqlite3_stmt *s=NULL;BOOL ok=sqlite3_prepare_v2(db,sql,-1,&s,NULL)==SQLITE_OK;
    if(ok&&json)ok=sqlite3_bind_text(s,1,json,-1,SQLITE_TRANSIENT)==SQLITE_OK;
    if(ok){int rc=sqlite3_step(s);ok=rc==SQLITE_DONE||(rc==SQLITE_ROW&&sqlite3_column_int(s,0)==1);}
    if(sqlite3_finalize(s)!=SQLITE_OK)ok=FALSE;
    return ok;
}
static BOOL valid_json(sqlite3 *db,const char *json){
    if(!sql_text(db,"SELECT json_valid(?1)",json))return FALSE;
    const char *metadata="SELECT (json_type(?1)='object' AND (SELECT count(*) FROM json_each(?1))=6"
        " AND json_type(?1,'$.version')='integer' AND json_extract(?1,'$.version')=1"
        " AND json_extract(?1,'$.application')='NOVA Desktop' AND json_type(?1,'$.application')='text'"
        " AND json_type(?1,'$.initialized') IN ('true','false')"
        " AND json_type(?1,'$.settings')='array' AND json_array_length(?1,'$.settings')<=4096"
        " AND json_type(?1,'$.workspaces')='array' AND json_array_length(?1,'$.workspaces')<=8"
        " AND json_type(?1,'$.items')='array' AND json_array_length(?1,'$.items')<=160"
        " AND NOT EXISTS (SELECT 1 FROM json_tree(?1) WHERE type='text' AND instr(atom,char(0))>0)"
        " AND NOT EXISTS (SELECT 1 FROM json_tree(?1) WHERE parent IS NOT NULL AND typeof(key)='text' GROUP BY parent,key HAVING count(*)>1)"
        " AND (json_extract(?1,'$.initialized') OR (json_array_length(?1,'$.settings')=0 AND json_array_length(?1,'$.workspaces')=0 AND json_array_length(?1,'$.items')=0))) IS TRUE";
    if(!sql_text(db,metadata,json))return FALSE;
    const char *settings="SELECT NOT EXISTS (SELECT 1 FROM json_each(?1,'$.settings') AS entry WHERE (type='object'"
        " AND (SELECT count(*) FROM json_each(entry.value))=4"
        " AND json_type(value,'$.scope')='text' AND length(json_extract(value,'$.scope')) BETWEEN 1 AND 64"
        " AND json_type(value,'$.section')='text' AND length(json_extract(value,'$.section')) BETWEEN 1 AND 64"
        " AND json_type(value,'$.key')='text' AND length(json_extract(value,'$.key')) BETWEEN 1 AND 64"
        " AND json_type(value,'$.value')='text' AND length(CAST(json_extract(value,'$.value') AS BLOB))<=16384) IS NOT TRUE)";
    const char *workspaces="SELECT NOT EXISTS (SELECT 1 FROM json_each(?1,'$.workspaces') AS entry WHERE (type='object'"
        " AND (SELECT count(*) FROM json_each(entry.value))=3"
        " AND json_type(value,'$.id')='text' AND length(json_extract(value,'$.id')) BETWEEN 1 AND 19"
        " AND json_extract(value,'$.id') NOT GLOB '*[^0-9]*' AND substr(json_extract(value,'$.id'),1,1) BETWEEN '1' AND '9'"
        " AND (length(json_extract(value,'$.id'))<19 OR json_extract(value,'$.id')<='9223372036854775807')"
        " AND json_type(value,'$.position')='integer' AND json_extract(value,'$.position') BETWEEN 0 AND 7"
        " AND json_type(value,'$.name')='text' AND length(json_extract(value,'$.name')) BETWEEN 1 AND 39) IS NOT TRUE)";
    const char *items="SELECT NOT EXISTS (SELECT 1 FROM json_each(?1,'$.items') AS entry WHERE (type='object'"
        " AND (SELECT count(*) FROM json_each(entry.value))=5"
        " AND json_type(value,'$.id')='text' AND length(json_extract(value,'$.id')) BETWEEN 1 AND 19"
        " AND json_extract(value,'$.id') NOT GLOB '*[^0-9]*' AND substr(json_extract(value,'$.id'),1,1) BETWEEN '1' AND '9'"
        " AND (length(json_extract(value,'$.id'))<19 OR json_extract(value,'$.id')<='9223372036854775807')"
        " AND json_type(value,'$.workspace_id')='text' AND length(json_extract(value,'$.workspace_id')) BETWEEN 1 AND 19"
        " AND json_extract(value,'$.workspace_id') NOT GLOB '*[^0-9]*' AND substr(json_extract(value,'$.workspace_id'),1,1) BETWEEN '1' AND '9'"
        " AND (length(json_extract(value,'$.workspace_id'))<19 OR json_extract(value,'$.workspace_id')<='9223372036854775807')"
        " AND json_type(value,'$.position')='integer' AND json_extract(value,'$.position') BETWEEN 0 AND 19"
        " AND json_type(value,'$.name')='text' AND length(json_extract(value,'$.name')) BETWEEN 1 AND 63"
        " AND json_type(value,'$.target')='text' AND length(json_extract(value,'$.target')) BETWEEN 1 AND 259) IS NOT TRUE)";
    if(!sql_text(db,settings,json)||!sql_text(db,workspaces,json)||!sql_text(db,items,json))return FALSE;
    sqlite3_stmt *s=NULL;BOOL ok=sqlite3_prepare_v2(db,"SELECT atom FROM json_tree(?1) WHERE type='text'",-1,&s,NULL)==SQLITE_OK;
    if(ok)ok=sqlite3_bind_text(s,1,json,-1,SQLITE_TRANSIENT)==SQLITE_OK;
    int rc=SQLITE_DONE;
    while(ok&&(rc=sqlite3_step(s))==SQLITE_ROW){const char *text=(const char*)sqlite3_column_text(s,0);int bytes=sqlite3_column_bytes(s,0);if(bytes&&(!text||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,bytes,NULL,0)))ok=FALSE;}
    ok=ok&&rc==SQLITE_DONE;sqlite3_finalize(s);return ok;
}
static char *snapshot(sqlite3 *db,BOOL local){
    const char *sql="SELECT json_pretty(json_object('version',1,'application','NOVA Desktop','initialized',json('true'),"
        "'settings',json((SELECT json_group_array(json_object('scope',scope,'section',section,'key',key,'value',value)) FROM (SELECT * FROM settings WHERE NOT " LOCAL_STATE " ORDER BY scope,section,key))),"
        "'workspaces',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'position',position,'name',name)) FROM (SELECT * FROM workspaces ORDER BY position,id))),"
        "'items',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'workspace_id',CAST(workspace_id AS TEXT),'position',position,'name',name,'target',target)) FROM (SELECT * FROM items ORDER BY workspace_id,position,id)))),'  ')";
    const char *local_sql="SELECT json_pretty(json_object('version',1,'application','NOVA Desktop','initialized',json('true'),"
        "'settings',json((SELECT json_group_array(json_object('scope',scope,'section',section,'key',key,'value',value)) FROM (SELECT * FROM settings WHERE " LOCAL_STATE " ORDER BY scope,section,key))),"
        "'workspaces',json('[]'),'items',json('[]')),'  ')";
    sqlite3_stmt *s=NULL;char *copy=NULL;
    if(sqlite3_prepare_v2(db,local?local_sql:sql,-1,&s,NULL)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW){
        int size=sqlite3_column_bytes(s,0);const char *text=(const char*)sqlite3_column_text(s,0);
        if(text&&size>0&&size<JSON_LIMIT){copy=malloc((size_t)size+2);if(copy){memcpy(copy,text,(size_t)size);copy[size]='\n';copy[size+1]=0;}}
    }
    sqlite3_finalize(s);return copy;
}
static BOOL write_temp(const wchar_t *path,const char *json){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    DWORD length=(DWORD)strlen(json),written=0;
    BOOL ok=WriteFile(file,json,length,&written,NULL)&&written==length&&FlushFileBuffers(file);
    DWORD error=ok?0:GetLastError();CloseHandle(file);if(!ok){DeleteFileW(path);SetLastError(error);}return ok;
}
static void temp_path(const wchar_t *directory,const wchar_t *tag,wchar_t *path){
    unsigned long long nonce;sqlite3_randomness(sizeof(nonce),&nonce);
    swprintf(path,MAX_PATH,L"%ls\\.nova-%ls-%lu-%llx.tmp",directory,tag,GetCurrentProcessId(),nonce);
}
static BOOL valid_utf8(const char *json,int length){return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,json,length,NULL,0)>0;}
static BOOL import_json(sqlite3 *db,const char *json){
    BOOL ok=sql_text(db,"BEGIN IMMEDIATE",NULL)&&
        sql_text(db,"INSERT INTO settings SELECT json_extract(value,'$.scope'),json_extract(value,'$.section'),json_extract(value,'$.key'),json_extract(value,'$.value') FROM json_each(?1,'$.settings')",json)&&
        sql_text(db,"INSERT INTO workspaces SELECT CAST(json_extract(value,'$.id') AS INTEGER),json_extract(value,'$.position'),json_extract(value,'$.name') FROM json_each(?1,'$.workspaces')",json)&&
        sql_text(db,"INSERT INTO items SELECT CAST(json_extract(value,'$.id') AS INTEGER),CAST(json_extract(value,'$.workspace_id') AS INTEGER),json_extract(value,'$.position'),json_extract(value,'$.name'),json_extract(value,'$.target') FROM json_each(?1,'$.items')",json)&&
        sql_text(db,"COMMIT",NULL);
    if(!ok)sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);
    return ok;
}
static char *read_document(sqlite3 *db,const wchar_t *directory,const wchar_t *name,BOOL *missing,wchar_t *error){
    *missing=FALSE;wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\%ls",directory,name);
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND){*missing=TRUE;return NULL;}file_error(error);return NULL;}
    LARGE_INTEGER size;BOOL ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=JSON_LIMIT;
    char *data=ok?malloc((size_t)size.QuadPart+1):NULL;DWORD read=0;
    if(data){ok=ReadFile(file,data,(DWORD)size.QuadPart,&read,NULL)&&read==(DWORD)size.QuadPart;data[read]=0;}else ok=FALSE;
    CloseHandle(file);
    if(ok&&read>=3&&!memcmp(data,"\xef\xbb\xbf",3)){memmove(data,data+3,read-2);read-=3;}
    if(ok)ok=valid_utf8(data,(int)read)&&strlen(data)==read&&valid_json(db,data);
    if(!ok){free(data);invalid(error);return NULL;}
    return data;
}
BOOL config_json_load(sqlite3 *db,const wchar_t *directory,BOOL *initialized,wchar_t *error){
    *initialized=FALSE;BOOL missing;char *json=read_document(db,directory,L"nova.json",&missing,error);
    if(!json)return missing;
    sqlite3_stmt *s=NULL;BOOL ok=sqlite3_prepare_v2(db,"SELECT json_extract(?1,'$.initialized')",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,json,-1,SQLITE_TRANSIENT);ok=sqlite3_step(s)==SQLITE_ROW;if(ok)*initialized=sqlite3_column_int(s,0)!=0;}sqlite3_finalize(s);
    if(ok&&*initialized)ok=import_json(db,json);
    free(json);return ok?TRUE:invalid(error);
}
BOOL config_json_load_local(sqlite3 *db,const wchar_t *directory,BOOL *migrate,wchar_t *error){
    *migrate=sql_text(db,"SELECT EXISTS(SELECT 1 FROM settings WHERE " LOCAL_STATE ")",NULL);
    BOOL missing;char *json=read_document(db,directory,L"local.json",&missing,error);
    if(!json)return missing;
    const char *allowed="SELECT json_array_length(?1,'$.workspaces')=0 AND json_array_length(?1,'$.items')=0"
        " AND NOT EXISTS (SELECT 1 FROM (SELECT json_extract(value,'$.scope') AS scope,json_extract(value,'$.section') AS section,json_extract(value,'$.key') AS key FROM json_each(?1,'$.settings')) WHERE NOT " LOCAL_STATE ")"
        " AND NOT EXISTS (SELECT 1 FROM json_each(?1,'$.settings') GROUP BY json_extract(value,'$.scope'),json_extract(value,'$.section'),json_extract(value,'$.key') HAVING count(*)>1)";
    BOOL ok=sql_text(db,allowed,json)&&sql_text(db,"INSERT OR REPLACE INTO settings SELECT json_extract(value,'$.scope'),json_extract(value,'$.section'),json_extract(value,'$.key'),json_extract(value,'$.value') FROM json_each(?1,'$.settings')",json);
    free(json);
    if(ok){
        /* A synchronized tab deletion can invalidate a cached local selection.
           Normalize valid old selections only; malformed values still fail. */
        ok=sqlite3_exec(db,"UPDATE settings SET value='0' WHERE scope='files' AND section IN ('Pane0','Pane1','Pane2','Pane3') AND key='Selected'"
            " AND length(value) BETWEEN 1 AND 2 AND value NOT GLOB '*[^0-9]*' AND CAST(value AS INTEGER) BETWEEN 0 AND 11"
            " AND CAST(value AS INTEGER)>=COALESCE((SELECT CAST(value AS INTEGER) FROM settings AS counts WHERE counts.scope='files' AND counts.section=settings.section AND counts.key='Count'),1)",NULL,NULL,NULL)==SQLITE_OK;
        if(ok&&sqlite3_changes(db)>0)*migrate=TRUE;
    }
    return ok?TRUE:invalid(error);
}
BOOL config_json_legacy(sqlite3 *db,const wchar_t *directory,BOOL *imported,wchar_t *error){
    *imported=FALSE;wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.sqlite",directory);
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES){DWORD code=GetLastError();return code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND?TRUE:invalid(error);}
    char utf8[MAX_PATH*4];if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,utf8,sizeof(utf8),NULL,NULL))return invalid(error);
    sqlite3 *source=NULL;BOOL ok=sqlite3_open_v2(utf8,&source,SQLITE_OPEN_READONLY,NULL)==SQLITE_OK;
    if(ok)sqlite3_limit(source,SQLITE_LIMIT_LENGTH,JSON_LIMIT);
    if(ok)ok=sql_text(source,"SELECT (SELECT user_version FROM pragma_user_version)=1 AND (SELECT application_id FROM pragma_application_id)=1313822273",NULL);
    sqlite3_stmt *check=NULL;
    if(ok)ok=sqlite3_prepare_v2(source,"PRAGMA quick_check",-1,&check,NULL)==SQLITE_OK&&sqlite3_step(check)==SQLITE_ROW&&!strcmp((const char*)sqlite3_column_text(check,0),"ok");
    sqlite3_finalize(check);
    /* Export logical rows rather than copying database pages: old databases
       created with sqlite3_open16 can have a different text encoding. */
    char *json=NULL;
    if(ok){
        /* Old databases have one document. Serialize all scopes for validation. */
        sqlite3_stmt *s=NULL;
        const char *all="SELECT json_pretty(json_object('version',1,'application','NOVA Desktop','initialized',json('true'),'settings',json((SELECT json_group_array(json_object('scope',scope,'section',section,'key',key,'value',value)) FROM settings)),'workspaces',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'position',position,'name',name)) FROM workspaces)),'items',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'workspace_id',CAST(workspace_id AS TEXT),'position',position,'name',name,'target',target)) FROM items))))";
        if(sqlite3_prepare_v2(source,all,-1,&s,NULL)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW){const char *value=(const char*)sqlite3_column_text(s,0);if(value){size_t length=strlen(value);json=malloc(length+1);if(json)memcpy(json,value,length+1);}}
        sqlite3_finalize(s);
    }
    if(source)sqlite3_close(source);
    if(ok)ok=json&&valid_json(db,json)&&import_json(db,json);
    free(json);
    if(ok)*imported=TRUE;
    return ok?TRUE:invalid(error);
}
typedef struct {
    wchar_t path[MAX_PATH],temporary[MAX_PATH],previous[MAX_PATH];
    BOOL changed,existed,published;
} ConfigPublication;
static BOOL prepare_publication(const wchar_t *directory,const wchar_t *name,const char *json,ConfigPublication *publication){
    ZeroMemory(publication,sizeof(*publication));swprintf(publication->path,MAX_PATH,L"%ls\\%ls",directory,name);
    HANDLE file=CreateFileW(publication->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file!=INVALID_HANDLE_VALUE){
        publication->existed=TRUE;LARGE_INTEGER size;size_t length=strlen(json);BOOL same=FALSE;
        if(GetFileSizeEx(file,&size)&&size.QuadPart==(LONGLONG)length){char *old=malloc(length);DWORD read=0;if(old){same=ReadFile(file,old,(DWORD)length,&read,NULL)&&read==length&&!memcmp(old,json,length);free(old);}}
        CloseHandle(file);if(same)return TRUE;
    }else if(GetLastError()!=ERROR_FILE_NOT_FOUND)return FALSE;
    publication->changed=TRUE;temp_path(directory,L"write",publication->temporary);temp_path(directory,L"rollback",publication->previous);
    return write_temp(publication->temporary,json);
}
BOOL config_json_commit(sqlite3 *db,const wchar_t *directory,wchar_t *error){
    char *shared=snapshot(db,FALSE),*local=snapshot(db,TRUE);ConfigPublication publications[2];ZeroMemory(publications,sizeof(publications));
    BOOL ok=shared&&local;
    if(ok)ok=prepare_publication(directory,L"local.json",local,&publications[0])&&prepare_publication(directory,L"nova.json",shared,&publications[1]);
    free(shared);free(local);
    for(int i=0;i<2&&ok;i++)if(publications[i].changed){
        ConfigPublication *p=&publications[i];ok=p->existed?ReplaceFileW(p->path,p->temporary,p->previous,0,NULL,NULL):MoveFileExW(p->temporary,p->path,MOVEFILE_WRITE_THROUGH);p->published=ok;
    }
    DWORD code=ok?0:GetLastError();
    if(ok)ok=sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK;
    if(!ok){
        sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);
        for(int i=1;i>=0;i--){ConfigPublication *p=&publications[i];if(p->published){if(p->existed)MoveFileExW(p->previous,p->path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);else DeleteFileW(p->path);}}
    }
    for(int i=0;i<2;i++){if(publications[i].temporary[0])DeleteFileW(publications[i].temporary);if(ok&&publications[i].previous[0])DeleteFileW(publications[i].previous);}
    if(!ok){SetLastError(code);return file_error(error);}return TRUE;
}
BOOL config_json_backup(const wchar_t *directory,wchar_t *error){
    const wchar_t *names[]={L"nova",L"local"};
    for(int i=0;i<2;i++){
        wchar_t path[MAX_PATH],backup[MAX_PATH],temporary[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\%ls.json",directory,names[i]);swprintf(backup,MAX_PATH,L"%ls\\%ls.backup.json",directory,names[i]);temp_path(directory,L"backup",temporary);
        BOOL ok=CopyFileW(path,temporary,TRUE)&&MoveFileExW(temporary,backup,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok){DWORD code=GetLastError();DeleteFileW(temporary);SetLastError(code);return file_error(error);}
    }
    return TRUE;
}
