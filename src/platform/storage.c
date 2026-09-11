#include "storage.h"
#include "../i18n.h"
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
    swprintf(error_text,320,nova_text(L"本地数据库操作失败（%d）：%ls",L"Local database operation failed (%d): %ls"),rc,db?(const wchar_t*)sqlite3_errmsg16(db):nova_text(L"无法打开数据库",L"unable to open database"));return FALSE;
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
    if(db){if(!lstrcmpiW(directory,root))return TRUE;lstrcpyW(error_text,nova_text(L"已有其他数据目录打开。",L"Another data folder is already open."));return FALSE;}
    wchar_t path[MAX_PATH];if(wcslen(directory)>MAX_PATH-32){lstrcpyW(error_text,nova_text(L"数据目录路径太长。",L"The data folder path is too long."));return FALSE;}
    swprintf(path,MAX_PATH,L"%ls\\nova.sqlite",directory);
    if(!check(sqlite3_open16(path,&db))){store_close();return FALSE;}
    lstrcpynW(root,directory,MAX_PATH);sqlite3_busy_timeout(db,500);
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,1024*1024);
    sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,65536);
    sqlite3_stmt *s=prepare("PRAGMA user_version");
    if(!s){store_close();return FALSE;}
    int rc=sqlite3_step(s),version=rc==SQLITE_ROW?sqlite3_column_int(s,0):-1;sqlite3_finalize(s);
    if(version<0||version>1){lstrcpyW(error_text,nova_text(L"数据库损坏或由更新版本创建，原数据未修改。",L"The database is damaged or was created by a newer version. The original data was not changed."));store_close();return FALSE;}
    s=prepare("PRAGMA application_id");if(!s){store_close();return FALSE;}
    rc=sqlite3_step(s);int app=rc==SQLITE_ROW?sqlite3_column_int(s,0):-1;sqlite3_finalize(s);
    if((version==1&&app!=0x4e4f5641)||(version==0&&app!=0&&app!=0x4e4f5641)){lstrcpyW(error_text,nova_text(L"数据文件不是 NOVA 数据库，未修改原文件。",L"The data file is not a NOVA database. The original file was not changed."));store_close();return FALSE;}
    if(version==0){
        s=prepare("SELECT count(*) FROM sqlite_schema WHERE name NOT LIKE 'sqlite_%'");if(!s){store_close();return FALSE;}
        rc=sqlite3_step(s);BOOL empty=rc==SQLITE_ROW&&sqlite3_column_int(s,0)==0;sqlite3_finalize(s);
        if(!empty){lstrcpyW(error_text,nova_text(L"未识别的数据文件，已停止迁移并保留原文件。",L"The data file is not recognized. Migration stopped and the original file was preserved."));store_close();return FALSE;}
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
    if(!healthy){lstrcpyW(error_text,nova_text(L"数据库完整性检查失败，原文件保留，请从备份恢复。",L"The database integrity check failed. The original file was preserved; restore from the backup."));store_close();return FALSE;}
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
        if(n==MAX_WORKSPACES||sqlite3_column_int(s,2)>MAX_APPS){lstrcpyW(error_text,nova_text(L"数据库项目数量超过本版本容量，未截断或覆盖数据。",L"The database contains more items than this version supports. No data was truncated or overwritten."));sqlite3_finalize(s);return -1;}
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
BOOL store_load_batch_task(BatchTask *task){
    if(!task)return FALSE;
    ZeroMemory(task,sizeof(*task));
    wchar_t command[BATCH_COMMAND_CAP+1];
    if(!store_get(L"batch_git_pull_main",L"Task",L"Command",L"git pull origin main",command,BATCH_COMMAND_CAP+1))return FALSE;
    if(wcslen(command)>=BATCH_COMMAND_CAP){lstrcpyW(error_text,nova_text(L"保存的命令超过长度限制，未修改原数据。",L"The saved command exceeds the length limit. Data was not changed."));return FALSE;}
    lstrcpyW(task->command,command);
    int count=store_int(L"batch_git_pull_main",L"Task",L"DirectoryCount",0);
    if(count<0||count>BATCH_DIRECTORY_LIMIT){lstrcpyW(error_text,nova_text(L"批量任务目录数量超过本版本容量，原数据未修改。",L"The batch task contains more folders than this version supports. The original data was not changed."));return FALSE;}
    for(int i=0;i<count;i++){
        wchar_t key[32],directory[BATCH_DIRECTORY_CAP];swprintf(key,32,L"Directory%d",i);
        if(!store_get(L"batch_git_pull_main",L"Task",key,L"",directory,BATCH_DIRECTORY_CAP)||batch_task_add_directory(task,directory)!=1){
            lstrcpyW(error_text,nova_text(L"批量任务数据无效或包含重复目录，原数据未修改。",L"The batch task data is invalid or contains duplicate folders. The original data was not changed."));ZeroMemory(task,sizeof(*task));return FALSE;
        }
    }
    return TRUE;
}
BOOL store_save_batch_task(const BatchTask *task){
    if(!batch_task_is_valid(task)){lstrcpyW(error_text,nova_text(L"批量任务数据无效，未保存。",L"The batch task data is invalid and was not saved."));return FALSE;}
    if(!store_begin())return FALSE;
    BOOL ok=store_clear(L"batch_git_pull_main");
    ok=store_set(L"batch_git_pull_main",L"Task",L"Command",task->command)&&ok;
    ok=store_set_int(L"batch_git_pull_main",L"Task",L"DirectoryCount",task->directory_count)&&ok;
    for(int i=0;i<task->directory_count&&ok;i++){
        wchar_t key[32];swprintf(key,32,L"Directory%d",i);ok=store_set(L"batch_git_pull_main",L"Task",key,task->directories[i]);
    }
    return store_end(ok);
}
/* Versioned collection in a separate scope; the legacy single task is kept intact. */
static BOOL batch_read_text(const wchar_t *section,const wchar_t *key,wchar_t *value,int capacity){
    sqlite3_stmt *s=prepare("SELECT value FROM settings WHERE scope='batch_tasks' AND section=? AND key=?");
    if(!s)return FALSE;
    bind_text(s,1,section);bind_text(s,2,key);
    int rc=sqlite3_step(s);BOOL ok=FALSE;
    if(rc==SQLITE_ROW){
        const wchar_t *text=sqlite3_column_text16(s,0);
        int bytes=sqlite3_column_bytes16(s,0);
        if(text&&bytes>=0&&bytes<capacity*(int)sizeof(wchar_t)&&wcslen(text)==(size_t)bytes/sizeof(wchar_t)){
            lstrcpyW(value,text);ok=TRUE;
        }
    }
    if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)check(rc);
    sqlite3_finalize(s);
    if(!ok)lstrcpyW(error_text,nova_text(L"批量任务配置缺失、损坏或超过容量，未修改原数据。",L"Batch task settings are missing, damaged, or exceed capacity. Data was not changed."));
    return ok;
}
static BOOL batch_read_number(const wchar_t *section,const wchar_t *key,int maximum,int *value){
    wchar_t text[32],*end;
    if(!batch_read_text(section,key,text,32)||!text[0])return FALSE;
    long parsed=wcstol(text,&end,10);
    if(*end||parsed<0||parsed>maximum){
        lstrcpyW(error_text,nova_text(L"批量任务数量、版本或执行模式无效，未修改原数据。",L"Invalid batch task count, version, or execution mode. Data was not changed."));return FALSE;
    }
    *value=(int)parsed;return TRUE;
}
BOOL store_save_batch_tasks(BatchTaskList *tasks){
    if(!tasks||tasks->count<0||tasks->count>BATCH_TASK_LIMIT)return FALSE;
    for(int i=0;i<tasks->count;i++){
        BatchTask *t=&tasks->tasks[i];
        if(!t->id)t->id=store_new_id();
        if(t->id<0)return FALSE;
        for(int j=0;j<i;j++)if(tasks->tasks[j].id==t->id){lstrcpyW(error_text,nova_text(L"批量任务标识重复，未保存。",L"Duplicate batch task ID. Nothing was saved."));return FALSE;}
        if(!batch_task_is_valid(t)||!t->name[wcsspn(t->name,L" \t\r\n")]||!t->command[wcsspn(t->command,L" \t\r\n")]){
            lstrcpyW(error_text,nova_text(L"请填写任务名称和命令，并选择有效执行模式。",L"Enter a task name and command, and select a valid execution mode."));return FALSE;
        }
    }
    if(!store_begin())return FALSE;
    BOOL ok=store_clear(L"batch_tasks");
    ok=store_set_int(L"batch_tasks",L"Collection",L"Version",2)&&ok;
    ok=store_set_int(L"batch_tasks",L"Collection",L"Count",tasks->count)&&ok;
    for(int i=0;i<tasks->count&&ok;i++){
        const BatchTask *t=&tasks->tasks[i];wchar_t section[32];swprintf(section,32,L"Task%d",i);
        wchar_t identity[40];swprintf(identity,40,L"%lld",t->id);ok=store_set(L"batch_tasks",section,L"Id",identity)&&ok;
        ok=store_set(L"batch_tasks",section,L"Name",t->name)&&ok;
        ok=store_set(L"batch_tasks",section,L"Command",t->command)&&ok;
        ok=store_set_int(L"batch_tasks",section,L"Mode",t->mode)&&ok;
        ok=store_set_int(L"batch_tasks",section,L"DirectoryCount",t->directory_count)&&ok;
        for(int j=0;j<t->directory_count&&ok;j++){
            wchar_t key[32];swprintf(key,32,L"Directory%d",j);ok=store_set(L"batch_tasks",section,key,t->directories[j]);
        }
    }
    return store_end(ok);
}
BOOL store_load_batch_tasks(BatchTaskList *tasks){
    if(!tasks)return FALSE;
    BatchTaskList *next=calloc(1,sizeof(*next));if(!next)return FALSE;
    sqlite3_stmt *s=prepare("SELECT count(*) FROM settings WHERE scope='batch_tasks'");
    if(!s){free(next);return FALSE;}
    int rc=sqlite3_step(s);BOOL fresh=rc==SQLITE_ROW&&sqlite3_column_int(s,0)==0;
    BOOL ok=check(rc);sqlite3_finalize(s);
    if(ok&&fresh){
        next->count=1;ok=store_load_batch_task(&next->tasks[0]);
        lstrcpyW(next->tasks[0].name,nova_text(L"任务 1",L"Task 1"));
        if(ok&&!next->tasks[0].command[0])lstrcpyW(next->tasks[0].command,L"git pull origin main");
        if(ok)ok=store_save_batch_tasks(next);
    }else if(ok){
        int version=0;
        ok=batch_read_number(L"Collection",L"Version",2,&version)&&version>=1;
        if(ok)ok=batch_read_number(L"Collection",L"Count",BATCH_TASK_LIMIT,&next->count);
        for(int i=0;i<next->count&&ok;i++){
            BatchTask *t=&next->tasks[i];wchar_t section[32];swprintf(section,32,L"Task%d",i);
            if(version==2){
                wchar_t identity[40],target[64];
                if(!batch_read_text(section,L"Id",identity,40)){ok=FALSE;break;}
                swprintf(target,64,L"nova-batch:%ls",identity);t->id=batch_task_target_id(target);
                if(!t->id){ok=FALSE;break;}
                for(int j=0;j<i;j++)if(next->tasks[j].id==t->id)ok=FALSE;
                if(!ok)break;
            }
            ok=batch_read_text(section,L"Name",t->name,BATCH_NAME_CAP)&&
                batch_read_text(section,L"Command",t->command,BATCH_COMMAND_CAP)&&
                batch_read_number(section,L"Mode",BATCH_PARALLEL,&t->mode)&&
                batch_read_number(section,L"DirectoryCount",BATCH_DIRECTORY_LIMIT,&t->directory_count);
            for(int j=0;j<t->directory_count&&ok;j++){
                wchar_t key[32];swprintf(key,32,L"Directory%d",j);
                ok=batch_read_text(section,key,t->directories[j],BATCH_DIRECTORY_CAP);
            }
            if(ok)ok=batch_task_is_valid(t)&&t->name[0]&&t->command[0];
        }
        if(ok&&version==1)ok=store_save_batch_tasks(next);
    }
    if(ok)*tasks=*next;
    else lstrcpyW(error_text,nova_text(L"批量任务配置无效或来自更新版本，原数据未修改。",L"Batch task settings are invalid or from a newer version. Data was not changed."));
    free(next);return ok;
}
BOOL store_save_file_commands(const FileCommandList *commands){
    if(!file_command_list_is_valid(commands)){
        lstrcpyW(error_text,nova_text(L"命令预设无效：请填写名称和命令，并确保名称不重复。",L"Command presets are invalid. Enter a name and command, and use unique names."));
        return FALSE;
    }
    if(!store_begin())return FALSE;
    BOOL ok=store_clear(L"file_commands");
    ok=store_set_int(L"file_commands",L"Commands",L"Version",1)&&ok;
    ok=store_set_int(L"file_commands",L"Commands",L"Count",commands->count)&&ok;
    ok=store_set_int(L"file_commands",L"Commands",L"Default",commands->default_index)&&ok;
    for(int i=0;i<commands->count&&ok;i++){
        wchar_t section[32];swprintf(section,32,L"Command%d",i);
        ok=store_set(L"file_commands",section,L"Name",commands->items[i].name)&&ok;
        ok=store_set(L"file_commands",section,L"Command",commands->items[i].command)&&ok;
    }
    return store_end(ok);
}

