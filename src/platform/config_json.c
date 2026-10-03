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
static char *snapshot(sqlite3 *db){
    const char *sql="SELECT json_pretty(json_object('version',1,'application','NOVA Desktop','initialized',json('true'),"
        "'settings',json((SELECT json_group_array(json_object('scope',scope,'section',section,'key',key,'value',value)) FROM (SELECT * FROM settings ORDER BY scope,section,key))),"
        "'workspaces',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'position',position,'name',name)) FROM (SELECT * FROM workspaces ORDER BY position,id))),"
        "'items',json((SELECT json_group_array(json_object('id',CAST(id AS TEXT),'workspace_id',CAST(workspace_id AS TEXT),'position',position,'name',name,'target',target)) FROM (SELECT * FROM items ORDER BY workspace_id,position,id)))),'  ')";
    sqlite3_stmt *s=NULL;char *copy=NULL;
    if(sqlite3_prepare_v2(db,sql,-1,&s,NULL)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW){
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
BOOL config_json_load(sqlite3 *db,const wchar_t *directory,BOOL *initialized,wchar_t *error){
    *initialized=FALSE;wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.json",directory);
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return TRUE;return file_error(error);}
    LARGE_INTEGER size;BOOL ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=JSON_LIMIT;
    char *data=ok?malloc((size_t)size.QuadPart+1):NULL;DWORD read=0;
    if(data){ok=ReadFile(file,data,(DWORD)size.QuadPart,&read,NULL)&&read==(DWORD)size.QuadPart;data[read]=0;}else ok=FALSE;
    CloseHandle(file);char *json=data;
    if(ok&&read>=3&&!memcmp(data,"\xef\xbb\xbf",3)){json+=3;read-=3;}
    if(ok)ok=valid_utf8(json,(int)read)&&strlen(json)==read&&valid_json(db,json);
    if(ok){
        sqlite3_stmt *s=NULL;ok=sqlite3_prepare_v2(db,"SELECT json_extract(?1,'$.initialized')",-1,&s,NULL)==SQLITE_OK;
        if(ok){sqlite3_bind_text(s,1,json,-1,SQLITE_TRANSIENT);ok=sqlite3_step(s)==SQLITE_ROW;if(ok)*initialized=sqlite3_column_int(s,0)!=0;}sqlite3_finalize(s);
    }
    if(ok&&*initialized)ok=import_json(db,json);
    free(data);return ok?TRUE:invalid(error);
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
    char *json=ok?snapshot(source):NULL;
    if(source)sqlite3_close(source);
    if(ok)ok=json&&valid_json(db,json)&&import_json(db,json);
    free(json);
    if(ok)*imported=TRUE;
    return ok?TRUE:invalid(error);
}
BOOL config_json_commit(sqlite3 *db,const wchar_t *directory,wchar_t *error){
    char *json=snapshot(db);if(!json){sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);return invalid(error);}
    wchar_t path[MAX_PATH],temporary[MAX_PATH],previous[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.json",directory);
    temp_path(directory,L"write",temporary);temp_path(directory,L"rollback",previous);
    BOOL existed=GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
    BOOL ok=write_temp(temporary,json);free(json);
    if(ok)ok=existed?ReplaceFileW(path,temporary,previous,0,NULL,NULL):MoveFileExW(temporary,path,MOVEFILE_WRITE_THROUGH);
    if(!ok){DWORD code=GetLastError();sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);DeleteFileW(temporary);SetLastError(code);return file_error(error);}
    ok=sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK;
    if(!ok){sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);if(existed)MoveFileExW(previous,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);else DeleteFileW(path);return invalid(error);}
    if(existed)DeleteFileW(previous);
    return TRUE;
}
BOOL config_json_backup(const wchar_t *directory,wchar_t *error){
    wchar_t path[MAX_PATH],backup[MAX_PATH],temporary[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.json",directory);swprintf(backup,MAX_PATH,L"%ls\\nova.backup.json",directory);temp_path(directory,L"backup",temporary);
    BOOL ok=CopyFileW(path,temporary,TRUE)&&MoveFileExW(temporary,backup,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok){DWORD code=GetLastError();DeleteFileW(temporary);SetLastError(code);return file_error(error);}return TRUE;
}
