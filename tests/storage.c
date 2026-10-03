#define wWinMain nova_entry
#include "../src/main.c"
#undef wWinMain
#include "../third_party/sqlite/sqlite3.h"
#include <assert.h>

static DWORD read_file(const wchar_t *path,BYTE *bytes,DWORD capacity){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(f!=INVALID_HANDLE_VALUE);
    DWORD n=0;assert(ReadFile(f,bytes,capacity,&n,NULL));CloseHandle(f);return n;
}
static void write_text_file(const wchar_t *path,const char *text){
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);assert(file!=INVALID_HANDLE_VALUE);
    DWORD written,length=(DWORD)strlen(text);assert(WriteFile(file,text,length,&written,NULL)&&written==length);CloseHandle(file);
}
static void refused_json(const wchar_t *directory,const char *text){
    wchar_t path[MAX_PATH];swprintf(path,MAX_PATH,L"%ls\\nova.json",directory);write_text_file(path,text);
    assert(!store_open(directory)&&!store_ready());BYTE bytes[4096];DWORD length=read_file(path,bytes,sizeof(bytes));assert(length==strlen(text)&&!memcmp(bytes,text,length));
}
static void test_config_location(const wchar_t *parent){
    wchar_t repository[MAX_PATH],config[MAX_PATH],marker[MAX_PATH],path[MAX_PATH],resolved[MAX_PATH],expected[MAX_PATH];
    swprintf(repository,MAX_PATH,L"%ls\\repo",parent);swprintf(config,MAX_PATH,L"%ls\\config",repository);
    assert(CreateDirectoryW(repository,NULL)&&CreateDirectoryW(config,NULL));swprintf(marker,MAX_PATH,L"%ls\\.git",repository);write_text_file(marker,"fixture");
    swprintf(path,MAX_PATH,L"%ls\\nova.json",config);write_text_file(path,"fixture");
    swprintf(path,MAX_PATH,L"%ls\\build\\packages\\archive\\nova-desktop.exe",repository);
    assert(resolve_config_directory(path,resolved)&&!wcscmp(resolved,config));
    swprintf(path,MAX_PATH,L"%ls\\portable\\nova-desktop.exe",parent);swprintf(expected,MAX_PATH,L"%ls\\portable\\config",parent);
    assert(resolve_config_directory(path,resolved)&&!wcscmp(resolved,expected));
    swprintf(path,MAX_PATH,L"%ls\\nova.json",config);DeleteFileW(path);DeleteFileW(marker);assert(RemoveDirectoryW(config)&&RemoveDirectoryW(repository));
    puts("PASS repository executables share root config; standalone packages resolve adjacent config independently of cwd");
}
static void test_usage_data(const wchar_t *parent){
    wchar_t directory[MAX_PATH],path[MAX_PATH],second[MAX_PATH];swprintf(directory,MAX_PATH,L"%ls\\usage",parent);swprintf(second,MAX_PATH,L"%ls\\second",directory);assert(CreateDirectoryW(directory,NULL));
    assert(store_open(directory));
    BatchTaskList *tasks=calloc(1,sizeof(*tasks)),*restored=calloc(1,sizeof(*restored));assert(tasks&&restored);
    tasks->count=1;lstrcpyW(tasks->tasks[0].name,L"任务使用数据");lstrcpyW(tasks->tasks[0].command,L"echo fixture");tasks->tasks[0].mode=BATCH_PARALLEL;
    assert(batch_task_add_directory(&tasks->tasks[0],directory)==1&&batch_task_add_directory(&tasks->tasks[0],second)==1);
    lstrcpyW(tasks->tasks[0].commands[0],L"echo first");lstrcpyW(tasks->tasks[0].commands[1],L"echo second");assert(store_save_batch_tasks(tasks));long long task_id=tasks->tasks[0].id;
    Workspace *values=calloc(MAX_WORKSPACES,sizeof(*values));assert(values);lstrcpyW(values[0].name,L"目录");lstrcpyW(values[1].name,L"使用数据");values[0].items_loaded=values[1].items_loaded=1;values[1].app_count=1;
    lstrcpyW(values[1].apps[0].name,L"任务入口");swprintf(values[1].apps[0].target,NOVA_PATH_CAP,L"nova-batch:%lld",task_id);assert(store_save_workspaces(values,2,1,FALSE));
    FileCommandList presets;file_command_defaults(&presets);assert(file_command_add(&presets,L"检查",L"echo checked")==1);presets.default_index=1;assert(store_save_file_commands(&presets));
    assert(store_begin());assert(store_set_int(L"files",L"Manager",L"Migrated",1)&&store_set_int(L"files",L"Manager",L"FavoriteCount",1)&&store_set(L"files",L"Favorites",L"Item0",directory));
    assert(store_set_int(L"files",L"Pane0",L"Count",2)&&store_set_int(L"files",L"Pane0",L"Selected",1)&&store_set(L"files",L"Pane0",L"Tab0",directory)&&store_set(L"files",L"Pane0",L"Tab1",second));assert(store_end(TRUE));
    store_close();assert(store_open(directory));assert(store_load_batch_tasks(restored)&&restored->count==1&&restored->tasks[0].id==task_id&&restored->tasks[0].mode==BATCH_PARALLEL);
    assert(!wcscmp(restored->tasks[0].commands[0],L"echo first")&&!wcscmp(restored->tasks[0].commands[1],L"echo second"));assert(store_load_workspaces(values)==2&&store_load_items(&values[1]));
    assert(batch_task_target_id(values[1].apps[0].target)==task_id&&store_load_file_commands(&presets)&&presets.default_index==1);
    wchar_t location[MAX_PATH];assert(store_get(L"files",L"Pane0",L"Tab1",L"",location,MAX_PATH)&&!wcscmp(location,second));assert(store_int(L"files",L"Pane0",L"Selected",0)==1&&store_int(L"files",L"Manager",L"FavoriteCount",0)==1);
    swprintf(path,MAX_PATH,L"%ls\\nova.sqlite",directory);assert(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);store_close();swprintf(path,MAX_PATH,L"%ls\\nova.json",directory);DeleteFileW(path);assert(RemoveDirectoryW(directory));free(values);free(tasks);free(restored);
    puts("PASS tasks, independent subtask commands, shortcuts, presets, tabs and favorites reload from config JSON without a disk database");
}
static void test_json_migration(const wchar_t *parent){
    wchar_t legacy[MAX_PATH],target[MAX_PATH],database[MAX_PATH],json_path[MAX_PATH];
    swprintf(legacy,MAX_PATH,L"%ls\\legacy",parent);swprintf(target,MAX_PATH,L"%ls\\config",parent);assert(CreateDirectoryW(legacy,NULL)&&CreateDirectoryW(target,NULL));
    swprintf(database,MAX_PATH,L"%ls\\nova.sqlite",legacy);swprintf(json_path,MAX_PATH,L"%ls\\nova.json",target);
    sqlite3 *source=NULL;assert(sqlite3_open16(database,&source)==SQLITE_OK);
    assert(sqlite3_exec(source,"CREATE TABLE settings(scope TEXT,section TEXT,key TEXT,value TEXT,PRIMARY KEY(scope,section,key));"
        "CREATE TABLE workspaces(id INTEGER PRIMARY KEY,position INTEGER,name TEXT);"
        "CREATE TABLE items(id INTEGER PRIMARY KEY,workspace_id INTEGER,position INTEGER,name TEXT,target TEXT);"
        "INSERT INTO workspaces VALUES(9223372036854775807,0,'Files');"
        "INSERT INTO items VALUES(9223372036854775806,9223372036854775807,0,'quote \" and slash','C:\\fixture');"
        "INSERT INTO settings VALUES('app','Nova','Language','1');PRAGMA application_id=1313822273;PRAGMA user_version=1",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_close(source);BYTE *before=malloc(1024*1024),*after=malloc(1024*1024);assert(before&&after);DWORD length=read_file(database,before,1024*1024);
    write_text_file(json_path,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":false,\"settings\":[],\"workspaces\":[],\"items\":[]}");
    BOOL migrated=store_open_from(target,legacy);if(!migrated)fwprintf(stderr,L"Migration failed: %ls\n",store_error());assert(migrated);Workspace values[MAX_WORKSPACES];assert(store_load_workspaces(values)==1&&values[0].id==9223372036854775807LL);
    assert(store_load_items(&values[0])&&values[0].apps[0].id==9223372036854775806LL&&store_int(L"app",L"Nova",L"Language",0)==1);
    assert(read_file(database,after,1024*1024)==length&&!memcmp(before,after,length));
    assert(store_set(L"app",L"Unicode",L"Escapes",L"中文\n\"quoted\"\\path"));store_close();
    assert(store_open_from(target,legacy));wchar_t restored[80];assert(store_get(L"app",L"Unicode",L"Escapes",L"",restored,80)&&!wcscmp(restored,L"中文\n\"quoted\"\\path"));store_close();
    assert(sqlite3_open16(database,&source)==SQLITE_OK);assert(sqlite3_exec(source,"PRAGMA user_version=99",NULL,NULL,NULL)==SQLITE_OK);sqlite3_close(source);
    assert(store_open_from(target,legacy));store_close(); /* JSON wins after the one-time migration. */
    DeleteFileW(json_path);length=read_file(database,before,1024*1024);assert(!store_open_from(target,legacy));
    assert(read_file(database,after,1024*1024)==length&&!memcmp(before,after,length));free(before);free(after);
    DeleteFileW(database);assert(RemoveDirectoryW(legacy)&&RemoveDirectoryW(target));
    puts("PASS read-only SQLite migration, exact 64-bit IDs as strings, Unicode/escape round-trip and one-time JSON precedence");
}
int wmain(void){
    test_mode=TRUE;setvbuf(stdout,NULL,_IONBF,0);
    wchar_t temp[MAX_PATH],directory[MAX_PATH],database[MAX_PATH],backup[MAX_PATH];GetTempPathW(MAX_PATH,temp);
    swprintf(directory,MAX_PATH,L"%lsNovaStorage-%lu",temp,GetCurrentProcessId());assert(CreateDirectoryW(directory,NULL));
    swprintf(config_path,MAX_PATH,L"%ls\\config.ini",directory);swprintf(database,MAX_PATH,L"%ls\\nova.json",directory);swprintf(backup,MAX_PATH,L"%ls\\nova.backup.json",directory);
    HANDLE f=CreateFileW(config_path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(f!=INVALID_HANDLE_VALUE);WORD bom=0xfeff;DWORD bytes;WriteFile(f,&bom,2,&bytes,NULL);CloseHandle(f);
#define LEGACY(section,key,value) assert(WritePrivateProfileStringW(section,key,value,config_path))
    LEGACY(L"Nova",L"WorkspaceCount",L"2");LEGACY(L"Nova",L"Active",L"1");LEGACY(L"Nova",L"AlwaysOnTop",L"1");
    LEGACY(L"Workspace0",L"Name",L"开发 中文");LEGACY(L"Workspace0",L"AppCount",L"1");LEGACY(L"Workspace0",L"App0Name",L"代码编辑器");LEGACY(L"Workspace0",L"App0Target",L"C:\\中文路径\\editor.exe");
    LEGACY(L"Workspace1",L"Name",L"工作 文档");LEGACY(L"Workspace1",L"AppCount",L"1");LEGACY(L"Workspace1",L"App0Name",L"报告");LEGACY(L"Workspace1",L"App0Target",L"D:\\报告.docx");
    BYTE before[4096],after[4096];DWORD original=read_file(config_path,before,sizeof(before));
    ULONGLONG start=GetTickCount64();load_config();assert(!storage_failed&&store_ready());
    assert(space_count==2&&active_space==1&&prefer_pin);assert(!spaces[0].items_loaded&&spaces[1].items_loaded);
    assert(!wcscmp(spaces[0].name,L"目录"));
    assert(!wcscmp(spaces[1].apps[0].name,L"报告"));assert(read_file(config_path,after,sizeof(after))==original&&!memcmp(before,after,original));
    printf("PASS migration, unchanged UTF-16 INI, lazy inactive workspace; migration %lu ms\n",(unsigned long)(GetTickCount64()-start));
    long long workspace_id=spaces[0].id,item_id=spaces[1].apps[0].id;
    lstrcpyW(spaces[0].name,L"开发 已重命名");assert(save_config());assert(ensure_items(0));assert(spaces[0].app_count==1&&!wcscmp(spaces[0].apps[0].name,L"代码编辑器"));
    assert(workspace_move(spaces,space_count,1,0,0,-1)==1);assert(save_config());load_config();assert(!storage_failed);assert(ensure_items(0));
    assert(spaces[0].id==workspace_id&&spaces[0].apps[1].id==item_id&&spaces[0].app_count==2&&spaces[1].app_count==0);
    assert(store_begin());assert(store_set_int(L"app",L"Nova",L"Active",99));assert(!store_set(L"app",L"Bad",L"Null",NULL));assert(!store_end(TRUE));assert(store_int(L"app",L"Nova",L"Active",-1)==1);
    BatchTask batch={0},loaded_batch={0};assert(batch_task_add_directory(&batch,L"C:\\projects\\one")==1);assert(batch_task_add_directory(&batch,L"D:\\projects\\two")==1);
    assert(store_save_batch_task(&batch));assert(store_load_batch_task(&loaded_batch));assert(loaded_batch.directory_count==2&&!wcscmp(loaded_batch.directories[0],L"C:\\projects\\one")&&!wcscmp(loaded_batch.directories[1],L"D:\\projects\\two"));
    puts("PASS bounded batch-task folders persist transactionally without executing commands");
    lstrcpyW(batch.command,L"npm run build && echo 完成");assert(store_save_batch_task(&batch));assert(store_load_batch_task(&loaded_batch));assert(!wcscmp(batch.command,loaded_batch.command));
    puts("PASS custom Unicode command persistence");
    BatchTaskList *collection=calloc(1,sizeof(*collection)),*restored=calloc(1,sizeof(*restored));assert(collection&&restored);
    assert(store_load_batch_tasks(collection));assert(collection->count==1&&!wcscmp(collection->tasks[0].command,batch.command));assert(collection->tasks[0].directory_count==2&&collection->tasks[0].mode==BATCH_SEQUENTIAL);
    collection->count=2;collection->tasks[1]=collection->tasks[0];collection->tasks[1].id=0;lstrcpyW(collection->tasks[1].name,L"构建项目");lstrcpyW(collection->tasks[1].command,L"npm test");collection->tasks[1].mode=BATCH_PARALLEL;
    assert(batch_task_remove_directory(&collection->tasks[1],0));assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored));
    assert(restored->count==2&&restored->tasks[1].directory_count==1&&restored->tasks[1].mode==BATCH_PARALLEL&&!wcscmp(restored->tasks[1].command,L"npm test"));
    assert(restored->tasks[0].id>0&&restored->tasks[1].id>0&&restored->tasks[0].id!=restored->tasks[1].id);
    long long retained_id=restored->tasks[1].id;BatchTask first=collection->tasks[0];collection->tasks[0]=collection->tasks[1];collection->tasks[1]=first;
    lstrcpyW(collection->tasks[0].name,L"构建项目 已改名");assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored));assert(restored->tasks[0].id==retained_id);
    assert(store_set_int(L"batch_tasks",L"Collection",L"Version",1));assert(store_set(L"batch_tasks",L"Task0",L"Id",L""));assert(store_load_batch_tasks(restored));assert(restored->tasks[0].id>0&&store_int(L"batch_tasks",L"Collection",L"Version",0)==3);
    *collection=*restored;retained_id=collection->tasks[0].id;
    collection->count=1;assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored)&&restored->count==1);
    assert(restored->tasks[0].id==retained_id);
    assert(store_set_int(L"batch_tasks",L"Collection",L"Version",2));assert(store_load_batch_tasks(restored));
    assert(restored->tasks[0].id==retained_id&&!wcscmp(batch_task_command(&restored->tasks[0],0),restored->tasks[0].command));
    lstrcpyW(collection->tasks[0].commands[0],L"echo 独立命令");assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored));
    assert(!wcscmp(restored->tasks[0].commands[0],L"echo 独立命令"));
    assert(store_set(L"batch_tasks",L"Task0",L"StepCommand0",L" \t"));assert(!store_load_batch_tasks(restored));assert(!wcscmp(restored->tasks[0].commands[0],L"echo 独立命令"));assert(store_save_batch_tasks(collection));
    assert(store_set_int(L"batch_tasks",L"Collection",L"Version",4));assert(!store_load_batch_tasks(restored));assert(store_int(L"batch_tasks",L"Collection",L"Version",0)==4);assert(store_save_batch_tasks(collection));
    assert(store_set(L"batch_tasks",L"Task0",L"Id",L"9223372036854775808"));assert(!store_load_batch_tasks(restored));assert(store_save_batch_tasks(collection));
    puts("PASS stable task IDs across rename/reorder/delete, v1 collection migration and malformed-ID refusal");
    assert(store_set_int(L"batch_tasks",L"Collection",L"Count",BATCH_TASK_LIMIT+1));assert(!store_load_batch_tasks(restored));assert(store_int(L"batch_tasks",L"Collection",L"Count",0)==BATCH_TASK_LIMIT+1);
    assert(store_save_batch_tasks(collection));collection->count=0;assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored)&&restored->count==0);
    free(collection);free(restored);
    puts("PASS single-task migration, independent task commands/folders/modes, deletion, empty collection and over-capacity refusal");
    FileCommandList commands={0},restored_commands={0};assert(store_load_file_commands(&commands));assert(commands.count==1&&!wcscmp(commands.items[0].name,L"cmd.exe"));
    assert(file_command_add(&commands,L"检查状态",L"git status")==1);commands.default_index=1;assert(store_save_file_commands(&commands));assert(store_load_file_commands(&restored_commands));
    assert(restored_commands.count==2&&restored_commands.default_index==1&&!wcscmp(restored_commands.items[1].command,L"git status"));
    assert(store_set_int(L"file_commands",L"Commands",L"Version",2));assert(!store_load_file_commands(&restored_commands));assert(store_int(L"file_commands",L"Commands",L"Version",0)==2);assert(store_save_file_commands(&commands));
    puts("PASS bounded command presets and default selection persist transactionally; newer data is preserved");
    assert(store_backup());sqlite3 *reader=NULL;assert(sqlite3_open(":memory:",&reader)==SQLITE_OK);sqlite3_stmt *q=NULL;
    BYTE *json=calloc(1,1024*1024);assert(json);DWORD json_length=read_file(backup,json,1024*1024-1);
    assert(sqlite3_prepare_v2(reader,"SELECT json_array_length(?1,'$.items')",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_bind_text(q,1,(char*)json,json_length,SQLITE_STATIC)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==2);sqlite3_finalize(q);sqlite3_close(reader);
    HANDLE locked=CreateFileW(database,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(locked!=INVALID_HANDLE_VALUE);
    assert(!store_set_int(L"app",L"Nova",L"Active",7));assert(store_int(L"app",L"Nova",L"Active",-1)==1);CloseHandle(locked);
    DWORD saved_length=read_file(database,json,1024*1024-1);assert(saved_length==json_length);free(json);
    puts("PASS stable IDs, unloaded items, transaction rollback, atomic JSON backup and rollback when the destination is locked");
    /* A later launch must ignore stale INI data after successful migration. */
    LEGACY(L"Workspace0",L"Name",L"过期 INI");store_close();load_config();if(storage_failed)fwprintf(stderr,L"JSON reopen failed: %ls\n",store_error());assert(!storage_failed&&!wcscmp(spaces[0].name,L"目录"));
    store_close();test_config_location(directory);test_usage_data(directory);test_json_migration(directory);
    refused_json(directory,"{\"version\":99,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[],\"workspaces\":[],\"items\":[]}");
    refused_json(directory,"{\"version\":1,\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[],\"workspaces\":[],\"items\":[]}");
    refused_json(directory,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[],\"workspaces\":[{\"id\":\"9223372036854775808\",\"position\":0,\"name\":\"Files\"}],\"items\":[]}");
    refused_json(directory,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[{\"scope\":\"app\",\"section\":\"Nova\",\"key\":\"Language\",\"value\":\"a\\u0000b\"}],\"workspaces\":[],\"items\":[]}");
    refused_json(directory,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[],\"workspaces\":[],\"items\":[{\"id\":\"1\",\"workspace_id\":\"2\",\"position\":0,\"name\":\"missing workspace\",\"target\":\"C:\\\\fixture\"}]}");
    refused_json(directory,"{ invalid JSON }");
    refused_json(directory,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[{\"scope\":\"files\",\"section\":\"Pane0\",\"key\":\"Count\",\"value\":\"13\"}],\"workspaces\":[],\"items\":[]}");
    refused_json(directory,"{\"version\":1,\"application\":\"NOVA Desktop\",\"initialized\":true,\"settings\":[],\"workspaces\":[{\"id\":\"1\",\"position\":0,\"name\":\"\\ud800\"}],\"items\":[]}");
    puts("PASS malformed/newer JSON, duplicate keys, ID overflow, embedded NUL and orphan refusal without overwriting originals");
    DeleteFileW(database);DeleteFileW(backup);DeleteFileW(config_path);assert(RemoveDirectoryW(directory));return 0;
}