static BOOL command_read_text(const wchar_t *section,const wchar_t *key,wchar_t *value,int capacity){
    sqlite3_stmt *s=prepare("SELECT value FROM settings WHERE scope='file_commands' AND section=? AND key=?");if(!s)return FALSE;
    bind_text(s,1,section);bind_text(s,2,key);int rc=sqlite3_step(s);BOOL ok=FALSE;
    if(rc==SQLITE_ROW){
        const wchar_t *text=sqlite3_column_text16(s,0);int bytes=sqlite3_column_bytes16(s,0);
        if(text&&bytes>=0&&bytes<capacity*(int)sizeof(wchar_t)&&wcslen(text)==(size_t)bytes/sizeof(wchar_t)){lstrcpyW(value,text);ok=TRUE;}
    }
    if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)check(rc);
    sqlite3_finalize(s);return ok;
}
static BOOL command_read_number(const wchar_t *key,int maximum,int *value){
    wchar_t text[32],*end;if(!command_read_text(L"Commands",key,text,32)||!text[0])return FALSE;
    long parsed=wcstol(text,&end,10);if(*end||parsed<0||parsed>maximum)return FALSE;*value=(int)parsed;return TRUE;
}

BOOL store_load_file_commands(FileCommandList *commands){
    if(!commands)return FALSE;
    sqlite3_stmt *s=prepare("SELECT count(*) FROM settings WHERE scope='file_commands'");if(!s)return FALSE;
    int rc=sqlite3_step(s);BOOL fresh=rc==SQLITE_ROW&&sqlite3_column_int(s,0)==0,ok=check(rc);sqlite3_finalize(s);
    if(!ok)return FALSE;
    if(fresh){file_command_defaults(commands);return store_save_file_commands(commands);}
    FileCommandList next={0};
    int version=0;ok=command_read_number(L"Version",1,&version)&&version==1;
    if(ok)ok=command_read_number(L"Count",FILE_COMMAND_LIMIT,&next.count)&&next.count>=1;
    if(ok)ok=command_read_number(L"Default",FILE_COMMAND_LIMIT-1,&next.default_index)&&next.default_index<next.count;
    for(int i=0;i<next.count&&ok;i++){
        wchar_t section[32];swprintf(section,32,L"Command%d",i);
        ok=command_read_text(section,L"Name",next.items[i].name,FILE_COMMAND_NAME_CAP)&&
           command_read_text(section,L"Command",next.items[i].command,FILE_COMMAND_TEXT_CAP);
    }
    ok=ok&&file_command_list_is_valid(&next);
    if(ok)*commands=next;
    else lstrcpyW(error_text,nova_text(L"命令预设缺失、损坏或来自更新版本，原数据未修改。",L"Command presets are missing, damaged, or from a newer version. The original data was not changed."));
    return ok;
}

BOOL store_backup(void){
    if(!db)return FALSE;
    wchar_t path[MAX_PATH],temp[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.backup.sqlite",root);swprintf(temp,MAX_PATH,L"%ls\\nova.backup.tmp",root);
    sqlite3 *dest=NULL;if(sqlite3_open16(temp,&dest)!=SQLITE_OK){if(dest)sqlite3_close(dest);return FALSE;}
    sqlite3_backup *b=sqlite3_backup_init(dest,"main",db,"main");BOOL ok=FALSE;
    if(b){int rc=sqlite3_backup_step(b,-1);ok=sqlite3_backup_finish(b)==SQLITE_OK&&rc==SQLITE_DONE;}
    sqlite3_close(dest);return ok&&MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
