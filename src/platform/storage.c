#include "storage.h"
#include "../../third_party/sqlite/sqlite3.h"
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

static sqlite3 *db;
static wchar_t root[MAX_PATH],error_text[320];
static BOOL transaction_failed;
static BOOL check(int rc){
    if(rc==SQLITE_OK||rc==SQLITE_DONE||rc==SQLITE_ROW)return TRUE;
    transaction_failed=TRUE;
    swprintf(error_text,320,L"本地数据库操作失败（%d）：%ls",rc,db?(const wchar_t*)sqlite3_errmsg16(db):L"无法打开数据库");return FALSE;
}
static BOOL execute(const char *sql){return check(sqlite3_exec(db,sql,NULL,NULL,NULL));}
static sqlite3_stmt *prepare(const char *sql){sqlite3_stmt *s=NULL;if(!db||!check(sqlite3_prepare_v2(db,sql,-1,&s,NULL)))return NULL;return s;}
static BOOL bind_text(sqlite3_stmt *s,int n,const wchar_t *value){return check(sqlite3_bind_text16(s,n,value,-1,SQLITE_TRANSIENT));}
static BOOL finish(sqlite3_stmt *s){BOOL ok=check(sqlite3_step(s));return check(sqlite3_finalize(s))&&ok;}
static void read_text(sqlite3_stmt *s,int column,wchar_t *out,int capacity){const wchar_t *text=sqlite3_column_text16(s,column);lstrcpynW(out,text?text:L"",capacity);}
BOOL store_ready(void){return db!=NULL;}
const wchar_t *store_error(void){return error_text;}
BOOL store_begin(void){if(!db)return FALSE;transaction_failed=FALSE;return execute("BEGIN IMMEDIATE");}
BOOL store_end(BOOL success){
    if(!db)return FALSE;
    if(!success||transaction_failed){sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);return FALSE;}
    if(execute("COMMIT"))return TRUE;
    sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);return FALSE;
}
void store_close(void){if(db){sqlite3_close_v2(db);db=NULL;}root[0]=0;}
BOOL store_open(const wchar_t *directory){
    if(db){if(!lstrcmpiW(directory,root))return TRUE;lstrcpyW(error_text,L"已有其他数据目录打开。");return FALSE;}
    wchar_t path[MAX_PATH];if(wcslen(directory)>MAX_PATH-32){lstrcpyW(error_text,L"数据目录路径太长。");return FALSE;}
    swprintf(path,MAX_PATH,L"%ls\\nova.sqlite",directory);
    if(!check(sqlite3_open16(path,&db))){store_close();return FALSE;}
    lstrcpynW(root,directory,MAX_PATH);sqlite3_busy_timeout(db,500);
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,1024*1024);
    sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,65536);
    sqlite3_stmt *s=prepare("PRAGMA user_version");
    if(!s){store_close();return FALSE;}
    int rc=sqlite3_step(s),version=rc==SQLITE_ROW?sqlite3_column_int(s,0):-1;sqlite3_finalize(s);
    if(version<0||version>1){lstrcpyW(error_text,L"数据库损坏或由更新版本创建，原数据未修改。");store_close();return FALSE;}
    s=prepare("PRAGMA application_id");if(!s){store_close();return FALSE;}
    rc=sqlite3_step(s);int app=rc==SQLITE_ROW?sqlite3_column_int(s,0):-1;sqlite3_finalize(s);
    if((version==1&&app!=0x4e4f5641)||(version==0&&app!=0&&app!=0x4e4f5641)){lstrcpyW(error_text,L"数据文件不是 NOVA 数据库，未修改原文件。");store_close();return FALSE;}
    if(version==0){
        s=prepare("SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");if(!s){store_close();return FALSE;}
        rc=sqlite3_step(s);BOOL empty=rc==SQLITE_ROW&&sqlite3_column_int(s,0)==0;sqlite3_finalize(s);
        if(!empty){lstrcpyW(error_text,L"未识别的数据文件，已停止迁移并保留原文件。");store_close();return FALSE;}
    }
    if(!execute("PRAGMA foreign_keys=ON; PRAGMA cache_size=-512; PRAGMA mmap_size=0; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA temp_store=FILE; PRAGMA trusted_schema=OFF;")){store_close();return FALSE;}
    if(version==0){
        if(!store_begin()){store_close();return FALSE;}
        BOOL ok=execute("CREATE TABLE IF NOT EXISTS settings(scope TEXT NOT NULL,section TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT NULL,PRIMARY KEY(scope,section,key)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS workspaces(id INTEGER PRIMARY KEY,position INTEGER NOT NULL,name TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS items(id INTEGER PRIMARY KEY,workspace_id INTEGER NOT NULL REFERENCES workspaces(id) ON DELETE CASCADE,position INTEGER NOT NULL,name TEXT NOT NULL,target TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS items_workspace ON items(workspace_id,position); PRAGMA application_id=1313822273; PRAGMA user_version=1;");
        if(!store_end(ok)){store_close();return FALSE;}
    }
    s=prepare("PRAGMA quick_check");if(!s){store_close();return FALSE;}
    rc=sqlite3_step(s);BOOL healthy=rc==SQLITE_ROW&&!strcmp((const char*)sqlite3_column_text(s,0),"ok");sqlite3_finalize(s);
    if(!healthy){lstrcpyW(error_text,L"数据库完整性检查失败，原文件保留，请从备份恢复。");store_close();return FALSE;}
    return TRUE;
}
BOOL store_get(const wchar_t *scope,const wchar_t *section,const wchar_t *key,const wchar_t *fallback,wchar_t *value,int capacity){
    lstrcpynW(value,fallback,capacity);sqlite3_stmt *s=prepare("SELECT value FROM settings WHERE scope=? AND section=? AND key=?");if(!s)return FALSE;
    bind_text(s,1,scope);bind_text(s,2,section);bind_text(s,3,key);int rc=sqlite3_step(s);if(rc==SQLITE_ROW)read_text(s,0,value,capacity);
    BOOL ok=check(rc);sqlite3_finalize(s);return ok;
}
int store_int(const wchar_t *scope,const wchar_t *section,const wchar_t *key,int fallback){
    wchar_t value[48],*end;store_get(scope,section,key,L"",value,48);if(!value[0])return fallback;
    long n=wcstol(value,&end,10);return *end?fallback:(int)n;
}
BOOL store_set(const wchar_t *scope,const wchar_t *section,const wchar_t *key,const wchar_t *value){
    sqlite3_stmt *s=prepare("INSERT INTO settings VALUES(?,?,?,?) ON CONFLICT(scope,section,key) DO UPDATE SET value=excluded.value WHERE value<>excluded.value");if(!s)return FALSE;
    bind_text(s,1,scope);bind_text(s,2,section);bind_text(s,3,key);bind_text(s,4,value);return finish(s);
}
BOOL store_set_int(const wchar_t *scope,const wchar_t *section,const wchar_t *key,int value){wchar_t text[24];swprintf(text,24,L"%d",value);return store_set(scope,section,key,text);}
BOOL store_clear(const wchar_t *scope){sqlite3_stmt *s=prepare("DELETE FROM settings WHERE scope=?");if(!s)return FALSE;bind_text(s,1,scope);return finish(s);}
long long store_new_id(void){sqlite3_int64 id;do{sqlite3_randomness(sizeof(id),&id);id&=0x7fffffffffffffffLL;}while(!id);return id;}
int store_load_workspaces(Workspace *spaces){
    sqlite3_stmt *s=prepare("SELECT w.id,w.name,(SELECT count(*) FROM items WHERE workspace_id=w.id) FROM workspaces w ORDER BY position,id");if(!s)return -1;
    int n=0,rc;while((rc=sqlite3_step(s))==SQLITE_ROW){
        if(n==MAX_WORKSPACES||sqlite3_column_int(s,2)>MAX_APPS){lstrcpyW(error_text,L"数据库项目数量超过本版本容量，未截断或覆盖数据。");sqlite3_finalize(s);return -1;}
        Workspace *w=&spaces[n++];ZeroMemory(w,sizeof(*w));w->id=sqlite3_column_int64(s,0);read_text(s,1,w->name,40);w->app_count=sqlite3_column_int(s,2);
    }
    BOOL ok=check(rc);sqlite3_finalize(s);return ok?n:-1;
}
BOOL store_load_items(Workspace *w){
    if(w->items_loaded)return TRUE;
    sqlite3_stmt *s=prepare("SELECT id,name,target FROM items WHERE workspace_id=? ORDER BY position,id");if(!s)return FALSE;
    sqlite3_bind_int64(s,1,w->id);int n=0,rc;
    while((rc=sqlite3_step(s))==SQLITE_ROW&&n<MAX_APPS){AppItem *a=&w->apps[n++];a->id=sqlite3_column_int64(s,0);read_text(s,1,a->name,64);read_text(s,2,a->target,NOVA_PATH_CAP);}
    BOOL ok=check(rc)&&rc==SQLITE_DONE;sqlite3_finalize(s);if(ok){w->app_count=n;w->items_loaded=1;}return ok;
}
BOOL store_save_workspaces(Workspace *spaces,int count,int active,BOOL pinned){
    if(count<1||count>MAX_WORKSPACES||!store_begin())return FALSE;
    BOOL ok=TRUE;sqlite3_stmt *s;
    /* IDs survive reorder, rename and cross-workspace moves. New IDs are independent of transaction rollback. */
    for(int i=0;i<count&&ok;i++){
        Workspace *w=&spaces[i];if(!w->id)w->id=store_new_id();
        s=prepare("INSERT INTO workspaces VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET position=excluded.position,name=excluded.name WHERE position<>excluded.position OR name<>excluded.name");
        if(!s){ok=FALSE;break;}sqlite3_bind_int64(s,1,w->id);sqlite3_bind_int(s,2,i);bind_text(s,3,w->name);ok=finish(s);
    }
    /* Delete removed workspaces with a bound retained-ID list; never remove unloaded items. */
    s=prepare("DELETE FROM workspaces WHERE id NOT IN (?,?,?,?,?,?,?,?)");
    if(!s)ok=FALSE;else{for(int i=0;i<MAX_WORKSPACES;i++)sqlite3_bind_int64(s,i+1,i<count?spaces[i].id:0);ok=finish(s)&&ok;}
    /* Remove absent items, but leave unchanged rows/pages untouched. */
    char removal[256]="DELETE FROM items WHERE workspace_id=? AND id NOT IN (";
    for(int i=0;i<MAX_APPS;i++)strcat(removal,i?",?":"?");
    strcat(removal,")");
    for(int i=0;i<count&&ok;i++)if(spaces[i].items_loaded){
        Workspace *w=&spaces[i];if(w->app_count<0||w->app_count>MAX_APPS){ok=FALSE;break;}
        s=prepare(removal);if(!s){ok=FALSE;break;}sqlite3_bind_int64(s,1,w->id);
        for(int j=0;j<MAX_APPS;j++)sqlite3_bind_int64(s,j+2,j<w->app_count?w->apps[j].id:0);
        ok=finish(s);
    }
    for(int i=0;i<count&&ok;i++)if(spaces[i].items_loaded){
        Workspace *w=&spaces[i];if(w->app_count<0||w->app_count>MAX_APPS){ok=FALSE;break;}
        for(int j=0;j<w->app_count&&ok;j++){
            AppItem *a=&w->apps[j];if(!a->id)a->id=store_new_id();
            s=prepare("INSERT INTO items VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET workspace_id=excluded.workspace_id,position=excluded.position,name=excluded.name,target=excluded.target WHERE workspace_id<>excluded.workspace_id OR position<>excluded.position OR name<>excluded.name OR target<>excluded.target");if(!s){ok=FALSE;break;}
            sqlite3_bind_int64(s,1,a->id);sqlite3_bind_int64(s,2,w->id);sqlite3_bind_int(s,3,j);bind_text(s,4,a->name);bind_text(s,5,a->target);ok=finish(s);
        }
    }
    ok=store_set_int(L"app",L"Nova",L"Active",active)&&ok;ok=store_set_int(L"app",L"Nova",L"AlwaysOnTop",pinned)&&ok;
    return store_end(ok);
}
BOOL store_backup(void){
    if(!db)return FALSE;
    wchar_t path[MAX_PATH],temp[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.backup.sqlite",root);swprintf(temp,MAX_PATH,L"%ls\\nova.backup.tmp",root);
    sqlite3 *dest=NULL;if(sqlite3_open16(temp,&dest)!=SQLITE_OK){if(dest)sqlite3_close(dest);return FALSE;}
    sqlite3_backup *b=sqlite3_backup_init(dest,"main",db,"main");BOOL ok=FALSE;
    if(b){int rc=sqlite3_backup_step(b,-1);ok=sqlite3_backup_finish(b)==SQLITE_OK&&rc==SQLITE_DONE;}
    sqlite3_close(dest);return ok&&MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
