#define _WIN32_WINNT 0x0601
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/ui/sync_manager.c"
#include "log_capture.h"
BOOL nova_english=FALSE;
/* Fixture callbacks ensure this executable never accesses real login records. */
BOOL ssh_choose_server(HWND h,int64_t id,SshLogin *login){(void)h;(void)id;(void)login;return FALSE;}
BOOL ssh_pick_server(HWND h,HWND anchor,SshLogin *login){(void)h;(void)anchor;(void)login;return FALSE;}
static BOOL fake_login(BOOL interactive,SshLogin *login){(void)interactive;ZeroMemory(login,sizeof(*login));login->server.id=1;login->server.port=22;wcscpy(login->server.name,L"Synthetic SSH");wcscpy(login->server.host,L"fixture.invalid");return TRUE;}
static BOOL fail_attach,legacy_server,rejected_server,fail_bind;
static int checks,push_jobs,pull_jobs,resolutions;
static BOOL fake_target_login(int64_t id,BOOL interactive,SshLogin *login){BOOL ok=fake_login(interactive,login);login->server.id=id;return ok;}
static BOOL fake_run(SyncRequest *request,int action,SyncResult *result){
    ZeroMemory(result,sizeof(*result));if(action==SYNC_JOB_SERVER_ATTACH&&fail_attach){result->failure=SYNC_CONFLICT;return FALSE;}if(WaitForSingleObject(request->cancel,0)==WAIT_OBJECT_0){result->failure=SYNC_CANCELLED;return FALSE;}
    if(action==SYNC_JOB_BIND&&fail_bind){result->failure=SYNC_NETWORK;wcscpy(result->detail,L"Synthetic bind failure");return FALSE;}
    if(legacy_server&&(action==SYNC_JOB_PUSH||action==SYNC_JOB_CHECK)){result->failure=SYNC_UPGRADE;wcscpy(result->detail,L"Legacy broker request detail");return FALSE;}
    if(rejected_server&&action==SYNC_JOB_PUSH&&!wcscmp(request->pair.id,L"fixture-pair")){result->failure=SYNC_REMOTE;wcscpy(result->detail,L"Specific server rejection");return FALSE;}
    result->cloud=strdup("{\"devices\":[{\"role\":\"A\",\"node_id\":\"synthetic-node\",\"name\":\"Office laptop\"},{\"role\":\"B\",\"node_id\":\"peer-b\",\"name\":\"Home desktop\"},{\"role\":\"C\",\"node_id\":\"peer-c\",\"name\":\"Studio PC\"}],\"datasets\":[{\"id\":\"fixture-data\",\"name\":\"Fixture group\",\"publisher\":\"A\"}],\"bindings\":[{\"dataset\":\"fixture-data\",\"device\":\"A\",\"path\":\"C:\\\\synthetic-local\",\"pending\":0,\"waiting\":0},{\"dataset\":\"fixture-data\",\"device\":\"B\",\"path\":\"C:\\\\synthetic-peer\",\"pending\":2,\"waiting\":1}],\"stored_bytes\":0,\"quota_bytes\":16106127360,\"jobs\":{}}");
    if(request->settings.count){sqlite3 *db=NULL;assert(sqlite3_open(":memory:",&db)==SQLITE_OK);char *path=sync_utf8(request->settings.pairs[0].directory);assert(path);char *cloud=sync_json_patch(db,result->cloud,"$.bindings[0].path",path,FALSE);assert(cloud);free(result->cloud);result->cloud=cloud;free(path);sqlite3_close(db);}
    if(action==SYNC_JOB_SERVER_PROBE){free(result->cloud);result->cloud=strdup("{\"devices\":[],\"datasets\":[{\"id\":\"unrelated-target\",\"name\":\"Destination only\"}],\"bindings\":[],\"jobs\":{},\"stored_bytes\":0,\"quota_bytes\":1073741824,\"python\":\"3.12.0\",\"free_bytes\":10737418240,\"control\":{\"phase\":\"ACTIVE\"}}");}
    if(action>=SYNC_JOB_SERVER_PROBE){result->installed=TRUE;result->switched=action==SYNC_JOB_SERVER_MIGRATE||action==SYNC_JOB_SERVER_ATTACH||action==SYNC_JOB_SERVER_DEPLOY;wcscpy(result->server_instance,L"fixture-new-server");wcscpy(result->server_phase,L"ACTIVE");}
    if(action==SYNC_JOB_CHECK){checks++;result->changes=strdup("[{\"path\":\"report.docx\",\"state\":\"conflict\",\"local\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"size\":24064,\"mtime\":1792893600000000000,\"remote_size\":22016,\"remote_mtime\":1792890000000000000,\"local_version\":\"version-local\",\"remote_version\":\"version-server\"},{\"path\":\"notes.txt\",\"state\":\"push\",\"size\":4096,\"mtime\":1792893600000000000,\"local\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"local_version\":\"\",\"remote_version\":\"\"}]");result->conflicts=1;result->pushes=1;}
    if(action==SYNC_JOB_PUSH)push_jobs++;
    if(action==SYNC_JOB_PULL)pull_jobs++;
    if(action==SYNC_JOB_RUN||action==SYNC_JOB_PUSH||action==SYNC_JOB_PULL||action==SYNC_JOB_RESOLVE)result->changes=strdup("[]");
    if(action==SYNC_JOB_RESOLVE){resolutions++;assert(request->resolution==1&&!wcscmp(request->resolve_path,L"report.docx")&&!strcmp(request->expected_remote,"version-server"));}
    result->stats.sent=1024;result->stats.reused=4096;return result->cloud!=NULL;
}
static void pump(void){MSG m;while(PeekMessageW(&m,NULL,0,0,PM_REMOVE)){if(!IsDialogMessageW(sync_manager_window(),&m)){TranslateMessage(&m);DispatchMessageW(&m);}}}
static void finish(void){ULONGLONG deadline=GetTickCount64()+5000;while(job&&GetTickCount64()<deadline){pump();Sleep(5);}assert(!job);}
static void capture(HWND h,const wchar_t *file){
    RECT r;GetWindowRect(h,&r);int w=r.right-r.left,height=r.bottom-r.top;HDC dc=GetDC(h),memory=CreateCompatibleDC(dc);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,NULL,0);assert(bitmap);HGDIOBJ old=SelectObject(memory,bitmap);RedrawWindow(h,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);assert(PrintWindow(h,memory,0));BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+w*height*4;HANDLE output=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD n;assert(output!=INVALID_HANDLE_VALUE);assert(WriteFile(output,&header,sizeof(header),&n,NULL)&&WriteFile(output,&info.bmiHeader,sizeof(info.bmiHeader),&n,NULL)&&WriteFile(output,pixels,w*height*4,&n,NULL));CloseHandle(output);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(h,dc);
}
int wmain(int argc,wchar_t **argv){
    SetProcessDPIAware();assert(SUCCEEDED(OleInitialize(NULL)));wchar_t base[MAX_PATH],directory_path[MAX_PATH],work[MAX_PATH];assert(GetTempPathW(MAX_PATH,base));assert(GetTempFileNameW(base,L"nsu",0,directory_path));assert(DeleteFileW(directory_path)&&CreateDirectoryW(directory_path,NULL));assert(nova_log_init(directory_path));assert(store_open(directory_path));swprintf(work,MAX_PATH,L"%ls\\synthetic-work",directory_path);assert(CreateDirectoryW(work,NULL));
    SyncSettings *initial=calloc(1,sizeof(*initial));assert(initial);sync_settings_default(initial);initial->server_id=1;wcscpy(initial->node_id,L"synthetic-node");wcscpy(initial->name,L"User device name");initial->count=1;SyncPair *p=&initial->pairs[0];wcscpy(p->id,L"fixture-pair");wcscpy(p->dataset,L"fixture-data");wcscpy(p->name,L"User folder name");wcscpy(p->group,L"Fixture group");lstrcpyW(p->directory,work);wcscpy(p->excludes,L".git;node_modules");p->paused=true;assert(sync_settings_save(initial));free(initial);
    SshServers fixture_servers={0};fixture_servers.count=3;for(int i=0;i<3;i++){SshServer *server=&fixture_servers.items[i];server->id=i+1;server->port=22;swprintf(server->name,SSH_NAME_CAP,L"Synthetic server %d",i+1);swprintf(server->host,256,L"server%d.example.invalid",i+1);wcscpy(server->user,L"fixture");wcscpy(server->directory,L"/");}assert(store_save_ssh_servers(&fixture_servers));
    login_backend=fake_login;target_login_backend=fake_target_login;run_backend=fake_run;HWND parent=CreateWindowExW(0,L"STATIC",L"NOVA sync fixture",WS_OVERLAPPEDWINDOW,0,0,400,300,NULL,NULL,GetModuleHandleW(NULL),NULL);assert(parent&&sync_manager_open(parent));pump();
    assert(start_job(SYNC_JOB_REGISTER,FALSE));finish();assert(settings.registered&&cloud_view&&group_count==1);assert(store_int(L"sync",L"Manager",L"Registered",0)==1);
    wchar_t publisher[192];ListView_GetItemText(pairs,0,2,publisher,192);assert(!wcscmp(publisher,L"Office laptop"));
    assert(!wcscmp(publisher_name(1),L"Home desktop"));
    cloud_view->present[0]=FALSE;assert(!wcscmp(publisher_name(0),settings.name));cloud_view->present[0]=TRUE;
    nova_english=TRUE;assert(!wcscmp(publisher_name(-1),L"Publisher unknown"));
    int old_publisher=groups[0].publisher;groups[0].publisher=-1;populate_pairs();ListView_GetItemText(pairs,0,2,publisher,192);assert(!wcscmp(publisher,L"Publisher unknown"));groups[0].publisher=old_publisher;populate_pairs();
    nova_english=FALSE;assert(!wcscmp(publisher_name(-1),L"发布设备未知"));
    puts("PASS publication labels show registered local/peer names and distinguish missing publisher metadata");
    int original_groups=group_count;assert(!load_cloud("{\"devices\":[],\"datasets\":{},\"bindings\":[]}"));assert(group_count==original_groups);
    int original_bindings=binding_count;assert(!load_cloud("{\"devices\":[],\"datasets\":[],\"bindings\":[{\"dataset\":\"fixture-data\",\"device\":\"B\",\"path\":\"C:\\\\fixture\",\"pending\":1,\"waiting\":2}]}"));assert(binding_count==original_bindings&&group_count==original_groups);
    SetWindowTextW(name_edit,L"Unsaved user name");nova_english=TRUE;sync_manager_language_changed();wchar_t value[256];GetWindowTextW(window,value,256);assert(wcsstr(value,L"Devices and synchronization"));GetWindowTextW(name_edit,value,256);assert(!wcscmp(value,L"Unsaved user name"));GetWindowTextW(pair_name,value,256);assert(!wcscmp(value,L"User folder name"));
    page=1;TabCtrl_SetCurSel(tab,1);layout();select_pair(0);SetWindowTextW(pair_name,L"Saved user pair");save_pair();finish();assert(!wcscmp(settings.pairs[0].name,L"Saved user pair")&&settings.pairs[0].paused);
    int previous_history=ListView_GetItemCount(history);assert(start_job(SYNC_JOB_RUN,FALSE));finish();assert(ListView_GetItemCount(history)==previous_history+1);assert(settings.count==1);assert(GetFileAttributesW(work)&FILE_ATTRIBUTE_DIRECTORY);
    int old_count=settings.count;new_pair();assert(settings.count==old_count);GetWindowTextW(path_edit,value,256);assert(!value[0]);assert(!IsWindowEnabled(GetDlgItem(window,C_RUN)));
    wchar_t another[MAX_PATH];swprintf(another,MAX_PATH,L"%ls\\synthetic-second",directory_path);assert(CreateDirectoryW(another,NULL));SetWindowTextW(path_edit,another);SetWindowTextW(pair_name,L"Second user pair");save_pair();finish();assert(settings.count==old_count+1&&settings.pairs[old_count].paused&&!wcscmp(settings.pairs[old_count].directory,another));select_pair(0);
    /* Choosing a candidate alone never changes the active server or identity. */
    int64_t original_server=settings.server_id;candidate_id=2;page=3;TabCtrl_SetCurSel(tab,3);layout();
    SetWindowTextW(target_project_edit,L"/fixture-new-server");assert(settings.server_id==original_server);
    GetWindowTextW(target_root_edit,value,256);assert(!wcscmp(value,L"/fixture-new-server/runtime/broker"));
    assert(start_job(SYNC_JOB_SERVER_PROBE,FALSE));finish();assert(settings.server_id==original_server&&group_count==1&&!wcscmp(groups[0].id,L"fixture-data"));
    assert(start_job(SYNC_JOB_SERVER_MIGRATE,FALSE));finish();assert(settings.server_id==2&&!wcscmp(settings.node_id,L"synthetic-node")&&settings.count==2&&settings.pairs[0].paused&&!wcscmp(settings.pairs[0].dataset,L"fixture-data"));
    candidate_id=3;fail_attach=TRUE;assert(start_job(SYNC_JOB_SERVER_ATTACH,FALSE));finish();assert(settings.server_id==2&&!wcscmp(settings.server_instance,L"fixture-new-server"));fail_attach=FALSE;candidate_id=2;
    SetWindowTextW(target_project_edit,L"/unsaved-destination");nova_english=FALSE;sync_manager_language_changed();GetWindowTextW(target_project_edit,value,256);assert(!wcscmp(value,L"/unsaved-destination"));
    puts("PASS native server UI: candidate isolation, probe, retained identity/pairs on migration, rejected switch preservation and localized unsaved fields");
    page=1;TabCtrl_SetCurSel(tab,1);editing=FALSE;layout();assert(!IsWindowVisible(role_combo)&&!IsWindowVisible(filter_edit)&&!IsWindowVisible(target_project_edit));
    int previous_checks=checks;start_batch(SYNC_JOB_CHECK);finish();assert(checks==previous_checks+settings.count&&inspected[0]&&inspected[1]&&pending_conflict[0]==1);
    folder_checked[0]=TRUE;folder_checked[1]=FALSE;int previous_push=push_jobs;start_batch(SYNC_JOB_PUSH);finish();assert(push_jobs==previous_push+1);
    int previous_pull=pull_jobs;start_batch(SYNC_JOB_PULL);finish();assert(pull_jobs==previous_pull+1);
    select_pair(0);assert(start_job(SYNC_JOB_CHECK,FALSE));finish();ListView_SetItemState(changes,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);resolve_choice=1;assert(start_job(SYNC_JOB_RESOLVE,FALSE));finish();assert(resolutions==1);
    assert(ListView_GetItemCount(changes)==0&&inspected[0]&&pending_conflict[0]==0);nova_english=TRUE;sync_manager_language_changed();assert(ListView_GetItemCount(changes)==0);
    puts("PASS native workflow UI: hidden technical fields, all-folder inspection, checked-folder push/pull, version choice wiring and translated file states");
    legacy_server=TRUE;start_batch(SYNC_JOB_PUSH);finish();assert(folder_codes[0]==SYNC_UPGRADE&&batch_failures==1&&!batch_action);
    ListView_GetItemText(pairs,0,3,value,256);assert(wcsstr(value,L"Server update required"));assert(wcsstr(summary,L"Configure server")&&wcsstr(summary,L"Legacy broker request detail"));
    assert(history_codes[0]==SYNC_UPGRADE&&!wcscmp(history_details[0],L"Legacy broker request detail"));
    ListView_SetItemState(history,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);assert(wcsstr(summary,L"Legacy broker request detail"));
    nova_english=FALSE;sync_manager_language_changed();ListView_GetItemText(pairs,0,3,value,256);assert(wcsstr(value,L"服务器版本较旧"));
    legacy_server=FALSE;start_batch(SYNC_JOB_PUSH);finish();assert(folder_codes[0]==SYNC_OK&&batch_failures==0);
    puts("PASS outdated-server guidance, persistent folder error, detailed transfer history, localization and successful retry reset");
    rejected_server=TRUE;folder_checked[1]=TRUE;start_batch(SYNC_JOB_PUSH);finish();
    assert(batch_failures==1&&folder_codes[0]==SYNC_REMOTE&&folder_codes[1]==SYNC_OK&&wcsstr(summary,L"Specific server rejection"));
    assert(start_job(SYNC_JOB_REFRESH,FALSE));finish();assert(folder_codes[0]==SYNC_REMOTE);
    rejected_server=FALSE;start_batch(SYNC_JOB_PUSH);finish();assert(batch_failures==0&&folder_codes[0]==SYNC_OK);folder_checked[1]=FALSE;
    puts("PASS batch completion preserves first rejection detail across later successes and metadata refresh");
    select_pair(0);render_folder_detail();assert(ListView_GetItemCount(participants)==2);
    ListView_GetItemText(participants,1,2,value,256);assert(wcsstr(value,L"2")&&wcsstr(value,L"1"));
    assert(folder_state(0).relation==SYNC_REL_PUBLISHER);
    wchar_t matched_path[SYNC_PATH_CAP];lstrcpyW(matched_path,bindings[0].path);wcscpy(bindings[0].path,L"C:\\old-binding");controls();assert(folder_state(0).relation==SYNC_REL_LOCAL&&IsWindowEnabled(GetDlgItem(window,C_BIND)));lstrcpyW(bindings[0].path,matched_path);controls();assert(!IsWindowEnabled(GetDlgItem(window,C_BIND)));
    folder_checked[0]=folder_checked[1]=FALSE;controls();assert(!IsWindowEnabled(GetDlgItem(window,C_PUSH_ALL))&&!IsWindowEnabled(GetDlgItem(window,C_SYNC_ALL)));
    folder_checked[0]=TRUE;controls();assert(IsWindowEnabled(GetDlgItem(window,C_PUSH_ALL)));
    /* Selection and checkboxes have separate purposes; jobs retain the inspected folder. */
    select_pair(1);start_batch(SYNC_JOB_CHECK);finish();assert(selected==1);
    select_pair(0);ListView_SetItemState(changes,1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);controls();assert(!IsWindowEnabled(GetDlgItem(window,C_LOCAL)));
    ListView_SetItemState(changes,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);controls();assert(IsWindowEnabled(GetDlgItem(window,C_LOCAL)));
    /* Read-only inspection errors must not change scheduling preferences. */
    settings.pairs[0].paused=false;legacy_server=TRUE;assert(start_job(SYNC_JOB_CHECK,FALSE));finish();assert(!settings.pairs[0].paused);legacy_server=FALSE;settings.pairs[0].paused=true;
    fail_bind=TRUE;assert(start_job(SYNC_JOB_BIND,FALSE));finish();assert(folder_codes[0]==SYNC_NETWORK&&IsWindowEnabled(GetDlgItem(window,C_BIND))&&wcsstr(summary,L"重试接入"));
    fail_bind=FALSE;assert(start_job(SYNC_JOB_BIND,FALSE));finish();assert(folder_codes[0]==SYNC_OK);
    new_pair();SetWindowTextW(pair_name,L"Discarded draft");int draft_count=settings.count;SendMessageW(window,WM_COMMAND,C_CANCEL_EDIT,0);assert(!editing&&settings.count==draft_count&&new_index==-1);
    SendMessageW(window,WM_COMMAND,C_CATALOG,0);assert(catalog_mode&&ListView_GetItemCount(pairs)==0&&!(ListView_GetExtendedListViewStyle(pairs)&LVS_EX_CHECKBOXES));
    SendMessageW(window,WM_COMMAND,C_MY_FOLDERS,0);assert(!catalog_mode&&ListView_GetItemCount(pairs)==settings.count);
    assert(load_cloud("{\"devices\":[{\"role\":\"B\",\"name\":\"Home desktop\",\"node_id\":\"peer-b\"}],\"datasets\":[{\"id\":\"fixture-data\",\"name\":\"Fixture group\",\"publisher\":\"A\"},{\"id\":\"remote-data\",\"name\":\"Shared project\",\"publisher\":\"B\"}],\"bindings\":[{\"dataset\":\"remote-data\",\"device\":\"B\",\"path\":\"C:\\\\Projects\\\\Shared\",\"pending\":0,\"waiting\":0}]}"));
    SendMessageW(window,WM_COMMAND,C_CATALOG,0);assert(ListView_GetItemCount(pairs)==1&&catalog_choice==1&&IsWindowEnabled(GetDlgItem(window,C_SUBSCRIBE))&&ListView_GetItemCount(participants)==1);
    if(argc>1&&!wcscmp(argv[1],L"--capture")){nova_english=TRUE;sync_manager_language_changed();capture(window,L"build\\sync-catalog-en.bmp");}
    int old_transfers=push_jobs+pull_jobs;SendMessageW(window,WM_COMMAND,C_SUBSCRIBE,0);assert(editing&&subscribing&&!catalog_mode&&settings.count==draft_count&&!wcscmp(draft_pair.dataset,L"remote-data"));
    SetWindowTextW(pair_name,L"Local alias");sync_manager_language_changed();GetWindowTextW(pair_name,value,256);assert(!wcscmp(value,L"Local alias")&&!wcscmp(draft_pair.group,L"Shared project"));
    if(argc>1&&!wcscmp(argv[1],L"--capture"))capture(window,L"build\\sync-subscribe-en.bmp");
    SendMessageW(window,WM_COMMAND,C_CANCEL_EDIT,0);assert(settings.count==draft_count&&old_transfers==push_jobs+pull_jobs);
    select_pair(0);assert(start_job(SYNC_JOB_CHECK,FALSE));finish();
    puts("PASS separated participation/scheduling/comparison, device receipts, checked batch scope, stable inspection selection, conflict-only resolution, bind retry and draft cancellation");
    if(argc>1&&!wcscmp(argv[1],L"--capture")){
        nova_english=FALSE;sync_manager_language_changed();capture(window,L"build\\sync-native-zh.bmp");
        nova_english=TRUE;sync_manager_language_changed();capture(window,L"build\\sync-native-en.bmp");
        devices_mode=TRUE;render_folder_detail();layout();capture(window,L"build\\sync-participants-en.bmp");devices_mode=FALSE;layout();
        RECT original;GetWindowRect(window,&original);SetWindowPos(window,NULL,0,0,px(1000),px(740),SWP_NOMOVE|SWP_NOZORDER);capture(window,L"build\\sync-minimum-en.bmp");SetWindowPos(window,NULL,0,0,original.right-original.left,original.bottom-original.top,SWP_NOMOVE|SWP_NOZORDER);
        page=0;TabCtrl_SetCurSel(tab,0);layout();capture(window,L"build\\sync-devices-en.bmp");
        page=3;TabCtrl_SetCurSel(tab,3);layout();capture(window,L"build\\sync-servers-en.bmp");
        nova_english=FALSE;sync_manager_language_changed();capture(window,L"build\\sync-servers-zh.bmp");
        server_advanced=TRUE;layout();capture(window,L"build\\sync-server-advanced-zh.bmp");server_advanced=FALSE;
        new_pair();SetWindowTextW(path_edit,work);layout();capture(window,L"build\\sync-publish-zh.bmp");advanced=TRUE;layout();capture(window,L"build\\sync-folder-advanced-zh.bmp");
        assert(IsWindowVisible(GetDlgItem(window,C_CLOSE))&&IsWindowVisible(GetDlgItem(window,C_REFRESH)));
    }
    if(argc>1&&!wcscmp(argv[1],L"--show")){SetWindowTextW(window,L"NOVA 原生同步预览 · Synthetic data / no network");while(IsWindowVisible(window)){pump();Sleep(30);}}
    sync_manager_shutdown();DestroyWindow(parent);store_close();{const char *events[]={"component=sync_ui","event=worker_started","event=completion_consumed","event=request_result"};check_diagnostics(directory_path,events,4);}OleUninitialize();puts("PASS native sync UI: registration, folder pairing/save, language, preserved user names, bounded cloud validation, worker lifecycle (fake backend/temporary state only)");return 0;
}
