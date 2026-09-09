#define wWinMain nova_entry
#include "../src/main.c"
#undef wWinMain
#include "../third_party/sqlite/sqlite3.h"
#include <assert.h>

static DWORD read_file(const wchar_t *path,BYTE *bytes,DWORD capacity){
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(f!=INVALID_HANDLE_VALUE);
    DWORD n=0;assert(ReadFile(f,bytes,capacity,&n,NULL));CloseHandle(f);return n;
}
int wmain(void){
    test_mode=TRUE;setvbuf(stdout,NULL,_IONBF,0);
    wchar_t temp[MAX_PATH],directory[MAX_PATH],database[MAX_PATH],backup[MAX_PATH];GetTempPathW(MAX_PATH,temp);
    swprintf(directory,MAX_PATH,L"%lsNovaStorage-%lu",temp,GetCurrentProcessId());assert(CreateDirectoryW(directory,NULL));
    swprintf(config_path,MAX_PATH,L"%ls\\config.ini",directory);swprintf(database,MAX_PATH,L"%ls\\nova.sqlite",directory);swprintf(backup,MAX_PATH,L"%ls\\nova.backup.sqlite",directory);
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
    assert(store_set_int(L"batch_tasks",L"Collection",L"Version",1));assert(store_set(L"batch_tasks",L"Task0",L"Id",L""));assert(store_load_batch_tasks(restored));assert(restored->tasks[0].id>0&&store_int(L"batch_tasks",L"Collection",L"Version",0)==2);
    *collection=*restored;retained_id=collection->tasks[0].id;
    collection->count=1;assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored)&&restored->count==1);
    assert(restored->tasks[0].id==retained_id);
    assert(store_set(L"batch_tasks",L"Task0",L"Id",L"9223372036854775808"));assert(!store_load_batch_tasks(restored));assert(store_save_batch_tasks(collection));
    puts("PASS stable task IDs across rename/reorder/delete, v1 collection migration and malformed-ID refusal");
    assert(store_set_int(L"batch_tasks",L"Collection",L"Count",BATCH_TASK_LIMIT+1));assert(!store_load_batch_tasks(restored));assert(store_int(L"batch_tasks",L"Collection",L"Count",0)==BATCH_TASK_LIMIT+1);
    assert(store_save_batch_tasks(collection));collection->count=0;assert(store_save_batch_tasks(collection));assert(store_load_batch_tasks(restored)&&restored->count==0);
    free(collection);free(restored);
    puts("PASS single-task migration, independent task commands/folders/modes, deletion, empty collection and over-capacity refusal");
    assert(store_backup());sqlite3 *reader=NULL;assert(sqlite3_open16(backup,&reader)==SQLITE_OK);sqlite3_stmt *q=NULL;
    assert(sqlite3_prepare_v2(reader,"SELECT count(*) FROM items",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==2);sqlite3_finalize(q);sqlite3_close(reader);
    printf("PASS stable IDs, cross-workspace transaction, unloaded-item preservation, rollback and SQLite backup; SQLite heap %lu bytes\n",(unsigned long)sqlite3_memory_used());
    /* A later launch must ignore stale INI data after successful migration. */
    LEGACY(L"Workspace0",L"Name",L"过期 INI");store_close();load_config();assert(!storage_failed&&!wcscmp(spaces[0].name,L"目录"));
    store_close();assert(sqlite3_open16(database,&reader)==SQLITE_OK);assert(sqlite3_exec(reader,"PRAGMA user_version=99",NULL,NULL,NULL)==SQLITE_OK);sqlite3_close(reader);
    assert(!store_open(directory));assert(!store_ready());
    assert(sqlite3_open16(database,&reader)==SQLITE_OK);assert(sqlite3_prepare_v2(reader,"PRAGMA user_version",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)==99);sqlite3_finalize(q);sqlite3_close(reader);
    puts("PASS one-time migration and refusal to overwrite newer schema");
    DeleteFileW(database);DeleteFileW(backup);DeleteFileW(config_path);assert(RemoveDirectoryW(directory));return 0;
}
