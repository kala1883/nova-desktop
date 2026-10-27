#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wincred.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "sync_manager.h"
#include "ssh_view.h"
#include "../platform/sync_settings.h"
#include "../platform/sync_client.h"
#include "../platform/sync_json.h"
#include "../platform/storage.h"
#include "../platform/app_log.h"
#include "../platform/ssh_credentials.h"
#include "../../third_party/sqlite/sqlite3.h"
#include "../i18n.h"

#define SYNC_CLASS L"NovaSyncManager"
#define PROGRESS_MESSAGE (WM_APP+115)
#define DONE_MESSAGE (WM_APP+116)
#define RECYCLE_MESSAGE (WM_APP+117)
#define AUTO_TIMER 7351
enum { C_TAB=6100,C_SERVER,C_NAME,C_ROLE,C_REGISTER,C_REFRESH,C_IMPORT,C_SERVER_TEXT,C_DEVICES,
 C_PAIRS,C_NEW,C_REMOVE,C_PAIR_NAME,C_GROUP,C_PATH,C_BROWSE,C_FILTER,C_DELETE,C_SAVE,C_RUN,C_AUTO,C_PAUSE,C_PROGRESS,C_LOG,C_CLOSE,C_PROJECT,C_BROKER,C_CLOUD,
 C_TARGET,C_TARGET_PROJECT,C_TARGET_ROOT,C_QUOTA,C_RESERVE,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW,C_TARGET_TEXT,C_ADMIN_NOTE,
 C_CHECK_ALL,C_PUSH_ALL,C_PULL_ALL,C_SYNC_ALL,C_SUBSCRIBE,C_EDIT,C_ADVANCED,C_SERVER_ADVANCED,C_LOCAL,C_REMOTE,C_CHANGES,C_EMPTY,C_HEADING,C_RECOVERY,
 C_MY_FOLDERS,C_CATALOG,C_DETAIL,C_FILES_VIEW,C_DEVICES_VIEW,C_PARTICIPANTS,C_CANCEL_EDIT,C_BIND,C_SCOPE,C_DETAIL_EMPTY };
typedef struct CloudGroup { wchar_t id[SYNC_ID_CAP],name[SYNC_NAME_CAP];
    int publisher;
    } CloudGroup;
typedef struct CloudBinding { wchar_t dataset[SYNC_ID_CAP],path[SYNC_PATH_CAP];
    int role,pending,waiting;
    } CloudBinding;
typedef struct SyncJob {
    SyncRequest request;
    SyncResult result;NovaLogSpan log;
    HANDLE worker,cancel,delete_done;
    CRITICAL_SECTION lock;
    SyncProgress latest;
    volatile LONG posted;
    HWND target;
    int action,index;
    BOOL interactive;
    wchar_t delete_path[SYNC_PATH_CAP];
    uint64_t delete_size,delete_mtime;
    BOOL delete_ok;
} SyncJob;
static HWND window,owner,tab,devices,pairs,history,progress,status_text,server_text,cloud_text;
static HWND name_edit,role_combo,project_edit,broker_edit,pair_name,group_combo,path_edit,filter_edit,delete_check;
static HFONT font,title_font;
static HBRUSH background,panel;
static SyncSettings settings;
static BOOL loaded,ready,shutting_down,runtime,refreshing;
static SyncJob *job;
static int page,selected=-1,dpi=96;
static CloudGroup groups[SYNC_PAIR_LIMIT];
static int group_count;
static CloudBinding bindings[SYNC_PAIR_LIMIT*SYNC_DEVICE_LIMIT];
static int binding_count;
static ULONGLONG last_run[SYNC_PAIR_LIMIT];
static SyncFailure history_codes[24];
static int history_actions[24];
static wchar_t history_details[24][1800];
static SyncFailure folder_codes[SYNC_PAIR_LIMIT];
static wchar_t folder_details[SYNC_PAIR_LIMIT][1800],batch_error[2048];
static wchar_t summary[2048];
static HWND labels[9];
static HWND admin_labels[4],target_project_edit,target_root_edit,quota_edit,reserve_edit,target_text,admin_note;
static int64_t candidate_id;
static BOOL migration_pending;
static char *server_route;
static BOOL admin_probe_available;
static wchar_t admin_python[40],admin_phase[32];
static uint64_t admin_free;
static int new_index=-1;
static SyncPair draft_pair;
static HWND changes,heading,empty_text;
static BOOL editing,advanced,server_advanced,subscribing;
static int catalog_choice=-1,resolve_choice,batch_action,batch_next,batch_failures;
static BOOL batch_checked[SYNC_PAIR_LIMIT],folder_checked[SYNC_PAIR_LIMIT];
static char *plans[SYNC_PAIR_LIMIT];
static int pending_push[SYNC_PAIR_LIMIT],pending_pull[SYNC_PAIR_LIMIT],pending_conflict[SYNC_PAIR_LIMIT],pending_wait[SYNC_PAIR_LIMIT];
static BOOL inspected[SYNC_PAIR_LIMIT];
static BOOL bind_failed[SYNC_PAIR_LIMIT];
static BOOL catalog_mode,devices_mode,metadata_known;
static HWND detail_text,participants,scope_text,detail_empty;
static void render_folder_detail(void);
static void render_changes(void);
static const wchar_t *publisher_name(int role);
static void start_batch(int action);
static void continue_batch(void);
static BOOL (*run_backend)(SyncRequest*,int,SyncResult*)=sync_client_run;
static BOOL login_for_job(BOOL interactive,SshLogin *login);
static BOOL (*login_backend)(BOOL,SshLogin*)=login_for_job;
static int px(int n){return MulDiv(n,dpi,96);
    }
static void localize(void);
static void layout(void);
static void populate_pairs(void);
static void controls(void);
static void select_server(void);
static void server_labels(void);
static void pending_server(void);
static void render_admin_note(void);
static BOOL login_from_profile(int64_t id,BOOL interactive,SshLogin *login);
static BOOL (*target_login_backend)(int64_t,BOOL,SshLogin*)=login_from_profile;
static BOOL start_job(int action,BOOL interactive);
static void text(const wchar_t *s){lstrcpynW(summary,s,2048);
    if(status_text)SetWindowTextW(status_text,summary);
    }
static HWND control(const wchar_t *cls,const wchar_t *name,DWORD style,int id){
    HWND h=CreateWindowExW(0,cls,name,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
    SetWindowTheme(h,L"DarkMode_Explorer",NULL);
    return h;
}
static void move(HWND h,int x,int y,int w,int height){MoveWindow(h,px(x),px(y),max(0,px(w)),max(0,px(height)),TRUE);
    }
static HWND button(int id,const wchar_t *label){return control(L"BUTTON",label,WS_TABSTOP|BS_OWNERDRAW,id);
    }
static void add_column(HWND list,int i,const wchar_t *name,int width){LVCOLUMNW column={0};
    column.mask=LVCF_TEXT|LVCF_WIDTH;
    column.pszText=(LPWSTR)name;
    column.cx=px(width);
    ListView_InsertColumn(list,i,&column);
    }
static LRESULT CALLBACK list_notifications(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;
    (void)data;
    if(message==WM_NOTIFY&&((NMHDR*)lp)->code==NM_CUSTOMDRAW)return SendMessageW(window,WM_NOTIFY,wp,lp);
    return DefSubclassProc(h,message,wp,lp);
}
static LRESULT CALLBACK tab_paint(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;
    (void)data;
    if(message==WM_PAINT||message==WM_PRINTCLIENT){
        PAINTSTRUCT paint;
        HDC dc=message==WM_PAINT?BeginPaint(h,&paint):(HDC)wp;
        RECT client;
        GetClientRect(h,&client);
        FillRect(dc,&client,background);
        HFONT old=SelectObject(dc,font);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,RGB(236,241,249));
        for(int i=0;i<TabCtrl_GetItemCount(h);i++){RECT area;
            TabCtrl_GetItemRect(h,i,&area);
            HBRUSH brush=CreateSolidBrush(TabCtrl_GetCurSel(h)==i?RGB(53,77,122):RGB(22,29,42));
            FillRect(dc,&area,brush);
            DeleteObject(brush);
            wchar_t name[80];
            TCITEMW item={0};
            item.mask=TCIF_TEXT;
            item.pszText=name;
            item.cchTextMax=80;
            TabCtrl_GetItem(h,i,&item);
            DrawTextW(dc,name,-1,&area,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            if(GetFocus()==h&&TabCtrl_GetCurSel(h)==i){InflateRect(&area,-3,-3);
                DrawFocusRect(dc,&area);
                }}
        SelectObject(dc,old);
        if(message==WM_PAINT)EndPaint(h,&paint);
        return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    return DefSubclassProc(h,message,wp,lp);
}
static HWND table(int id){HWND h=control(WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,id);
    ListView_SetExtendedListViewStyle(h,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    SetWindowSubclass(h,list_notifications,1,0);
    ListView_SetBkColor(h,RGB(22,29,42));
    ListView_SetTextBkColor(h,RGB(22,29,42));
    ListView_SetTextColor(h,RGB(236,241,249));
    return h;
    }
static void row(HWND list,int index,const wchar_t *name){LVITEMW item={0};
    item.mask=LVIF_TEXT;
    item.iItem=index;
    item.pszText=(LPWSTR)name;
    ListView_InsertItem(list,&item);
    }
static void bytes(wchar_t *out,size_t cap,uint64_t n){const wchar_t *units[]={L"B",L"KiB",L"MiB",L"GiB"};
    double x=(double)n;
    int unit=0;
    while(x>=1024&&unit<3){x/=1024;
        unit++;
        }swprintf(out,cap,L"%.1f %ls",x,units[unit]);
    }
static const wchar_t *phase_name(SyncPhase phase){switch(phase){case SYNC_CONNECT:return nova_text(L"连接服务器",L"Connecting");
        case SYNC_SCAN:return nova_text(L"校验本机文件（无传输）",L"Checking local files (no transfer)");
        case SYNC_UPLOAD:return nova_text(L"上传缺失块",L"Uploading missing blocks");
        case SYNC_DOWNLOAD:return nova_text(L"下载缺失块",L"Downloading missing blocks");
        case SYNC_APPLY:return nova_text(L"校验并保存",L"Verifying and saving");
        case SYNC_CONFIRM:return nova_text(L"确认接收并回收暂存",L"Confirming and reclaiming");
        case SYNC_DEPLOY:return nova_text(L"部署内置服务端",L"Deploying embedded server");
        case SYNC_FREEZE:return nova_text(L"冻结旧端并创建一致快照",L"Freezing source and creating snapshot");
        case SYNC_COPY_SERVER:return nova_text(L"迁移记录及未交接内容",L"Migrating records and pending content");
        case SYNC_VERIFY_SERVER:return nova_text(L"校验目标记录",L"Verifying destination records");
        case SYNC_CUTOVER:return nova_text(L"提交服务器切换",L"Committing server cutover");
        default:return nova_text(L"完成",L"Finished");
        }}
static const wchar_t *failure_name(SyncFailure f){switch(f){case SYNC_CANCELLED:return nova_text(L"已暂停，断点保留",L"Paused; checkpoints retained");
        case SYNC_STORAGE:return nova_text(L"本机同步记录保存失败",L"Unable to save local synchronization records");
        case SYNC_PATH:return nova_text(L"目录不可访问、路径无效或文件正被修改",L"Folder inaccessible, invalid path, or file being modified");
        case SYNC_CACHE:return nova_text(L"内容缓存校验或保存失败，原工作文件保留",L"Content cache verification/write failed; working files preserved");
        case SYNC_CONFLICT:return nova_text(L"两端存在冲突，未覆盖本机内容",L"Conflicting versions; local content was not overwritten");
        case SYNC_REMOTE:return nova_text(L"服务器拒绝请求",L"Server rejected the request");
        case SYNC_NETWORK:return nova_text(L"SSH 连接失败或中断，断点保留",L"SSH connection failed/interrupted; checkpoints retained");
        case SYNC_DELETE:return nova_text(L"删除未完成或删除同步尚未启用",L"Deletion was not completed or deletion sync is disabled");
        case SYNC_MOVED:return nova_text(L"服务器已迁移，请在服务器页连接迁移目标",L"Server moved; follow its destination on the Servers page");
        case SYNC_FROZEN:return nova_text(L"服务器迁移未结束，请继续迁移或恢复旧端",L"Server migration pending; resume it or restore the source");
        case SYNC_UPGRADE:return nova_text(L"服务器版本较旧，请在“服务器”页重新配置服务器",L"Server update required; choose Configure server on the Server page");
        default:return nova_text(L"同步协议校验失败",L"Synchronization protocol validation failed");
        }}
static void failure_message(wchar_t out[2048],SyncFailure code,const wchar_t *detail){
    swprintf(out,2048,L"%ls%ls%.1800ls",failure_name(code),detail[0]?L"\r\n":L"",detail);
}
static const wchar_t *action_name(int action){
    switch(action){
    case SYNC_JOB_PUSH:return nova_text(L"推送",L"Push");
    case SYNC_JOB_PULL:return nova_text(L"拉取",L"Pull");
    case SYNC_JOB_CHECK:return nova_text(L"检查",L"Check");
    case SYNC_JOB_RESOLVE:return nova_text(L"解决冲突",L"Resolve conflict");
    case SYNC_JOB_BIND:return nova_text(L"接入目录",L"Connect folder");
    case SYNC_JOB_UNBIND:return nova_text(L"断开本机",L"Disconnect here");
    default:return nova_text(L"双向同步",L"Sync");
    }
}
static BOOL persist(const SyncSettings *next){if(!sync_settings_save(next)){nova_log_write(NOVA_LOG_ERROR,"sync","settings_save_failed",L"win32=%lu",GetLastError());text(store_error());
        return FALSE;
        }settings=*next;
    return TRUE;
    }
static void forget_folder_view(int index){
    free(plans[index]);
    for(int i=index;i<SYNC_PAIR_LIMIT-1;i++){
        plans[i]=plans[i+1];inspected[i]=inspected[i+1];folder_checked[i]=folder_checked[i+1];bind_failed[i]=bind_failed[i+1];
        folder_codes[i]=folder_codes[i+1];lstrcpyW(folder_details[i],folder_details[i+1]);
        pending_push[i]=pending_push[i+1];pending_pull[i]=pending_pull[i+1];pending_conflict[i]=pending_conflict[i+1];pending_wait[i]=pending_wait[i+1];last_run[i]=last_run[i+1];
    }
    int last=SYNC_PAIR_LIMIT-1;plans[last]=NULL;inspected[last]=folder_checked[last]=bind_failed[last]=FALSE;
    folder_codes[last]=SYNC_OK;folder_details[last][0]=0;
    pending_push[last]=pending_pull[last]=pending_conflict[last]=pending_wait[last]=0;last_run[last]=0;
}
static void invalidate_comparison(int i){
    free(plans[i]);plans[i]=NULL;inspected[i]=FALSE;
    pending_push[i]=pending_pull[i]=pending_conflict[i]=pending_wait[i]=0;
}
static void new_id(wchar_t *out){unsigned char data[16];
    sqlite3_randomness(16,data);
    for(int i=0;i<16;i++)swprintf(out+i*2,3,L"%02x",data[i]);
    }
static void select_pair(int index){
    selected=index;
    catalog_choice=-1;
    refreshing=TRUE;
    SyncPair empty={0};
    SyncPair *p=index>=0&&index<settings.count?&settings.pairs[index]:(index==new_index&&index==settings.count?&draft_pair:&empty);
    SetWindowTextW(pair_name,p->name);
    SetWindowTextW(path_edit,p->directory);
    SetWindowTextW(filter_edit,p->excludes);
    SendMessageW(delete_check,BM_SETCHECK,p->sync_deletes?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(group_combo,CB_RESETCONTENT,0,0);
    SendMessageW(group_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"新建同步组",L"Create a synchronization group"));
    int choice=0;
    for(int i=0;i<group_count;i++){int at=(int)SendMessageW(group_combo,CB_ADDSTRING,0,(LPARAM)groups[i].name);
        if(!wcscmp(p->dataset,groups[i].id))choice=at;
        }
    if(p->dataset[0]&&!choice){choice=(int)SendMessageW(group_combo,CB_ADDSTRING,0,(LPARAM)p->group);
        }
    SendMessageW(group_combo,CB_SETCURSEL,choice,0);
    refreshing=FALSE;
    render_changes();
    render_folder_detail();
    controls();
}
static int group_for_pair(int index){
    for(int i=0;i<group_count;i++)if(!wcscmp(groups[i].id,settings.pairs[index].dataset))return i;
    return -1;
}
static BOOL bound_here(const SyncPair *pair){
    for(int i=0;i<binding_count;i++)if(bindings[i].role==settings.role&&!wcscmp(bindings[i].dataset,pair->dataset)&&!_wcsicmp(bindings[i].path,pair->directory))return TRUE;
    return FALSE;
}
static SyncFolderState folder_state(int i){
    int group=group_for_pair(i);
    SyncFolderFacts f={.metadata_known=metadata_known,.bound=bound_here(&settings.pairs[i])&&!bind_failed[i],
        .publisher=group>=0&&groups[group].publisher==settings.role,.publisher_known=group>=0&&groups[group].publisher>=0&&groups[group].publisher<SYNC_DEVICE_LIMIT,
        .inspected=inspected[i],.busy=job&&job->index==i,.failed=folder_codes[i]!=SYNC_OK,
        .pushes=pending_push[i],.pulls=pending_pull[i],.conflicts=pending_conflict[i],.waiting=pending_wait[i]};
    return sync_folder_state(&f);
}
static const wchar_t *relation_name(SyncRelation relation){
    switch(relation){
    case SYNC_REL_LOCAL:return nova_text(L"待接入",L"Local only");
    case SYNC_REL_PUBLISHER:return nova_text(L"本机发布",L"Published here");
    case SYNC_REL_SUBSCRIBER:return nova_text(L"本机订阅",L"Subscribed here");
    case SYNC_REL_CONNECTED:return nova_text(L"已接入",L"Connected");
    default:return nova_text(L"待确认",L"Unverified");
    }
}
static void populate_pairs(void){
    refreshing=TRUE;
    ListView_DeleteAllItems(pairs);
    ListView_SetExtendedListViewStyle(pairs,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|(catalog_mode?0:LVS_EX_CHECKBOXES));
    for(int i=0;!catalog_mode&&i<settings.count;i++){SyncPair *p=&settings.pairs[i];
        row(pairs,i,p->name);
        ListView_SetItemText(pairs,i,1,p->directory);
        int publisher=-1;
        for(int j=0;j<group_count;j++)if(!wcscmp(groups[j].id,p->dataset))publisher=groups[j].publisher;
        wchar_t source[192],state[192];
        lstrcpynW(source,publisher_name(publisher),192);
        ListView_SetItemText(pairs,i,2,source);
        SyncFolderState view=folder_state(i);
        switch(view.phase){
        case SYNC_FOLDER_WORKING:lstrcpyW(state,nova_text(L"处理中…",L"Working…"));break;
        case SYNC_FOLDER_ERROR:lstrcpynW(state,failure_name(folder_codes[i]),192);break;
        case SYNC_FOLDER_BIND:lstrcpyW(state,nova_text(L"请重试接入",L"Retry connection"));break;
        case SYNC_FOLDER_CHECK:case SYNC_FOLDER_UNKNOWN:lstrcpyW(state,nova_text(L"待检查",L"Check for changes"));break;
        case SYNC_FOLDER_CURRENT:lstrcpyW(state,nova_text(L"本机与服务器一致",L"Matches server"));break;
        case SYNC_FOLDER_CONFLICT:swprintf(state,192,nova_text(L"%d 个冲突",L"%d conflicts"),pending_conflict[i]);break;
        case SYNC_FOLDER_WAIT:swprintf(state,192,nova_text(L"%d 个等待源设备补发",L"%d awaiting source"),pending_wait[i]);break;
        default:swprintf(state,192,nova_text(L"推送 %d / 拉取 %d",L"%d push / %d pull"),pending_push[i],pending_pull[i]);break;
        }
        ListView_SetItemText(pairs,i,3,state);
        ListView_SetItemText(pairs,i,4,(LPWSTR)relation_name(view.relation));
        ListView_SetItemText(pairs,i,5,(LPWSTR)(p->paused?nova_text(L"已关闭",L"Off"):nova_text(L"已开启",L"On")));
        ListView_SetCheckState(pairs,i,folder_checked[i]);
        LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=i;item.lParam=i;ListView_SetItem(pairs,&item);
        }
    for(int i=0;catalog_mode&&i<group_count;i++){BOOL bound=FALSE;
        for(int j=0;j<settings.count;j++)if(!wcscmp(groups[i].id,settings.pairs[j].dataset))bound=TRUE;
        if(bound)continue;
        int at=ListView_GetItemCount(pairs);row(pairs,at,groups[i].name);
        ListView_SetItemText(pairs,at,1,(LPWSTR)nova_text(L"选择本机目录后订阅",L"Choose a local folder to subscribe"));
        ListView_SetItemText(pairs,at,2,(LPWSTR)publisher_name(groups[i].publisher));
        ListView_SetItemText(pairs,at,3,(LPWSTR)nova_text(L"可订阅",L"Available"));
        ListView_SetItemText(pairs,at,4,(LPWSTR)nova_text(L"未接入本机",L"Not connected"));
        ListView_SetItemText(pairs,at,5,L"—");
        LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=at;item.lParam=-i-1;ListView_SetItem(pairs,&item);
        if(catalog_choice==i)ListView_SetItemState(pairs,at,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    if(!catalog_mode&&selected>=0&&selected<settings.count)ListView_SetItemState(pairs,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    refreshing=FALSE;
    render_folder_detail();
}
typedef struct SyncCloudView {
    CloudGroup groups[SYNC_PAIR_LIMIT];
    CloudBinding bindings[SYNC_PAIR_LIMIT*SYNC_DEVICE_LIMIT];
    wchar_t names[SYNC_DEVICE_LIMIT][SYNC_NAME_CAP];
    wchar_t nodes[SYNC_DEVICE_LIMIT][SYNC_ID_CAP];
    BOOL present[SYNC_DEVICE_LIMIT];
    int groups_count,bindings_count;
    uint64_t stored,quota;
    int64_t pending;
} SyncCloudView;
static SyncCloudView *cloud_view;
static const wchar_t *publisher_name(int role){
    if(cloud_view&&role>=0&&role<SYNC_DEVICE_LIMIT&&cloud_view->present[role])return cloud_view->names[role];
    if(settings.registered&&role==settings.role&&settings.name[0])return settings.name;
    return nova_text(L"发布设备未知",L"Publisher unknown");
}
static void clear_cloud(void){
    free(cloud_view);cloud_view=NULL;metadata_known=FALSE;
    group_count=binding_count=0;catalog_choice=-1;
    ListView_DeleteAllItems(devices);ListView_DeleteAllItems(participants);
    SetWindowTextW(cloud_text,nova_text(L"尚未连接云端",L"Cloud not connected"));
}
static void render_folder_detail(void){
    if(!detail_text)return;
    int checked=0;for(int i=0;i<settings.count;i++)if(folder_checked[i])checked++;
    wchar_t value[768];
    swprintf(value,768,nova_text(L"批量范围：勾选 %d / %d 个目录",L"Batch scope: %d / %d folders checked"),checked,settings.count);
    SetWindowTextW(scope_text,value);
    if(editing){
        if(selected==new_index&&subscribing)swprintf(value,768,nova_text(L"订阅：%ls\r\n选择本机路径；保存只登记关系，接入后各设备均可推送和拉取。",L"Subscribe to: %ls\r\nChoose a local path. Saving connects metadata; all devices can push and pull."),draft_pair.group);
        else lstrcpyW(value,selected==new_index?nova_text(L"让其他设备发现此目录，再由各设备选择自己的本机路径。\r\n发布只登记元数据；检查并同步后才传输文件。",L"Make this folder discoverable; each device chooses its own local path.\r\nPublishing saves metadata. Check and sync to transfer files."):nova_text(L"修改本机的目录绑定；更改路径会关闭自动同步和删除传播。",L"Edit this device's binding; a path change turns off automatic sync and deletion propagation."));
        SetWindowTextW(detail_text,value);return;
    }
    ListView_DeleteAllItems(participants);
    const wchar_t *dataset=NULL,*name=NULL;
    if(catalog_mode&&catalog_choice>=0&&catalog_choice<group_count){dataset=groups[catalog_choice].id;name=groups[catalog_choice].name;}
    else if(!catalog_mode&&selected>=0&&selected<settings.count){dataset=settings.pairs[selected].dataset;name=settings.pairs[selected].name;}
    if(!dataset){SetWindowTextW(detail_text,nova_text(L"选择目录查看文件差异与参与设备。",L"Choose a folder to view changes and participating devices."));
        SetWindowTextW(detail_empty,nova_text(L"选择上方的一个目录。",L"Select a folder above."));return;}
    int peers=0;
    for(int i=0;i<binding_count;i++){CloudBinding *b=&bindings[i];
        if(wcscmp(b->dataset,dataset))continue;
        int at=peers++;wchar_t label[192],state[192];
        swprintf(label,192,L"%.125ls%ls",publisher_name(b->role),b->role==settings.role?nova_text(L"（本机）",L" (this device)"):L"");
        row(participants,at,label);ListView_SetItemText(participants,at,1,b->path);
        if(b->pending<0)lstrcpyW(state,nova_text(L"接收情况未知，请更新服务器",L"Receipts unknown; update server"));
        else if(b->waiting>0)swprintf(state,192,nova_text(L"%d 个待接收，其中 %d 个等待源设备补发",L"%d pending; %d awaiting source"),b->pending,b->waiting);
        else if(b->pending>0)swprintf(state,192,nova_text(L"%d 个版本待接收",L"%d versions pending"),b->pending);
        else lstrcpyW(state,nova_text(L"无待接收版本",L"No pending versions"));
        ListView_SetItemText(participants,at,2,state);
    }
    if(catalog_mode)swprintf(value,768,nova_text(L"%ls · %d 台设备已接入\r\n订阅只绑定本机路径；接入后可推送或拉取。",L"%ls · %d connected devices\r\nSubscribe binds a local path; connected devices can push and pull."),name,peers);
    else if(inspected[selected])swprintf(value,768,nova_text(L"%ls · 推送 %d / 拉取 %d / 冲突 %d / 等待补发 %d\r\n这是最近一次检查；其他设备的接收情况见“参与设备”。",L"%ls · Push %d / pull %d / conflicts %d / awaiting source %d\r\nLast comparison; see Devices for other devices' receipts."),name,pending_push[selected],pending_pull[selected],pending_conflict[selected],pending_wait[selected]);
    else swprintf(value,768,nova_text(L"%ls · 尚无有效的文件比较\r\n先检查文件差异，再推送本机修改或拉取服务器版本。",L"%ls · File comparison needed\r\nCheck changes, then push local edits or pull server versions."),name);
    SetWindowTextW(detail_text,value);
    if(devices_mode||catalog_mode)SetWindowTextW(detail_empty,nova_text(L"暂无已确认的设备绑定，请刷新服务器状态。",L"No confirmed device bindings; refresh server status."));
    else SetWindowTextW(detail_empty,inspected[selected]?nova_text(L"此检查没有文件差异。",L"No file differences in this comparison."):nova_text(L"点击“检查全部”获取文件差异；检查不会传输文件。",L"Use Check all to compare files without transferring them."));
}
static void render_cloud(void){
    if(!cloud_view)return;
    ListView_DeleteAllItems(devices);
    int row_index=0;
    for(int i=0;i<SYNC_DEVICE_LIMIT;i++)if(cloud_view->present[i]){
        row(devices,row_index,cloud_view->names[i]);
        ListView_SetItemText(devices,row_index,1,(LPWSTR)(settings.registered&&i==settings.role&&!wcscmp(cloud_view->nodes[i],settings.node_id)?nova_text(L"本机",L"This device"):nova_text(L"已注册",L"Registered")));
        int count=0;for(int j=0;j<binding_count;j++)if(bindings[j].role==i)count++;
        wchar_t folders[40];swprintf(folders,40,nova_text(L"%d 个目录",L"%d folders"),count);ListView_SetItemText(devices,row_index,2,folders);
        row_index++;
    }
    wchar_t used[48],quota[48],value[256];
    bytes(used,48,cloud_view->stored);
    bytes(quota,48,cloud_view->quota);
    swprintf(value,256,nova_text(L"云端内容 %ls / %ls · %lld 个待接收版本",L"Cloud content %ls / %ls · %lld pending versions"),used,quota,(long long)cloud_view->pending);
    SetWindowTextW(cloud_text,value);
}
static BOOL load_cloud(const char *json){
    sqlite3 *db=NULL;
    if(sqlite3_open(":memory:",&db)!=SQLITE_OK)return FALSE;
    SyncCloudView *next=calloc(1,sizeof(*next));
    BOOL ok=next&&sync_json_valid(db,json);
    sqlite3_stmt *s=NULL;
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT json_type(?1,'$.devices')='array' AND json_array_length(?1,'$.devices')<=16 AND json_type(?1,'$.datasets')='array' AND json_array_length(?1,'$.datasets')<=16 AND (json_type(?1,'$.bindings') IS NULL OR (json_type(?1,'$.bindings')='array' AND json_array_length(?1,'$.bindings')<=256))",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        ok=sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_int(s,0)==1;
        }sqlite3_finalize(s);
    s=NULL;
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT value FROM json_each(?1,'$.devices')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){
        const char *record=(const char*)sqlite3_column_text(s,0);
        char *role=sync_json_string(db,record,"$.role"),*name=sync_json_string(db,record,"$.name");
        ok=role&&name&&strlen(role)==1&&role[0]>='A'&&role[0]<'A'+SYNC_DEVICE_LIMIT;
        if(ok){int i=role[0]-'A';
            ok=!next->present[i]&&sync_wide(name,next->names[i],SYNC_NAME_CAP);
            char *node=sync_json_string(db,record,"$.node_id");
            if(node){ok=ok&&sync_wide(node,next->nodes[i],SYNC_ID_CAP)&&sync_identifier(next->nodes[i]);
                free(node);
                }
            if(ok)next->present[i]=TRUE;
            }free(role);
        free(name);
    }sqlite3_finalize(s);
    s=NULL;
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT value FROM json_each(?1,'$.datasets')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){
        const char *record=(const char*)sqlite3_column_text(s,0);
        char *id=sync_json_string(db,record,"$.id"),*name=sync_json_string(db,record,"$.name");
        int at=next->groups_count;
        ok=at<SYNC_PAIR_LIMIT&&id&&name&&sync_wide(id,next->groups[at].id,SYNC_ID_CAP)&&sync_identifier(next->groups[at].id)&&sync_wide(name,next->groups[at].name,SYNC_NAME_CAP);
        for(int i=0;ok&&i<at;i++)if(!wcscmp(next->groups[i].id,next->groups[at].id))ok=FALSE;
        if(ok)next->groups_count++;
        if(ok){char *publisher=sync_json_string(db,record,"$.publisher");
            next->groups[at].publisher=publisher&&strlen(publisher)==1?publisher[0]-'A':-1;free(publisher);
        }
        free(id);
        free(name);
    }sqlite3_finalize(s);
    s=NULL;
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT value FROM json_each(?1,'$.bindings')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){
        const char *record=(const char*)sqlite3_column_text(s,0);
        char *dataset=sync_json_string(db,record,"$.dataset"),*path=sync_json_string(db,record,"$.path"),*role=sync_json_string(db,record,"$.device");
        int at=next->bindings_count;
        ok=at<SYNC_PAIR_LIMIT*SYNC_DEVICE_LIMIT&&dataset&&path&&role&&strlen(role)==1&&role[0]>='A'&&role[0]<'A'+SYNC_DEVICE_LIMIT&&sync_wide(dataset,next->bindings[at].dataset,SYNC_ID_CAP)&&sync_identifier(next->bindings[at].dataset)&&sync_wide(path,next->bindings[at].path,SYNC_PATH_CAP);
        if(ok){next->bindings[at].role=role[0]-'A';
            int64_t pending=sync_json_int(db,record,"$.pending",-1),waiting=sync_json_int(db,record,"$.waiting",-1);
            ok=pending>=-1&&pending<=INT_MAX&&waiting>=-1&&waiting<=INT_MAX&&((pending==-1&&waiting==-1)||(pending>=0&&waiting>=0&&waiting<=pending));
            next->bindings[at].pending=(int)pending;next->bindings[at].waiting=(int)waiting;
            for(int i=0;ok&&i<at;i++)if(next->bindings[i].role==next->bindings[at].role&&!wcscmp(next->bindings[i].dataset,next->bindings[at].dataset))ok=FALSE;
            next->bindings_count++;
            }free(dataset);
        free(path);
        free(role);
    }sqlite3_finalize(s);
    if(ok){int64_t stored=sync_json_int(db,json,"$.stored_bytes",0),quota=sync_json_int(db,json,"$.quota_bytes",sync_json_int(db,json,"$.quota",15LL*1024*1024*1024));
        next->pending=sync_json_int(db,json,"$.jobs.READY",0);
        ok=stored>=0&&quota>0&&next->pending>=0;
        next->stored=(uint64_t)stored;
        next->quota=(uint64_t)quota;
        }
    if(ok){
        wchar_t choice_id[SYNC_ID_CAP]=L"";
        wchar_t catalog_id[SYNC_ID_CAP]=L"";
        if(catalog_choice>=0&&catalog_choice<group_count)lstrcpyW(catalog_id,groups[catalog_choice].id);
        int choice=(int)SendMessageW(group_combo,CB_GETCURSEL,0,0);
        if(choice>0&&choice<=group_count)lstrcpyW(choice_id,groups[choice-1].id);
        free(cloud_view);
        cloud_view=next;
        next=NULL;
        metadata_known=TRUE;
        group_count=cloud_view->groups_count;
        binding_count=cloud_view->bindings_count;
        memcpy(groups,cloud_view->groups,sizeof(groups));
        memcpy(bindings,cloud_view->bindings,sizeof(bindings));
        for(int i=0;i<settings.count;i++)if(bind_failed[i]&&bound_here(&settings.pairs[i])){bind_failed[i]=FALSE;folder_codes[i]=SYNC_OK;folder_details[i][0]=0;}
        catalog_choice=-1;
        for(int i=0;i<group_count;i++)if(!wcscmp(catalog_id,groups[i].id))catalog_choice=i;
        render_cloud();
        if(!settings.registered&&cloud_view->present[0]&&!cloud_view->present[1]&&wcscmp(cloud_view->nodes[0],settings.node_id))SendMessageW(role_combo,CB_SETCURSEL,1,0);
        populate_pairs();
        SendMessageW(group_combo,CB_RESETCONTENT,0,0);
        SendMessageW(group_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"新建同步组",L"Create a synchronization group"));
        choice=0;
        for(int i=0;i<group_count;i++){SendMessageW(group_combo,CB_ADDSTRING,0,(LPARAM)groups[i].name);
            if((choice_id[0]&&!wcscmp(choice_id,groups[i].id))||(selected>=0&&selected<settings.count&&!wcscmp(settings.pairs[selected].dataset,groups[i].id)))choice=i+1;
            }SendMessageW(group_combo,CB_SETCURSEL,choice,0);
    }
    free(next);
    sqlite3_close(db);
    return ok;
}
static void progress_callback(void *context,const SyncProgress *progress_value){SyncJob *j=context;
    EnterCriticalSection(&j->lock);
    j->latest=*progress_value;
    LeaveCriticalSection(&j->lock);
    if(!InterlockedExchange(&j->posted,1)&&!PostMessageW(j->target,PROGRESS_MESSAGE,0,0))InterlockedExchange(&j->posted,0);
    }
static BOOL recycle_callback(void *context,const wchar_t *path,uint64_t size,uint64_t mtime){
    SyncJob *j=context;
    lstrcpynW(j->delete_path,path,SYNC_PATH_CAP);
    j->delete_size=size;
    j->delete_mtime=mtime;
    j->delete_ok=FALSE;
    ResetEvent(j->delete_done);
    if(!PostMessageW(j->target,RECYCLE_MESSAGE,0,0))return FALSE;
    WaitForSingleObject(j->delete_done,INFINITE);
    return j->delete_ok;
}
static DWORD WINAPI worker(void *context){SyncJob *j=context;
    NovaLogSpan span=j->log;nova_log_write(NOVA_LOG_INFO,"sync_ui","worker_started",L"op=%ld action=%d",span.id,j->action);
    run_backend(&j->request,j->action,&j->result);
    BOOL posted=PostMessageW(j->target,DONE_MESSAGE,0,0);
    nova_log_write(posted?NOVA_LOG_INFO:NOVA_LOG_ERROR,"sync_ui","completion_posted",L"op=%ld ok=%d win32=%lu",span.id,posted,posted?0:GetLastError());
    return 0;
    }
static void pin_target(const SshServer *s,wchar_t *target){swprintf(target,384,L"NOVA Desktop SSH host:%ls:%d",s->host,s->port);
    }
static void load_pin(SshLogin *login){wchar_t target[384];
    pin_target(&login->server,target);
    PCREDENTIALW pin=NULL;
    if(CredReadW(target,CRED_TYPE_GENERIC,0,&pin)){if(pin->CredentialBlobSize==64){memcpy(login->fingerprint,pin->CredentialBlob,64);
            login->fingerprint[64]=0;
            }CredFree(pin);
        }}
static BOOL login_from_profile(int64_t id,BOOL interactive,SshLogin *login){
    SshServers *servers=calloc(1,sizeof(*servers));
    if(!servers)return FALSE;
    BOOL ok=store_load_ssh_servers(servers);
    BOOL found=FALSE;
    for(int i=0;ok&&i<servers->count;i++)if(servers->items[i].id==id){login->server=servers->items[i];
        DWORD error=0;
        ok=ssh_credentials_load(login,&found,&error,NULL);
        break;
        }free(servers);
    if(!found||!ok||(!login->secret[0]&&!login->key_file[0])){if(!interactive)return FALSE;
        if(!ssh_choose_server(window,id,login))return FALSE;
        }
    load_pin(login);
    return TRUE;
}
static BOOL login_for_job(BOOL interactive,SshLogin *login){return login_from_profile(settings.server_id,interactive,login);
    }
static BOOL start_job_impl(int action,BOOL interactive,NovaLogSpan span){
    if(job||!ready){nova_log_write(NOVA_LOG_WARN,"sync_ui","request_rejected",L"op=%ld action=%d reason=busy_or_not_ready",span.id,action);return FALSE;}
    if(action<SYNC_JOB_SERVER_PROBE&&migration_pending){text(nova_text(L"请先继续迁移或恢复旧服务器，断点未解决前暂停普通同步。",L"Resume migration or restore the source before ordinary synchronization."));
        return FALSE;
        }
    if(action<SYNC_JOB_SERVER_PROBE&&!settings.server_id){page=3;
        TabCtrl_SetCurSel(tab,3);
        layout();
        text(nova_text(L"请先在服务器页部署或连接服务器，再登记设备。",L"Deploy or connect a server on the Servers page before registering this device."));
        return FALSE;
        }
    BOOL folder_action=action>=SYNC_JOB_RUN&&action<=SYNC_JOB_RESOLVE;
    if(folder_action&&(selected<0||selected>=settings.count))return FALSE;
    SyncJob *j=calloc(1,sizeof(*j));
    if(!j)return FALSE;
    j->action=action;j->log=span;j->request.log_parent=span.id;
    j->index=folder_action?selected:-1;
    j->interactive=interactive;
    j->target=window;
    BOOL admin=action>=SYNC_JOB_SERVER_PROBE;
    if(admin&&action!=SYNC_JOB_SERVER_RESTORE&&!candidate_id){select_server();
        if(!candidate_id){nova_log_write(NOVA_LOG_WARN,"sync_ui","request_rejected",L"op=%ld reason=candidate_cancelled",span.id);free(j);
            return FALSE;
            }}
    if((!admin||action==SYNC_JOB_SERVER_MIGRATE||action==SYNC_JOB_SERVER_RESTORE)&&!login_backend(interactive,&j->request.login)){nova_log_write(NOVA_LOG_WARN,"sync_ui","request_rejected",L"op=%ld reason=login_unavailable",span.id);SecureZeroMemory(j,sizeof(*j));
        free(j);
        text(nova_text(L"请选择服务器并在本机保存登录信息。自动任务未连接。",L"Select a server and save login details locally. Automatic work did not connect."));
        return FALSE;
        }
    if(admin&&action!=SYNC_JOB_SERVER_RESTORE&&!target_login_backend(candidate_id,interactive,&j->request.target_login)){nova_log_write(NOVA_LOG_WARN,"sync_ui","request_rejected",L"op=%ld reason=target_login_unavailable",span.id);SecureZeroMemory(j,sizeof(*j));
        free(j);
        return FALSE;
        }
    if(admin){
        GetWindowTextW(target_project_edit,j->request.target_project,SYNC_PATH_CAP);
        GetWindowTextW(target_root_edit,j->request.target_broker,SYNC_PATH_CAP);
        BOOL quota_ok=FALSE,reserve_ok=FALSE;
        j->request.quota_gib=(int)GetDlgItemInt(window,C_QUOTA,&quota_ok,FALSE);
        j->request.reserve_gib=(int)GetDlgItemInt(window,C_RESERVE,&reserve_ok,FALSE);
        if(!quota_ok||!reserve_ok||j->request.quota_gib<1||j->request.quota_gib>1048576||j->request.reserve_gib<0||j->request.reserve_gib>1048576||j->request.target_project[0]!=L'/'||j->request.target_broker[0]!=L'/'||wcslen(j->request.target_project)>1900||wcslen(j->request.target_broker)>1900||wcspbrk(j->request.target_project,L"\r\n")||wcspbrk(j->request.target_broker,L"\r\n")){
            text(nova_text(L"请填写有效的服务器绝对目录与配额。",L"Enter valid absolute server paths and quotas."));
            SecureZeroMemory(j,sizeof(*j));
            free(j);
            return FALSE;
        }
        if(action==SYNC_JOB_SERVER_MIGRATE){SyncSettings next=settings;
            for(int i=0;i<next.count;i++)next.pairs[i].paused=true;
            if(!persist(&next)){SecureZeroMemory(j,sizeof(*j));
                free(j);
                return FALSE;
                }}
    }
    j->request.settings=settings;
    if(j->index>=0)j->request.pair=settings.pairs[j->index];
    if(action==SYNC_JOB_RESOLVE){
        int at=ListView_GetNextItem(changes,-1,LVNI_SELECTED);LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=at;
        sqlite3 *db=NULL;BOOL ok=at>=0&&ListView_GetItem(changes,&item)&&plans[selected]&&sqlite3_open(":memory:",&db)==SQLITE_OK;
        char pointer[80];snprintf(pointer,sizeof(pointer),"$[%d]",(int)(item.lParam>>1));
        char *record=ok?sync_json_string(db,plans[selected],pointer):NULL;
        char *path=record?sync_json_string(db,record,"$.path"):NULL,*local=record?sync_json_string(db,record,"$.local"):NULL,*remote=record?sync_json_string(db,record,"$.remote_version"):NULL;
        ok=ok&&path&&local&&remote&&strlen(local)<sizeof(j->request.expected_local)&&strlen(remote)<sizeof(j->request.expected_remote)&&sync_wide(path,j->request.resolve_path,SYNC_PATH_CAP);
        if(ok){strcpy(j->request.expected_local,local);strcpy(j->request.expected_remote,remote);j->request.resolution=resolve_choice;}
        free(record);free(path);free(local);free(remote);if(db)sqlite3_close(db);
        if(!ok){SecureZeroMemory(j,sizeof(*j));free(j);return FALSE;}
    }
    int n=swprintf(j->request.state_directory,SYNC_PATH_CAP,L"%ls\\sync\\client",store_directory());
    if(n<0||n>=SYNC_PATH_CAP){SecureZeroMemory(j,sizeof(*j));
        free(j);
        return FALSE;
        }
    j->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    j->delete_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    InitializeCriticalSection(&j->lock);
    j->request.cancel=j->cancel;
    j->request.progress=progress_callback;
    j->request.recycle=recycle_callback;
    j->request.context=j;
    if(j->cancel&&j->delete_done)j->worker=CreateThread(NULL,0,worker,j,CREATE_SUSPENDED,NULL);
    if(!j->worker){if(j->cancel)CloseHandle(j->cancel);
        if(j->delete_done)CloseHandle(j->delete_done);
        DeleteCriticalSection(&j->lock);
        SecureZeroMemory(j,sizeof(*j));
        free(j);
        return FALSE;
        }
    job=j;
    if(action==SYNC_JOB_CHECK||action==SYNC_JOB_RUN||action==SYNC_JOB_PUSH||action==SYNC_JOB_PULL||action==SYNC_JOB_RESOLVE){invalidate_comparison(j->index);render_changes();}
    ResumeThread(j->worker);
    controls();
    populate_pairs();
    text(nova_text(L"正在连接…",L"Connecting…"));
    return TRUE;
}
static BOOL start_job(int action,BOOL interactive){
    NovaLogSpan span=nova_log_begin("sync_ui","request",0);BOOL ok=start_job_impl(action,interactive,span);
    nova_log_write(ok?NOVA_LOG_INFO:NOVA_LOG_WARN,"sync_ui","request_result",L"op=%ld action=%d interactive=%d accepted=%d",span.id,action,interactive,ok);
    if(!ok)nova_log_end(span,FALSE,GetLastError());
    return ok;
}
static void recycle_on_ui(void){
    if(!job)return;
    SyncJob *j=job;
    if(WaitForSingleObject(j->cancel,0)!=WAIT_OBJECT_0){
        WIN32_FILE_ATTRIBUTE_DATA attr;
        uint64_t size=0,ticks=0;
        if(GetFileAttributesExW(j->delete_path,GetFileExInfoStandard,&attr)){size=((uint64_t)attr.nFileSizeHigh<<32)|attr.nFileSizeLow;
            ticks=(((uint64_t)attr.ftLastWriteTime.dwHighDateTime<<32)|attr.ftLastWriteTime.dwLowDateTime)-116444736000000000ULL;
            }
        if(size==j->delete_size&&ticks*100==j->delete_mtime){IFileOperation *operation=NULL;
            IShellItem *item=NULL;
            BOOL aborted=TRUE;
            HRESULT hr=CoCreateInstance(&CLSID_FileOperation,NULL,CLSCTX_INPROC_SERVER,&IID_IFileOperation,(void**)&operation);
            if(SUCCEEDED(hr))hr=IFileOperation_SetOwnerWindow(operation,window);
            if(SUCCEEDED(hr))hr=IFileOperation_SetOperationFlags(operation,FOF_ALLOWUNDO|FOF_WANTNUKEWARNING);
            if(SUCCEEDED(hr))hr=SHCreateItemFromParsingName(j->delete_path,NULL,&IID_IShellItem,(void**)&item);
            if(SUCCEEDED(hr))hr=IFileOperation_DeleteItem(operation,item,NULL);
            if(SUCCEEDED(hr))hr=IFileOperation_PerformOperations(operation);
            if(SUCCEEDED(hr))hr=IFileOperation_GetAnyOperationsAborted(operation,&aborted);
            j->delete_ok=SUCCEEDED(hr)&&!aborted;
            nova_log_write(j->delete_ok?NOVA_LOG_INFO:NOVA_LOG_WARN,"sync_ui","recycle_shell_result",L"op=%ld hresult=%lu aborted=%d",j->log.id,(DWORD)hr,aborted);
            if(item)IShellItem_Release(item);
            if(operation)IFileOperation_Release(operation);
        }
    }nova_log_write(j->delete_ok?NOVA_LOG_INFO:NOVA_LOG_WARN,"sync_ui","recycle_result",L"op=%ld ok=%d",j->log.id,j->delete_ok);SetEvent(j->delete_done);
}
static void finish_job(void){
    if(!job)return;
    SyncJob *j=job;
    WaitForSingleObject(j->worker,INFINITE);
    BOOL retry=FALSE;
    if(!shutting_down&&j->result.ssh.code==2&&j->interactive){wchar_t fingerprint[80],question[768];
        MultiByteToWideChar(CP_UTF8,0,j->result.ssh.fingerprint,-1,fingerprint,80);
        SshLogin *endpoint=j->result.target_error?&j->request.target_login:&j->request.login;
        swprintf(question,768,nova_text(L"首次连接到 %ls:%d。\nSHA256：%ls\n\n请独立核验，是否信任？",L"First connection to %ls:%d.\nSHA256: %ls\n\nVerify independently. Trust this host?"),endpoint->server.host,endpoint->server.port,fingerprint);
        if(MessageBoxW(window,question,nova_text(L"核验同步服务器",L"Verify synchronization server"),MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES){wchar_t target[384];
            pin_target(&endpoint->server,target);
            CREDENTIALW pin={0};
            pin.Type=CRED_TYPE_GENERIC;
            pin.TargetName=target;
            pin.CredentialBlob=(LPBYTE)j->result.ssh.fingerprint;
            pin.CredentialBlobSize=64;
            pin.Persist=CRED_PERSIST_LOCAL_MACHINE;
            retry=CredWriteW(&pin,0);
            }}
    if(!shutting_down){
        if(j->result.route){free(server_route);
            server_route=strdup(j->result.route);
            }
        if(j->action>=SYNC_JOB_SERVER_PROBE){
            if(j->result.failure==SYNC_OK){
                if(j->result.switched){SyncSettings next=settings;
                    next.server_id=j->request.target_login.server.id;
                    lstrcpyW(next.project,j->request.target_project);
                    lstrcpyW(next.broker,j->request.target_broker);
                    lstrcpyW(next.server_instance,j->result.server_instance);
                    if(persist(&next)){sync_server_commit_local(j->request.state_directory);
                        clear_cloud();
                        for(int i=0;i<SYNC_PAIR_LIMIT;i++)invalidate_comparison(i);
                        free(server_route);
                        server_route=NULL;
                        SetWindowTextW(project_edit,settings.project);
                        SetWindowTextW(broker_edit,settings.broker);
                        text(nova_text(L"服务器已就绪",L"Server ready"));
                        if(!settings.registered){page=0;TabCtrl_SetCurSel(tab,0);layout();SetFocus(name_edit);}
                    }else j->result.failure=SYNC_STORAGE;
                }else if(j->action==SYNC_JOB_SERVER_RESTORE)text(nova_text(L"旧服务器已恢复；所有目录保持暂停，可核对后继续。",L"Source server restored; folders remain paused for review."));
                else text(j->action==SYNC_JOB_SERVER_DEPLOY?nova_text(L"部署完成；目标已存在。本机有旧记录时请使用“迁移并切换”。",L"Deployment complete. To retain existing records, use Migrate and switch."):
                    j->result.installed?nova_text(L"服务器检查完成，可连接已有队列或迁移到此目标。",L"Server check completed; connect to its queue or migrate here."):nova_text(L"SSH 与 Python 已就绪；点击“一键部署”初始化服务器。",L"SSH and Python are ready; choose One-click deploy to initialize."));
                if(j->result.installed&&j->result.cloud){
                    if(j->result.switched||(j->request.target_login.server.id==settings.server_id&&!wcscmp(j->request.target_project,settings.project)&&!wcscmp(j->request.target_broker,settings.broker)))load_cloud(j->result.cloud);
                    sqlite3 *db=NULL;
                    if(sqlite3_open(":memory:",&db)==SQLITE_OK){char *state=sync_json_string(db,j->result.cloud,"$.control"),*phase=sync_json_string(db,j->result.cloud,"$.control.phase");
                        if(state&&phase&&!strcmp(phase,"REDIRECT")){free(server_route);
                            server_route=state;
                            state=NULL;
                            }
                        char *v=sync_json_string(db,j->result.cloud,"$.python");
                        int64_t available=sync_json_int(db,j->result.cloud,"$.free_bytes",-1);
                        admin_probe_available=v&&available>=0&&sync_wide(v,admin_python,40);
                        if(admin_probe_available){admin_free=(uint64_t)available;
                            sync_wide(phase?phase:"ACTIVE",admin_phase,32);
                            }
                        render_admin_note();
                        free(v);
                        free(phase);
                        free(state);
                        sqlite3_close(db);
                    }
                }
            }else if(!retry){wchar_t message[2048];
                swprintf(message,2048,L"%ls\r\n%.1000ls%.700ls",failure_name(j->result.failure),j->result.detail,j->result.ssh.error);
                text(message);
                }
            pending_server();
            server_labels();
        }else {
        if(j->result.failure==SYNC_OK){
            if(j->action==SYNC_JOB_REFRESH){for(int i=0;i<settings.count;i++)invalidate_comparison(i);render_changes();}
            if(j->action==SYNC_JOB_UNBIND&&j->index>=0&&j->index<settings.count){SyncSettings next=settings;
                sync_pair_remove(&next,j->index);
                if(!persist(&next))j->result.failure=SYNC_STORAGE;
                else {forget_folder_view(j->index);editing=FALSE;}
                selected=min(selected,settings.count-1);
                new_index=-1;
                }
            if(j->action==SYNC_JOB_REGISTER){SyncSettings next=settings;
                next.registered=true;
                next.role=j->result.registered_role;
                if(!persist(&next))j->result.failure=SYNC_STORAGE;
                else {page=1;TabCtrl_SetCurSel(tab,1);layout();}
                }if(j->result.failure!=SYNC_OK)text(failure_name(j->result.failure));
            else if(j->result.cloud&&!load_cloud(j->result.cloud))text(nova_text(L"云端结果超限或不完整，未截断显示。",L"Cloud results exceed limits or are incomplete; no truncated view was installed."));
            else text(j->action==SYNC_JOB_UNBIND?nova_text(L"已断开本机；本机文件、其他设备及服务器发布目录均保留。",L"Disconnected here; local files, other devices and the publication are retained."):nova_text(L"完成",L"Completed"));
            }
        else if(!retry){wchar_t message[2048];
            swprintf(message,2048,L"%ls\r\n%.1000ls%.700ls",failure_name(j->result.failure),j->result.detail,j->result.ssh.error);
            text(message);
            if(j->index>=0&&j->index<settings.count&&j->action!=SYNC_JOB_CHECK){SyncSettings next=settings;
                next.pairs[j->index].paused=true;
                persist(&next);
                }}
        if(j->index>=0&&j->index<settings.count&&j->result.changes&&(j->result.failure==SYNC_OK||j->action==SYNC_JOB_PUSH)){int i=j->index;
            free(plans[i]);plans[i]=j->result.changes;j->result.changes=NULL;
            inspected[i]=TRUE;pending_push[i]=j->result.pushes;pending_pull[i]=j->result.pulls;
            pending_conflict[i]=j->result.conflicts;pending_wait[i]=j->result.waiting;
            if(selected==i)render_changes();
        }
        if(j->action==SYNC_JOB_BIND&&j->index>=0&&j->index<settings.count){
            bind_failed[j->index]=j->result.failure!=SYNC_OK;
            folder_codes[j->index]=j->result.failure;
            swprintf(folder_details[j->index],1800,L"%.1000ls%.700ls",j->result.detail,j->result.ssh.error);
            if(j->result.failure==SYNC_OK)text(nova_text(L"目录已接入；保存只登记元数据，请检查后同步文件。",L"Folder connected; metadata saved. Check changes before transferring files."));
            else {wchar_t message[2048];swprintf(message,2048,nova_text(L"本机配置已保留，服务器接入失败。请点击“重试接入”。\r\n%.1600ls",L"Local settings retained; server connection failed. Choose Retry connection.\r\n%.1600ls"),summary);text(message);}
        }
        if(j->action==SYNC_JOB_RUN||j->action==SYNC_JOB_PUSH||j->action==SYNC_JOB_PULL||j->action==SYNC_JOB_RESOLVE||j->action==SYNC_JOB_CHECK||j->action==SYNC_JOB_BIND||j->action==SYNC_JOB_UNBIND){memmove(&history_codes[1],&history_codes[0],23*sizeof(history_codes[0]));
            memmove(&history_actions[1],&history_actions[0],23*sizeof(history_actions[0]));history_actions[0]=j->action;
            if(j->action!=SYNC_JOB_UNBIND&&j->index>=0&&j->index<settings.count){int i=j->index;
                folder_codes[i]=j->result.failure;
                swprintf(folder_details[i],1800,L"%.1000ls%.700ls",j->result.detail,j->result.ssh.error);
            }
            memmove(&history_details[1],&history_details[0],23*sizeof(history_details[0]));
            history_codes[0]=j->result.failure;
            swprintf(history_details[0],1800,L"%.1000ls%.700ls",j->result.detail,j->result.ssh.error);
            int count=ListView_GetItemCount(history);
            if(count>=24)ListView_DeleteItem(history,23);
            wchar_t name[SYNC_NAME_CAP],sent[40],received[40],reused[40];
            lstrcpynW(name,j->request.pair.name,SYNC_NAME_CAP);
            row(history,0,name);
            bytes(sent,40,j->result.stats.sent);
            bytes(received,40,j->result.stats.received);
            bytes(reused,40,j->result.stats.reused);
            ListView_SetItemText(history,0,1,sent);
            ListView_SetItemText(history,0,2,received);
            ListView_SetItemText(history,0,3,reused);
            ListView_SetItemText(history,0,4,(LPWSTR)(j->result.failure==SYNC_OK?nova_text(L"完成",L"Completed"):failure_name(j->result.failure)));
            ListView_SetItemText(history,0,5,(LPWSTR)action_name(j->action));
            if(j->action!=SYNC_JOB_UNBIND&&j->index>=0)last_run[j->index]=GetTickCount64();
            }
        }
        if(j->result.failure==SYNC_OK&&j->result.cloud&&j->action<SYNC_JOB_SERVER_PROBE){sqlite3 *db=NULL;
            if(sqlite3_open(":memory:",&db)==SQLITE_OK){char *instance=sync_json_string(db,j->result.cloud,"$.control.instance");
                wchar_t value[SYNC_ID_CAP];
            if(instance&&sync_wide(instance,value,SYNC_ID_CAP)&&sync_identifier(value)&&!settings.server_instance[0]){SyncSettings next=settings;
                    lstrcpyW(next.server_instance,value);
                    persist(&next);
                    }free(instance);
                sqlite3_close(db);
                }}
    }
    int action=j->action;
    if(batch_action&&!retry&&j->result.failure!=SYNC_OK){batch_failures++;
        if(!batch_error[0]){wchar_t detail[2048],raw[1800];
            swprintf(raw,1800,L"%.1000ls%.700ls",j->result.detail,j->result.ssh.error);
            failure_message(detail,j->result.failure,raw);
            swprintf(batch_error,2048,L"%.120ls: %.1900ls",j->request.pair.name,detail);
        }
        if(j->result.failure==SYNC_NETWORK||j->result.failure==SYNC_STORAGE||j->result.failure==SYNC_CANCELLED||j->result.failure==SYNC_MOVED||j->result.failure==SYNC_FROZEN||j->result.failure==SYNC_UPGRADE)batch_action=0;
    }
    nova_log_write(NOVA_LOG_INFO,"sync_ui","completion_consumed",L"op=%ld failure=%d shutting_down=%d retry=%d",j->log.id,j->result.failure,shutting_down,retry);
    nova_log_end(j->log,j->result.failure==SYNC_OK,j->result.failure==SYNC_CANCELLED?ERROR_CANCELLED:j->result.native_error);
    CloseHandle(j->worker);
    CloseHandle(j->cancel);
    CloseHandle(j->delete_done);
    DeleteCriticalSection(&j->lock);
    sync_result_dispose(&j->result);
    SecureZeroMemory(j,sizeof(*j));
    free(j);
    job=NULL;
    populate_pairs();
    controls();
    layout();
    if(retry)start_job(action,TRUE);
    else if(batch_action&&!shutting_down)continue_batch();
}
static void select_server(void){SshLogin *login=calloc(1,sizeof(*login));
    if(!login)return;
    page=3;
    TabCtrl_SetCurSel(tab,3);
    layout();
    if(ssh_pick_server(window,GetDlgItem(window,C_TARGET),login)){candidate_id=login->server.id;
        if(migration_pending)pending_server();
        server_labels();
        controls();
        }
    SecureZeroMemory(login,sizeof(*login));
    free(login);
    }
static void server_labels(void){
    SshServers *servers=calloc(1,sizeof(*servers));
    if(!servers)return;
    if(store_load_ssh_servers(servers)){
        wchar_t active[768],target[768];
        lstrcpyW(active,nova_text(L"尚未配置服务器",L"Server not configured"));
        lstrcpyW(target,nova_text(L"选择服务器",L"Choose a server"));
        for(int i=0;i<servers->count;i++){SshServer *s=&servers->items[i];
            if(s->id==settings.server_id)swprintf(active,768,L"%ls · %ls@%ls:%d",s->name,s->user,s->host,s->port);
            if(s->id==candidate_id)swprintf(target,768,L"%ls@%ls:%d",s->user,s->host,s->port);
        }SetWindowTextW(server_text,active);
        SetWindowTextW(target_text,target);
    }free(servers);
}
static void pending_server(void){
    SyncRequest *r=calloc(1,sizeof(*r));
    if(!r)return;
    r->settings=settings;
    swprintf(r->state_directory,SYNC_PATH_CAP,L"%ls\\sync\\client",store_directory());
    migration_pending=sync_server_pending(r);
    if(migration_pending){candidate_id=r->target_login.server.id;
        SetWindowTextW(target_project_edit,r->target_project);
        SetWindowTextW(target_root_edit,r->target_broker);

        }
    free(r);
    render_admin_note();
}
static void render_admin_note(void){
    if(!admin_note)return;
    if(migration_pending){SetWindowTextW(admin_note,nova_text(L"检测到迁移断点。继续迁移会复用已传文件；切换提交前也可恢复旧服务器。",L"Migration checkpoint found. Resume reuses received files; restore the source before cutover if needed."));
        return;
        }
    if(admin_probe_available){wchar_t size[48],value[512];
        bytes(size,48,admin_free);
        const wchar_t *state=!wcscmp(admin_phase,L"ACTIVE")?nova_text(L"就绪",L"Ready"):!wcscmp(admin_phase,L"REDIRECT")?nova_text(L"已迁移",L"Moved"):nova_text(L"迁移未结束",L"Migration pending");
        swprintf(value,512,nova_text(L"Python %ls · 可用 %ls · %ls",L"Python %ls · %ls free · %ls"),admin_python,size,state);
        SetWindowTextW(admin_note,value);
        return;
        }
    SetWindowTextW(admin_note,nova_text(L"先将本机 SSH 公钥加入服务器，再选择连接。",L"Add this computer's SSH public key to your server, then choose its connection."));
}
static void follow_server(void){
    if(!server_route||job)return;
    sqlite3 *db=NULL;
    if(sqlite3_open(":memory:",&db)!=SQLITE_OK)return;
    char *destination_json=sync_json_string(db,server_route,"$.destination");
    char *host=destination_json?sync_json_string(db,destination_json,"$.host"):NULL,*user=destination_json?sync_json_string(db,destination_json,"$.user"):NULL;
    char *name=destination_json?sync_json_string(db,destination_json,"$.name"):NULL,*project=destination_json?sync_json_string(db,destination_json,"$.project"):NULL,*root=destination_json?sync_json_string(db,destination_json,"$.root"):NULL;
    SshServers *servers=calloc(1,sizeof(*servers));
    SshServer entry={0};
    wchar_t p[SYNC_PATH_CAP],q[SYNC_PATH_CAP];
    entry.port=destination_json?(int)sync_json_int(db,destination_json,"$.port",0):0;
    BOOL ok=servers&&host&&user&&project&&root&&sync_wide(host,entry.host,256)&&sync_wide(user,entry.user,128)&&sync_wide(project,p,SYNC_PATH_CAP)&&sync_wide(root,q,SYNC_PATH_CAP)&&p[0]==L'/'&&q[0]==L'/'&&store_load_ssh_servers(servers);
    if(ok){lstrcpyW(entry.directory,L"/");
        if(!name||!sync_wide(name,entry.name,SSH_NAME_CAP)||!entry.name[0])lstrcpyW(entry.name,nova_text(L"同步迁移目标",L"Synchronization destination"));
        for(int i=0;i<servers->count;i++)if(!lstrcmpiW(servers->items[i].host,entry.host)&&!wcscmp(servers->items[i].user,entry.user)&&servers->items[i].port==entry.port)entry.id=servers->items[i].id;
        if(!entry.id&&servers->count<SSH_SERVER_LIMIT){entry.id=store_new_id();
            wchar_t base[40];
            lstrcpynW(base,entry.name,40);
            for(int suffix=1;suffix<=SSH_SERVER_LIMIT+1;suffix++){BOOL duplicate=FALSE;
                for(int i=0;i<servers->count;i++)if(!lstrcmpiW(servers->items[i].name,entry.name))duplicate=TRUE;
                if(!duplicate)break;
                swprintf(entry.name,SSH_NAME_CAP,L"%ls (%d)",base,suffix);
                }
            servers->items[servers->count++]=entry;
            ok=ssh_servers_valid(servers)&&store_save_ssh_servers(servers);
        }else if(!entry.id)ok=FALSE;
        if(ok){candidate_id=entry.id;
            SetWindowTextW(target_project_edit,p);
            SetWindowTextW(target_root_edit,q);
            page=3;
            TabCtrl_SetCurSel(tab,3);
            layout();
            server_labels();
            start_job(SYNC_JOB_SERVER_ATTACH,TRUE);
            }
    }
    if(!ok)text(nova_text(L"迁移目标格式无效或服务器配置已满，原连接保留。",L"Invalid destination or full server profile collection; current connection preserved."));
    free(servers);
    free(host);
    free(user);
    free(name);
    free(project);
    free(root);
    free(destination_json);
    sqlite3_close(db);
}
static BOOL save_identity(void){SyncSettings next=settings;
    GetWindowTextW(name_edit,next.name,SYNC_NAME_CAP);
    GetWindowTextW(project_edit,next.project,SYNC_PATH_CAP);
    GetWindowTextW(broker_edit,next.broker,SYNC_PATH_CAP);
    if(!next.node_id[0])new_id(next.node_id);
    return persist(&next);
    }
static void browse_folder(void){BROWSEINFOW b={0};
    b.hwndOwner=window;
    b.lpszTitle=nova_text(L"选择本机同步目录（只记录路径）",L"Choose a local sync folder (records its path only)");
    b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&b);
    if(id){wchar_t path[MAX_PATH];
        if(SHGetPathFromIDListW(id,path))SetWindowTextW(path_edit,path);
        CoTaskMemFree(id);
        }}
static void new_pair(void){
    if(settings.count>=SYNC_PAIR_LIMIT||job)return;
    ZeroMemory(&draft_pair,sizeof(draft_pair));
    new_id(draft_pair.id);
    new_id(draft_pair.dataset);
    subscribing=FALSE;catalog_mode=FALSE;catalog_choice=-1;
    lstrcpyW(draft_pair.name,nova_text(L"新同步目录",L"New sync folder"));
    lstrcpyW(draft_pair.group,draft_pair.name);
    lstrcpyW(draft_pair.excludes,L".git;node_modules;.venv;__pycache__;.next;.nova-sync-*;.cloudsync-*;*.tmp");
    draft_pair.paused=true;
    new_index=settings.count;
    refreshing=TRUE;
    ListView_SetItemState(pairs,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    refreshing=FALSE;
    select_pair(new_index);
    populate_pairs();
    editing=TRUE;advanced=FALSE;page=1;TabCtrl_SetCurSel(tab,1);layout();
    text(L"");
    SetFocus(path_edit);
}
static void save_pair(void){
    if(selected<0||selected>settings.count||(selected==settings.count&&selected!=new_index))return;
    SyncSettings next=settings;
    if(selected==settings.count){next.pairs[next.count++]=draft_pair;
        }
    SyncPair *p=&next.pairs[selected];
    GetWindowTextW(pair_name,p->name,SYNC_NAME_CAP);
    GetWindowTextW(path_edit,p->directory,SYNC_PATH_CAP);
    GetWindowTextW(filter_edit,p->excludes,SYNC_FILTER_CAP);
    p->sync_deletes=SendMessageW(delete_check,BM_GETCHECK,0,0)==BST_CHECKED;
    DWORD attr=GetFileAttributesW(p->directory);
    if(PathIsRelativeW(p->directory)||attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY)||(attr&FILE_ATTRIBUTE_REPARSE_POINT)){text(nova_text(L"请选择存在的绝对目录，不跟随重解析点。",L"Choose an existing absolute folder; reparse points are not followed."));
        return;
        }
    if(selected==new_index){int group=(int)SendMessageW(group_combo,CB_GETCURSEL,0,0);
        if(group>0&&group<=group_count){lstrcpyW(p->dataset,groups[group-1].id);
            lstrcpyW(p->group,groups[group-1].name);
            }else lstrcpyW(p->group,p->name);
        }
    if(selected>=settings.count||wcscmp(settings.pairs[selected].directory,p->directory)){p->paused=true;
        p->sync_deletes=false;
        }
    if(!sync_settings_valid(&next)){text(nova_text(L"目录名称、同步组或路径重复/无效，原配置保留。",L"Duplicate or invalid folder name, group or path; configuration preserved."));
        return;
        }
    BOOL path_changed=selected>=settings.count||wcscmp(settings.pairs[selected].directory,p->directory)!=0;
    if(persist(&next)){new_index=-1;
        nova_log_write(NOVA_LOG_INFO,"sync","binding_saved",L"path_changed=%d metadata_only=1 deletes=%d automatic=%d",path_changed,p->sync_deletes,!p->paused);
        folder_checked[selected]=TRUE;editing=FALSE;
        invalidate_comparison(selected);bind_failed[selected]=FALSE;folder_codes[selected]=SYNC_OK;folder_details[selected][0]=0;
        populate_pairs();
        select_pair(selected);
        layout();
        text(nova_text(L"已保存",L"Saved"));
        if(settings.registered&&settings.server_id)start_job(SYNC_JOB_BIND,TRUE);
        }
}
static void import_legacy(void){wchar_t path[MAX_PATH]=L"";
    OPENFILENAMEW f={0};
    f.lStructSize=sizeof(f);
    f.hwndOwner=window;
    f.lpstrFile=path;
    f.nMaxFile=MAX_PATH;
    f.lpstrTitle=nova_text(L"导入续桥配置 JSON",L"Import ChunkSync configuration JSON");
    f.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&f))return;
    SyncSettings *next=malloc(sizeof(*next));
    if(!next)return;
    if(sync_settings_import(path,next)&&persist(next)){clear_cloud();
        for(int i=0;i<SYNC_PAIR_LIMIT;i++){invalidate_comparison(i);bind_failed[i]=FALSE;folder_codes[i]=SYNC_OK;folder_details[i][0]=0;folder_checked[i]=i<settings.count;last_run[i]=0;}
        populate_pairs();
        select_pair(settings.count?0:-1);
        localize();
        text(nova_text(L"已导入设备身份和目录；所有目录保持暂停。",L"Device identity and folders imported; every folder remains paused."));
        }else text(nova_text(L"导入格式不兼容或超出容量，原配置未修改。",L"Import is incompatible or over capacity; configuration unchanged."));
    free(next);
    }
static void version_label(sqlite3 *db,const char *record,BOOL remote,wchar_t *out){
    if(remote&&sync_json_int(db,record,"$.remote_deleted",0)){lstrcpyW(out,nova_text(L"已删除",L"Deleted"));return;}
    int64_t nanos=sync_json_int(db,record,remote?"$.remote_mtime":"$.mtime",-1);
    int64_t size=sync_json_int(db,record,remote?"$.remote_size":"$.size",-1);
    if(nanos>=0&&size>=0){
        ULARGE_INTEGER value;value.QuadPart=(uint64_t)nanos/100+116444736000000000ULL;
        FILETIME file={value.LowPart,value.HighPart};SYSTEMTIME utc,local;wchar_t amount[40];
        if(FileTimeToSystemTime(&file,&utc)&&SystemTimeToTzSpecificLocalTime(NULL,&utc,&local)){
            const wchar_t *units[]={L"B",L"KiB",L"MiB",L"GiB"};double count=(double)size;int unit=0;
            while(count>=1024&&unit<3){count/=1024;unit++;}
            swprintf(amount,40,L"%.0f %ls",count,units[unit]);swprintf(out,80,L"%04u-%02u-%02u %02u:%02u · %ls",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute,amount);return;
        }
    }
    lstrcpyW(out,nova_text(L"—",L"—"));
}
static const wchar_t *change_name(const char *state){
    if(!strcmp(state,"conflict"))return nova_text(L"冲突",L"Conflict");
    if(!strcmp(state,"push"))return nova_text(L"↑ 待推送",L"↑ Push");
    if(!strcmp(state,"pull"))return nova_text(L"↓ 待拉取",L"↓ Pull");
    if(!strcmp(state,"waiting"))return nova_text(L"等待源设备",L"Awaiting source");
    return nova_text(L"已同步",L"Up to date");
}
static void render_changes(void){
    if(!changes)return;
    wchar_t chosen[SYNC_PATH_CAP]=L"";int previous=ListView_GetNextItem(changes,-1,LVNI_SELECTED);
    if(previous>=0)ListView_GetItemText(changes,previous,0,chosen,SYNC_PATH_CAP);
    BOOL was_refreshing=refreshing;refreshing=TRUE;
    ListView_DeleteAllItems(changes);
    if(selected<0||selected>=settings.count||!plans[selected]){refreshing=was_refreshing;return;}
    sqlite3 *db=NULL;sqlite3_stmt *s=NULL;
    BOOL ok=sqlite3_open(":memory:",&db)==SQLITE_OK&&sqlite3_prepare_v2(db,"SELECT key,value FROM json_each(?1) WHERE json_extract(value,'$.state')!='synced' ORDER BY CASE json_extract(value,'$.state') WHEN 'conflict' THEN 0 WHEN 'waiting' THEN 1 WHEN 'pull' THEN 2 WHEN 'push' THEN 3 ELSE 4 END,json_extract(value,'$.path')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,plans[selected],-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){const char *record=(const char*)sqlite3_column_text(s,1);
        char *p=sync_json_string(db,record,"$.path"),*local=sync_json_string(db,record,"$.local_version"),*remote=sync_json_string(db,record,"$.remote_version"),*state=sync_json_string(db,record,"$.state");
        wchar_t path[SYNC_PATH_CAP],a[80],b[80];
        ok=p&&local&&remote&&state&&sync_wide(p,path,SYNC_PATH_CAP);
        if(ok){int at=ListView_GetItemCount(changes);row(changes,at,path);
            version_label(db,record,FALSE,a);version_label(db,record,TRUE,b);
            ListView_SetItemText(changes,at,1,a);ListView_SetItemText(changes,at,2,b);ListView_SetItemText(changes,at,3,(LPWSTR)change_name(state));
            char *origin=sync_json_string(db,record,"$.origin");
            ListView_SetItemText(changes,at,4,(LPWSTR)(origin&&strlen(origin)==1?publisher_name(origin[0]-'A'):L"—"));free(origin);
            LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=at;item.lParam=((LPARAM)sqlite3_column_int(s,0)<<1)|(remote[0]!=0&&!strcmp(state,"conflict"));ListView_SetItem(changes,&item);
            if((!chosen[0]&&at==0)||!wcscmp(path,chosen))ListView_SetItemState(changes,at,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        }
        free(p);free(local);free(remote);free(state);
    }
    sqlite3_finalize(s);if(db)sqlite3_close(db);
    refreshing=was_refreshing;
    render_folder_detail();
}
static BOOL start_folder_job(int action,int index,BOOL interactive){
    int visible=selected;selected=index;
    BOOL ok=start_job(action,interactive);
    selected=visible;render_changes();populate_pairs();controls();
    return ok;
}
static void continue_batch(void){
    while(batch_next<settings.count){int i=batch_next++;
        if(!batch_checked[i])continue;
        if(start_folder_job(batch_action,i,TRUE))return;
        batch_action=0;text(nova_text(L"已停止批量操作",L"Batch stopped"));controls();return;
    }
    batch_action=0;
    int pushes=0,pulls=0,conflicts=0,waiting=0;
    for(int i=0;i<settings.count;i++){pushes+=pending_push[i];pulls+=pending_pull[i];conflicts+=pending_conflict[i];waiting+=pending_wait[i];}
    wchar_t value[2048];
    if(batch_failures)swprintf(value,2048,nova_text(L"%d 个目录失败 · %.1900ls",L"%d folders failed · %.1900ls"),batch_failures,batch_error);
    else swprintf(value,256,nova_text(L"↑ %d 待推送   ↓ %d 待拉取   ·   %d 个冲突   ·   %d 个等待补发",L"↑ %d push   ↓ %d pull   ·   %d conflicts   ·   %d awaiting source"),pushes,pulls,conflicts,waiting);
    text(value);controls();
}
static void start_batch(int action){
    if(job||editing||catalog_mode||!settings.registered||!settings.count||migration_pending)return;
    BOOL any=action==SYNC_JOB_CHECK;
    for(int i=0;i<settings.count;i++)if(folder_checked[i])any=TRUE;
    if(!any){text(nova_text(L"请先勾选要传输的本机目录。",L"Check the local folders to transfer first."));return;}
    batch_action=action;batch_next=0;batch_failures=0;
    batch_error[0]=0;
    for(int i=0;i<settings.count;i++)batch_checked[i]=action==SYNC_JOB_CHECK||folder_checked[i];
    continue_batch();
}
static void subscribe_folder(void){
    if(job||settings.count>=SYNC_PAIR_LIMIT||!settings.registered)return;
    int choice=catalog_choice;
    if(choice<0){HMENU menu=CreatePopupMenu();
        for(int i=0;i<group_count;i++){BOOL bound=FALSE;for(int j=0;j<settings.count;j++)if(!wcscmp(groups[i].id,settings.pairs[j].dataset))bound=TRUE;
            if(!bound)AppendMenuW(menu,MF_STRING,(UINT_PTR)i+1,groups[i].name);
        }
        RECT area;GetWindowRect(GetDlgItem(window,C_SUBSCRIBE),&area);
        choice=(int)TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,area.left,area.bottom,0,window,NULL)-1;DestroyMenu(menu);
    }
    if(choice<0||choice>=group_count){text(nova_text(L"暂无可订阅目录。请先刷新，或在另一台电脑发布目录。",L"No folders available. Refresh, or publish a folder on another device."));return;}
    new_pair();subscribing=TRUE;lstrcpyW(draft_pair.group,groups[choice].name);lstrcpyW(draft_pair.dataset,groups[choice].id);SetWindowTextW(pair_name,groups[choice].name);SendMessageW(group_combo,CB_SETCURSEL,choice+1,0);controls();layout();
}
static void controls(void){
    BOOL idle=job==NULL,has=selected>=0&&(selected<settings.count||(selected==new_index&&selected==settings.count));
    EnableWindow(pairs,idle&&!editing);
    const int edit_ids[]={C_SERVER,C_NAME,C_ROLE,C_REGISTER,C_REFRESH,C_IMPORT,C_NEW,C_REMOVE,C_PAIR_NAME,C_GROUP,C_PATH,C_BROWSE,C_FILTER,C_DELETE,C_SAVE,C_RUN,C_AUTO,C_PROJECT,C_BROKER};
    for(size_t i=0;i<sizeof(edit_ids)/sizeof(edit_ids[0]);i++)EnableWindow(GetDlgItem(window,edit_ids[i]),idle);
    EnableWindow(role_combo,idle&&!settings.registered);
    for(int id=C_PAIR_NAME;id<=C_SAVE;id++)if(GetDlgItem(window,id))EnableWindow(GetDlgItem(window,id),idle&&has);
    EnableWindow(group_combo,idle&&has&&selected==new_index);
    EnableWindow(GetDlgItem(window,C_RUN),idle&&!migration_pending&&has&&selected<settings.count&&settings.registered);
    EnableWindow(GetDlgItem(window,C_AUTO),idle&&!migration_pending&&has&&selected<settings.count&&settings.registered);
    EnableWindow(GetDlgItem(window,C_PAUSE),job!=NULL);
    const int admin_ids[]={C_TARGET,C_TARGET_PROJECT,C_TARGET_ROOT,C_QUOTA,C_RESERVE,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW};
    for(size_t i=0;i<sizeof(admin_ids)/sizeof(admin_ids[0]);i++)EnableWindow(GetDlgItem(window,admin_ids[i]),idle);
    if(migration_pending){EnableWindow(target_project_edit,FALSE);
        EnableWindow(target_root_edit,FALSE);
        EnableWindow(quota_edit,FALSE);
        EnableWindow(reserve_edit,FALSE);
        }
    EnableWindow(GetDlgItem(window,C_MIGRATE),idle&&settings.registered&&settings.server_id>0);
    EnableWindow(GetDlgItem(window,C_RESTORE),idle&&migration_pending);
    EnableWindow(GetDlgItem(window,C_FOLLOW),idle&&server_route!=NULL);
    if(GetDlgItem(window,C_AUTO))SetWindowTextW(GetDlgItem(window,C_AUTO),has&&selected<settings.count&&!settings.pairs[selected].paused?nova_text(L"暂停自动",L"Pause automatic"):nova_text(L"开启自动",L"Enable automatic"));
    BOOL checked=FALSE;for(int i=0;i<settings.count;i++)if(folder_checked[i])checked=TRUE;
    const int batch_ids[]={C_PUSH_ALL,C_PULL_ALL,C_SYNC_ALL};
    for(size_t i=0;i<sizeof(batch_ids)/sizeof(batch_ids[0]);i++)EnableWindow(GetDlgItem(window,batch_ids[i]),idle&&!editing&&!catalog_mode&&checked&&settings.registered&&!migration_pending);
    EnableWindow(GetDlgItem(window,C_CHECK_ALL),idle&&!editing&&!catalog_mode&&settings.registered&&settings.count>0&&!migration_pending);
    EnableWindow(GetDlgItem(window,C_NEW),idle&&settings.registered&&settings.count<SYNC_PAIR_LIMIT);
    EnableWindow(GetDlgItem(window,C_SUBSCRIBE),idle&&settings.registered&&group_count>0&&settings.count<SYNC_PAIR_LIMIT);
    EnableWindow(GetDlgItem(window,C_EDIT),idle&&!editing&&!catalog_mode&&has&&selected<settings.count);
    EnableWindow(GetDlgItem(window,C_REMOVE),idle&&!editing&&!catalog_mode&&has&&selected<settings.count);
    EnableWindow(GetDlgItem(window,C_AUTO),idle&&!editing&&!catalog_mode&&has&&selected<settings.count&&settings.registered&&!migration_pending);
    EnableWindow(GetDlgItem(window,C_CANCEL_EDIT),idle);
    EnableWindow(GetDlgItem(window,C_MY_FOLDERS),idle&&!editing);
    EnableWindow(GetDlgItem(window,C_CATALOG),idle&&!editing);
    EnableWindow(GetDlgItem(window,C_NEW),idle&&!editing&&settings.registered&&settings.count<SYNC_PAIR_LIMIT);
    EnableWindow(GetDlgItem(window,C_SUBSCRIBE),idle&&!editing&&settings.registered&&catalog_choice>=0&&settings.count<SYNC_PAIR_LIMIT);
    EnableWindow(GetDlgItem(window,C_BIND),idle&&!editing&&!catalog_mode&&has&&selected<settings.count&&settings.registered&&!migration_pending&&(!metadata_known||!bound_here(&settings.pairs[selected])||bind_failed[selected]));
    EnableWindow(GetDlgItem(window,C_REGISTER),idle&&settings.server_id>0&&!migration_pending);
    LVITEMW version={0};version.mask=LVIF_PARAM;version.iItem=ListView_GetNextItem(changes,-1,LVNI_SELECTED);
    BOOL can_resolve=version.iItem>=0&&ListView_GetItem(changes,&version)&&(version.lParam&1);
    EnableWindow(GetDlgItem(window,C_LOCAL),idle&&!editing&&!catalog_mode&&has&&inspected[selected]&&can_resolve);
    EnableWindow(GetDlgItem(window,C_REMOTE),idle&&!editing&&!catalog_mode&&has&&inspected[selected]&&can_resolve);
    SetWindowTextW(GetDlgItem(window,C_SAVE),selected==new_index?(subscribing?nova_text(L"订阅",L"Subscribe"):nova_text(L"发布",L"Publish")):nova_text(L"保存",L"Save"));
    SetWindowTextW(GetDlgItem(window,C_REGISTER),settings.registered?nova_text(L"保存名称",L"Save name"):nova_text(L"注册本设备",L"Register this device"));
}
static void localize(void){
    if(!window)return;
    SetWindowTextW(window,nova_text(L"设备与同步 · NOVA Desktop",L"Devices and synchronization · NOVA Desktop"));
    SetWindowTextW(devices,nova_text(L"已登记设备",L"Registered devices"));
    SetWindowTextW(pairs,catalog_mode?nova_text(L"可订阅目录",L"Available publications"):nova_text(L"本机同步目录",L"Local sync folders"));
    SetWindowTextW(history,nova_text(L"同步记录",L"Synchronization history"));
    SetWindowTextW(progress,nova_text(L"同步及服务器管理进度",L"Synchronization and server management progress"));
    const wchar_t *tabs[]={nova_text(L"设备",L"Devices"),nova_text(L"同步目录",L"Folders"),nova_text(L"传输记录",L"Transfers"),nova_text(L"服务器",L"Server")};
    for(int i=0;i<4;i++){TCITEMW item={0};
        item.mask=TCIF_TEXT;
        item.pszText=(LPWSTR)tabs[i];
        TabCtrl_SetItem(tab,i,&item);
        }
    const int ids[]={C_SERVER,C_REGISTER,C_REFRESH,C_IMPORT,C_NEW,C_REMOVE,C_BROWSE,C_SAVE,C_RUN,C_AUTO,C_PAUSE,C_CLOSE};
    const wchar_t *names[]={nova_text(L"配置服务器",L"Configure server"),nova_text(L"注册本设备",L"Register this device"),nova_text(L"刷新",L"Refresh"),nova_text(L"导入旧配置…",L"Import legacy…"),nova_text(L"发布目录",L"Publish folder"),nova_text(L"断开本机",L"Disconnect here"),nova_text(L"选择目录…",L"Choose folder…"),nova_text(L"保存",L"Save"),nova_text(L"同步此目录",L"Sync folder"),nova_text(L"开启自动",L"Enable automatic"),nova_text(L"停止任务",L"Stop task"),nova_text(L"关闭",L"Close")};
    for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++)SetWindowTextW(GetDlgItem(window,ids[i]),names[i]);
    SetWindowTextW(GetDlgItem(window,C_SERVER),nova_text(L"配置服务器",L"Configure server"));
    const int admin_ids[]={C_TARGET,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW};
    const wchar_t *admin_names[]={nova_text(L"选择 SSH 服务器…",L"Choose SSH server…"),nova_text(L"检查连接",L"Test connection"),nova_text(L"配置服务器",L"Configure server"),nova_text(L"连接已有队列",L"Connect existing queue"),nova_text(L"迁移并切换",L"Migrate and switch"),nova_text(L"恢复旧服务器",L"Restore source"),nova_text(L"跟随迁移目标",L"Follow destination")};
    for(size_t i=0;i<sizeof(admin_ids)/sizeof(admin_ids[0]);i++)SetWindowTextW(GetDlgItem(window,admin_ids[i]),admin_names[i]);
    const wchar_t *admin_text[]={nova_text(L"目标项目目录",L"Destination project"),nova_text(L"目标队列目录",L"Destination queue"),nova_text(L"新队列配额（GiB）",L"New queue quota (GiB)"),nova_text(L"磁盘保留（GiB）",L"Free reserve (GiB)")};
    for(int i=0;i<4;i++)SetWindowTextW(admin_labels[i],admin_text[i]);
    const wchar_t *label_text[]={nova_text(L"本机名称",L"Device name"),L"",L"",L"",nova_text(L"名称",L"Name"),nova_text(L"来源",L"Publication"),nova_text(L"本机目录",L"Local folder"),nova_text(L"排除规则",L"Exclusions"),L""};
    for(int i=0;i<9;i++)SetWindowTextW(labels[i],label_text[i]);
    SetWindowTextW(delete_check,nova_text(L"同步删除（系统确认并移入回收站）",L"Sync deletions (confirm and recycle)"));
    SetWindowTextW(name_edit,settings.name);
    SendMessageW(role_combo,CB_RESETCONTENT,0,0);
    SendMessageW(role_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"设备 A",L"Device A"));
    SendMessageW(role_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"设备 B",L"Device B"));
    SendMessageW(role_combo,CB_SETCURSEL,settings.role,0);
    SetWindowTextW(project_edit,settings.project);
    SetWindowTextW(broker_edit,settings.broker);
    const wchar_t *device_columns[]={nova_text(L"设备",L"Device"),nova_text(L"状态",L"Status"),nova_text(L"目录",L"Folders")};
    const wchar_t *pair_columns[]={nova_text(L"目录",L"Folder"),nova_text(L"本机位置",L"Local location"),nova_text(L"发布设备",L"Published by"),nova_text(L"文件差异",L"File changes"),nova_text(L"本机关系",L"Participation"),nova_text(L"自动同步",L"Automatic")};
    const int extra_ids[]={C_CHECK_ALL,C_PUSH_ALL,C_PULL_ALL,C_SYNC_ALL,C_SUBSCRIBE,C_EDIT,C_ADVANCED,C_SERVER_ADVANCED,C_LOCAL,C_REMOTE};
    const wchar_t *extra_names[]={nova_text(L"检查全部",L"Check all"),nova_text(L"推送勾选项",L"Push checked"),nova_text(L"拉取勾选项",L"Pull checked"),nova_text(L"同步勾选项",L"Sync checked"),nova_text(L"订阅此目录",L"Subscribe folder"),nova_text(L"编辑",L"Edit"),nova_text(L"高级选项",L"Advanced"),nova_text(L"高级设置",L"Advanced"),nova_text(L"保留本机版本",L"Keep local version"),nova_text(L"采用服务器版本",L"Use server version")};
    for(size_t i=0;i<sizeof(extra_ids)/sizeof(extra_ids[0]);i++)SetWindowTextW(GetDlgItem(window,extra_ids[i]),extra_names[i]);
    SetWindowTextW(GetDlgItem(window,C_MY_FOLDERS),nova_text(L"本机目录",L"Local folders"));
    SetWindowTextW(GetDlgItem(window,C_CATALOG),nova_text(L"可订阅目录",L"Available folders"));
    SetWindowTextW(GetDlgItem(window,C_FILES_VIEW),nova_text(L"文件差异",L"File changes"));
    SetWindowTextW(GetDlgItem(window,C_DEVICES_VIEW),nova_text(L"参与设备",L"Devices"));
    SetWindowTextW(GetDlgItem(window,C_CANCEL_EDIT),nova_text(L"取消编辑",L"Cancel edit"));
    SetWindowTextW(GetDlgItem(window,C_BIND),nova_text(L"重试接入",L"Retry connection"));
    SetWindowTextW(participants,nova_text(L"此目录的设备绑定和接收记录",L"Folder device bindings and receipts"));
    const wchar_t *participant_columns[]={nova_text(L"设备",L"Device"),nova_text(L"设备上的目录",L"Folder on device"),nova_text(L"服务器接收记录",L"Server receipts")};
    for(int i=0;i<3;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT;c.pszText=(LPWSTR)participant_columns[i];ListView_SetColumn(participants,i,&c);}
    SetWindowTextW(GetDlgItem(window,C_RECOVERY),nova_text(L"打开恢复副本",L"Open recovery copies"));
    SetWindowTextW(changes,nova_text(L"文件版本与冲突",L"File versions and conflicts"));
    const wchar_t *change_columns[]={nova_text(L"文件",L"File"),nova_text(L"本机版本",L"Local version"),nova_text(L"服务器版本",L"Server version"),nova_text(L"状态",L"Status"),nova_text(L"版本来源设备",L"Version source")};
    for(int i=0;i<5;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT;c.pszText=(LPWSTR)change_columns[i];ListView_SetColumn(changes,i,&c);}
    const wchar_t *history_columns[]={nova_text(L"同步目录",L"Folder"),nova_text(L"发送内容",L"Sent content"),nova_text(L"接收内容",L"Received content"),nova_text(L"复用内容",L"Reused content"),nova_text(L"结果",L"Result"),nova_text(L"操作",L"Action")};
    for(int i=0;i<3;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)device_columns[i];
        ListView_SetColumn(devices,i,&c);
        }for(int i=0;i<6;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)pair_columns[i];
        ListView_SetColumn(pairs,i,&c);
        }for(int i=0;i<6;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)history_columns[i];
        ListView_SetColumn(history,i,&c);
        }populate_pairs();
    controls();
}
static void show_control(int id,BOOL show){ShowWindow(GetDlgItem(window,id),show?SW_SHOW:SW_HIDE);}
static void layout(void){
    RECT r;GetClientRect(window,&r);
    int w=MulDiv(r.right,96,dpi),h=MulDiv(r.bottom,96,dpi);
    move(tab,24,16,w-48,32);
    move(server_text,26,64,w-180,24);
    move(GetDlgItem(window,C_REFRESH),w-134,58,108,32);
    move(heading,26,106,w-52,32);
    const wchar_t *titles[]={settings.registered?nova_text(L"设备",L"Devices"):nova_text(L"2  注册本设备",L"2  Register this device"),
        editing?(selected==new_index?(subscribing?nova_text(L"订阅目录",L"Subscribe folder"):nova_text(L"发布目录",L"Publish folder")):nova_text(L"编辑目录",L"Edit folder")):nova_text(L"同步目录",L"Sync folders"),
        nova_text(L"传输记录",L"Transfers"),settings.server_id?nova_text(L"服务器",L"Server"):nova_text(L"1  配置服务器",L"1  Configure server")};
    SetWindowTextW(heading,titles[page]);
    const int hidden[]={C_ROLE,C_PROJECT,C_BROKER,C_GROUP,C_RUN};
    for(size_t i=0;i<sizeof(hidden)/sizeof(hidden[0]);i++)show_control(hidden[i],FALSE);
    for(int i=0;i<9;i++)ShowWindow(labels[i],SW_HIDE);
    const int device_ids[]={C_SERVER,C_NAME,C_REGISTER,C_IMPORT,C_DEVICES};
    for(size_t i=0;i<sizeof(device_ids)/sizeof(device_ids[0]);i++)show_control(device_ids[i],page==0);
    ShowWindow(labels[0],page==0?SW_SHOW:SW_HIDE);
    move(labels[0],26,165,110,24);move(name_edit,140,158,w-430,34);
    move(GetDlgItem(window,C_REGISTER),w-270,158,244,34);
    move(devices,26,218,w-52,h-410);
    move(GetDlgItem(window,C_SERVER),26,h-174,180,32);
    move(GetDlgItem(window,C_IMPORT),220,h-174,160,32);
    ListView_SetColumnWidth(devices,0,px(w-420));
    ListView_SetColumnWidth(devices,1,px(200));ListView_SetColumnWidth(devices,2,px(160));

    const int pair_ids[]={C_PAIRS,C_NEW,C_MY_FOLDERS,C_CATALOG};
    for(size_t i=0;i<sizeof(pair_ids)/sizeof(pair_ids[0]);i++)show_control(pair_ids[i],page==1&&!editing);
    move(GetDlgItem(window,C_MY_FOLDERS),26,152,150,34);move(GetDlgItem(window,C_CATALOG),186,152,180,34);
    move(GetDlgItem(window,C_NEW),w-186,152,160,34);
    BOOL local=page==1&&!catalog_mode&&!editing;
    const int local_ids[]={C_REMOVE,C_EDIT,C_AUTO,C_BIND,C_CHECK_ALL,C_SYNC_ALL,C_PUSH_ALL,C_PULL_ALL,C_SCOPE};
    for(size_t i=0;i<sizeof(local_ids)/sizeof(local_ids[0]);i++)show_control(local_ids[i],local);
    show_control(C_SUBSCRIBE,page==1&&catalog_mode&&!editing);
    move(GetDlgItem(window,C_EDIT),26,198,74,32);move(GetDlgItem(window,C_REMOVE),110,198,150,32);
    move(GetDlgItem(window,C_AUTO),270,198,174,32);move(GetDlgItem(window,C_BIND),454,198,174,32);
    move(GetDlgItem(window,C_CHECK_ALL),w-156,198,130,32);
    move(scope_text,26,244,w-500,28);
    move(GetDlgItem(window,C_PUSH_ALL),w-456,240,136,32);move(GetDlgItem(window,C_PULL_ALL),w-310,240,136,32);move(GetDlgItem(window,C_SYNC_ALL),w-164,240,138,32);
    move(GetDlgItem(window,C_SUBSCRIBE),w-216,212,190,34);
    int table_height=editing?max(90,(h-560)/2):max(110,(h-490)/2);
    move(pairs,26,284,w-52,table_height);
    ListView_SetColumnWidth(pairs,0,px(140));ListView_SetColumnWidth(pairs,1,px(w-712));
    ListView_SetColumnWidth(pairs,2,px(140));ListView_SetColumnWidth(pairs,3,px(180));
    ListView_SetColumnWidth(pairs,4,px(110));ListView_SetColumnWidth(pairs,5,px(90));
    if(catalog_mode){ListView_SetColumnWidth(pairs,0,px(w-392));ListView_SetColumnWidth(pairs,1,0);ListView_SetColumnWidth(pairs,2,px(180));ListView_SetColumnWidth(pairs,3,px(160));ListView_SetColumnWidth(pairs,4,0);ListView_SetColumnWidth(pairs,5,0);}
    int y=editing?222:300+table_height;
    const int edit_ids[]={C_PAIR_NAME,C_PATH,C_BROWSE,C_SAVE,C_ADVANCED,C_CANCEL_EDIT};
    for(size_t i=0;i<sizeof(edit_ids)/sizeof(edit_ids[0]);i++)show_control(edit_ids[i],page==1&&editing);
    ShowWindow(labels[4],page==1&&editing?SW_SHOW:SW_HIDE);ShowWindow(labels[6],page==1&&editing?SW_SHOW:SW_HIDE);
    move(labels[4],26,y+6,100,24);move(pair_name,140,y,w-166,32);
    move(labels[6],26,y+50,110,24);move(path_edit,140,y+44,w-326,32);
    move(GetDlgItem(window,C_BROWSE),w-176,y+44,150,32);
    move(GetDlgItem(window,C_ADVANCED),26,y+90,150,32);
    move(GetDlgItem(window,C_CANCEL_EDIT),w-410,y+90,164,34);
    move(GetDlgItem(window,C_SAVE),w-236,y+90,210,34);
    show_control(C_FILTER,page==1&&editing&&advanced);show_control(C_DELETE,page==1&&editing&&advanced);
    ShowWindow(labels[7],page==1&&editing&&advanced?SW_SHOW:SW_HIDE);
    move(labels[7],26,y+140,110,24);move(filter_edit,140,y+134,w-166,32);
    move(delete_check,26,y+177,w-52,28);
    BOOL detail=page==1&&!editing,files=detail&&!catalog_mode&&!devices_mode;
    show_control(C_DETAIL,page==1);show_control(C_FILES_VIEW,detail&&!catalog_mode);show_control(C_DEVICES_VIEW,detail);
    show_control(C_CHANGES,files);show_control(C_PARTICIPANTS,detail&&!files);
    show_control(C_LOCAL,files);show_control(C_REMOTE,files);
    move(detail_text,26,editing?156:y,w-52,48);
    move(GetDlgItem(window,C_FILES_VIEW),26,y+56,144,32);move(GetDlgItem(window,C_DEVICES_VIEW),180,y+56,144,32);
    move(GetDlgItem(window,C_LOCAL),w-396,y+56,175,32);move(GetDlgItem(window,C_REMOTE),w-211,y+56,185,32);
    move(changes,26,y+100,w-52,max(50,h-y-210));move(participants,26,y+100,w-52,max(50,h-y-210));
    ListView_SetColumnWidth(changes,0,px(w-722));ListView_SetColumnWidth(changes,1,px(180));
    ListView_SetColumnWidth(changes,2,px(180));ListView_SetColumnWidth(changes,3,px(160));ListView_SetColumnWidth(changes,4,px(150));
    ListView_SetColumnWidth(participants,0,px(210));ListView_SetColumnWidth(participants,1,px(w-612));ListView_SetColumnWidth(participants,2,px(350));
    show_control(C_DETAIL_EMPTY,detail&&(files?ListView_GetItemCount(changes):ListView_GetItemCount(participants))==0);
    move(detail_empty,50,y+136,w-100,40);
    ShowWindow(empty_text,page==1&&!editing&&!ListView_GetItemCount(pairs)?SW_SHOW:SW_HIDE);
    SetWindowTextW(empty_text,catalog_mode?nova_text(L"暂无可订阅目录。请刷新，或在另一台设备发布目录。",L"No available folders. Refresh, or publish from another device."):nova_text(L"发布本机目录，或切换到“可订阅目录”接入其他设备。",L"Publish a local folder, or open Available folders to subscribe."));
    move(empty_text,56,326,w-112,36);

    show_control(C_LOG,page==2);move(history,26,202,w-52,h-326);
    ListView_SetColumnWidth(history,0,px(w-792));ListView_SetColumnWidth(history,1,px(120));ListView_SetColumnWidth(history,2,px(120));
    ListView_SetColumnWidth(history,3,px(120));ListView_SetColumnWidth(history,4,px(260));ListView_SetColumnWidth(history,5,px(120));
    wchar_t recovery_path[SYNC_PATH_CAP];swprintf(recovery_path,SYNC_PATH_CAP,L"%ls\\sync\\client\\recovery",store_directory());
    DWORD recovery_attr=GetFileAttributesW(recovery_path);show_control(C_RECOVERY,page==2&&recovery_attr!=INVALID_FILE_ATTRIBUTES&&(recovery_attr&FILE_ATTRIBUTE_DIRECTORY));
    move(GetDlgItem(window,C_RECOVERY),26,156,230,34);
    const int admin_ids[]={C_TARGET,C_DEPLOY,C_TARGET_TEXT,C_ADMIN_NOTE,C_SERVER_ADVANCED};
    for(size_t i=0;i<sizeof(admin_ids)/sizeof(admin_ids[0]);i++)show_control(admin_ids[i],page==3);
    const int advanced_ids[]={C_TARGET_PROJECT,C_TARGET_ROOT,C_QUOTA,C_RESERVE,C_PROBE,C_ATTACH,C_MIGRATE};
    for(size_t i=0;i<sizeof(advanced_ids)/sizeof(advanced_ids[0]);i++)show_control(advanced_ids[i],page==3&&server_advanced);
    show_control(C_RESTORE,page==3&&migration_pending);show_control(C_FOLLOW,page==3&&server_route!=NULL);
    for(int i=0;i<4;i++)ShowWindow(admin_labels[i],page==3&&server_advanced?SW_SHOW:SW_HIDE);
    move(admin_note,26,156,w-52,48);
    move(GetDlgItem(window,C_TARGET),26,218,240,36);move(target_text,284,224,w-310,30);
    move(GetDlgItem(window,C_DEPLOY),26,272,210,38);
    move(GetDlgItem(window,C_SERVER_ADVANCED),250,272,160,38);
    move(admin_labels[0],26,340,200,24);move(target_project_edit,240,334,w-266,32);
    move(admin_labels[1],26,384,200,24);move(target_root_edit,240,378,w-266,32);
    move(admin_labels[2],26,428,200,24);move(quota_edit,240,422,100,32);
    move(admin_labels[3],380,428,200,24);move(reserve_edit,600,422,100,32);
    move(GetDlgItem(window,C_PROBE),26,478,180,34);move(GetDlgItem(window,C_ATTACH),220,478,240,34);
    move(GetDlgItem(window,C_MIGRATE),474,478,240,34);
    move(GetDlgItem(window,C_RESTORE),26,532,210,34);move(GetDlgItem(window,C_FOLLOW),250,532,260,34);
    ShowWindow(cloud_text,page==0||page==3?SW_SHOW:SW_HIDE);move(cloud_text,26,h-124,w-52,24);
    move(progress,26,h-90,w-52,5);
    move(status_text,26,h-70,w-350,58);
    show_control(C_PAUSE,job!=NULL);move(GetDlgItem(window,C_PAUSE),w-310,h-64,130,34);
    move(GetDlgItem(window,C_CLOSE),w-166,h-64,140,34);
    render_folder_detail();
    controls();
}
static LRESULT CALLBACK window_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{window=h;
            HDC dc=GetDC(h);
            dpi=GetDeviceCaps(dc,LOGPIXELSX);
            ReleaseDC(h,dc);
            font=CreateFontW(-px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            title_font=CreateFontW(-px(20),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            background=CreateSolidBrush(RGB(14,19,29));
            panel=CreateSolidBrush(RGB(22,29,42));
        tab=control(WC_TABCONTROLW,L"",WS_TABSTOP|TCS_OWNERDRAWFIXED|TCS_FIXEDWIDTH,C_TAB);
            SetWindowSubclass(tab,tab_paint,1,0);
            SendMessageW(tab,TCM_SETITEMSIZE,0,MAKELPARAM(px(168),px(28)));
            for(int i=0;i<4;i++){TCITEMW t={0};
                t.mask=TCIF_TEXT;
                t.pszText=L"";
                TabCtrl_InsertItem(tab,i,&t);
                }server_text=control(L"STATIC",nova_text(L"选择 NOVA 已保存的 SSH 服务器；不使用额外本地程序。",L"Choose a saved NOVA SSH server; no additional local program is used."),0,C_SERVER_TEXT);
            cloud_text=control(L"STATIC",nova_text(L"尚未连接云端",L"Cloud not connected"),0,C_CLOUD);
        button(C_SERVER,L"");
            name_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_NAME);
            role_combo=control(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST,C_ROLE);
            button(C_REGISTER,L"");
            button(C_REFRESH,L"");
            button(C_IMPORT,L"");
            project_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,C_PROJECT);
            broker_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,C_BROKER);
            button(C_TARGET,L"");
            button(C_PROBE,L"");
            button(C_DEPLOY,L"");
            button(C_ATTACH,L"");
            button(C_MIGRATE,L"");
            button(C_RESTORE,L"");
            button(C_FOLLOW,L"");
            target_project_edit=control(L"EDIT",settings.project,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_TARGET_PROJECT);
            target_root_edit=control(L"EDIT",settings.broker,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_TARGET_ROOT);
            quota_edit=control(L"EDIT",L"15",WS_TABSTOP|WS_BORDER|ES_NUMBER,C_QUOTA);
            reserve_edit=control(L"EDIT",L"5",WS_TABSTOP|WS_BORDER|ES_NUMBER,C_RESERVE);
            target_text=control(L"STATIC",L"",0,C_TARGET_TEXT);
            admin_note=control(L"STATIC",nova_text(L"首次使用：选择服务器 → 一键部署或连接已有队列 → 设备登记 → 目录配对。\r\n迁移会冻结旧端，校验全部记录和待接收内容后切换；中断可继续，旧端数据保留。\r\n服务器需支持 SSH 和 Python 3.9+，本机无需脚本或 Python。",L"First use: choose server → deploy or connect → register device → pair folders.\r\nMigration freezes the source and verifies records and pending content before cutover. Resume after interruption; source data is retained.\r\nRequires SSH and Python 3.9+ on the server; no local scripts or Python."),0,C_ADMIN_NOTE);
            for(int i=0;i<4;i++)admin_labels[i]=control(L"STATIC",L"",0,6250+i);
            SendMessageW(target_project_edit,EM_SETLIMITTEXT,SYNC_PATH_CAP-1,0);
            SendMessageW(target_root_edit,EM_SETLIMITTEXT,SYNC_PATH_CAP-1,0);
            SendMessageW(quota_edit,EM_SETLIMITTEXT,7,0);
            SendMessageW(reserve_edit,EM_SETLIMITTEXT,7,0);
            candidate_id=settings.server_id;
        devices=table(C_DEVICES);
            add_column(devices,0,L"",90);
            add_column(devices,1,L"",380);
            add_column(devices,2,L"",190);
            pairs=table(C_PAIRS);
            ListView_SetExtendedListViewStyle(pairs,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_CHECKBOXES);
            add_column(pairs,0,L"",170);
            add_column(pairs,1,L"",340);
            add_column(pairs,2,L"",340);
            add_column(pairs,3,L"",120);
            add_column(pairs,4,L"",110);add_column(pairs,5,L"",90);
        button(C_NEW,L"");
            button(C_REMOVE,L"");
            button(C_RUN,L"");
            button(C_AUTO,L"");
            button(C_PAUSE,L"");
            const int extra_buttons[]={C_CHECK_ALL,C_PUSH_ALL,C_PULL_ALL,C_SYNC_ALL,C_SUBSCRIBE,C_EDIT,C_ADVANCED,C_SERVER_ADVANCED,C_LOCAL,C_REMOTE,C_RECOVERY,C_MY_FOLDERS,C_CATALOG,C_FILES_VIEW,C_DEVICES_VIEW,C_CANCEL_EDIT,C_BIND};
            for(size_t i=0;i<sizeof(extra_buttons)/sizeof(extra_buttons[0]);i++)button(extra_buttons[i],L"");
            changes=table(C_CHANGES);for(int i=0;i<5;i++)add_column(changes,i,L"",200);
            participants=table(C_PARTICIPANTS);for(int i=0;i<3;i++)add_column(participants,i,L"",200);
            detail_text=control(L"STATIC",L"",0,C_DETAIL);
            scope_text=control(L"STATIC",L"",0,C_SCOPE);
            detail_empty=control(L"STATIC",L"",SS_CENTER,C_DETAIL_EMPTY);
            heading=control(L"STATIC",L"",0,C_HEADING);SendMessageW(heading,WM_SETFONT,(WPARAM)title_font,TRUE);
            empty_text=control(L"STATIC",L"",SS_CENTER,C_EMPTY);
            pair_name=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_PAIR_NAME);
            group_combo=control(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST,C_GROUP);
            path_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_PATH);
            filter_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_FILTER);
            button(C_BROWSE,L"");
            delete_check=control(L"BUTTON",L"",WS_TABSTOP|BS_AUTOCHECKBOX,C_DELETE);
            button(C_SAVE,L"");
        history=table(C_LOG);
            for(int i=0;i<6;i++)add_column(history,i,L"",i==0?240:i==4?300:140);
            for(int i=0;i<9;i++)labels[i]=control(L"STATIC",L"",0,6200+i);
            progress=control(PROGRESS_CLASSW,L"",PBS_SMOOTH,C_PROGRESS);
            SendMessageW(progress,PBM_SETRANGE,0,MAKELPARAM(0,100));
            SetWindowTheme(progress,L"",L"");
            SendMessageW(progress,PBM_SETBKCOLOR,0,RGB(22,29,42));
            SendMessageW(progress,PBM_SETBARCOLOR,0,RGB(90,125,190));
            status_text=control(L"STATIC",L"",0,6300);
            button(C_CLOSE,L"");
        SendMessageW(name_edit,EM_SETLIMITTEXT,100,0);
            SendMessageW(pair_name,EM_SETLIMITTEXT,SYNC_NAME_CAP-1,0);
            SendMessageW(path_edit,EM_SETLIMITTEXT,SYNC_PATH_CAP-1,0);
            SendMessageW(filter_edit,EM_SETLIMITTEXT,SYNC_FILTER_CAP-1,0);
            SendMessageW(project_edit,EM_SETLIMITTEXT,SYNC_PATH_CAP-1,0);
            SendMessageW(broker_edit,EM_SETLIMITTEXT,SYNC_PATH_CAP-1,0);
        BOOL dark=TRUE;
            DwmSetWindowAttribute(h,20,&dark,sizeof(dark));
            localize();
            pending_server();
            server_labels();
            populate_pairs();
            select_pair(settings.count?0:-1);
            page=settings.registered?1:settings.server_id?0:3;TabCtrl_SetCurSel(tab,page);
            for(int i=0;i<settings.count;i++)folder_checked[i]=TRUE;
            populate_pairs();layout();
            text(nova_text(L"就绪",L"Ready"));
            SetTimer(h,AUTO_TIMER,1000,NULL);
            return 0;
            }
    case WM_SIZE:layout();
        return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;
            MONITORINFO monitor={sizeof(monitor),{0},{0},0};
            GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);
            m->ptMinTrackSize.x=min(px(1000),monitor.rcWork.right-monitor.rcWork.left);
            m->ptMinTrackSize.y=min(px(740),monitor.rcWork.bottom-monitor.rcWork.top);
            return 0;
            }
    case WM_NOTIFY:{NMHDR *n=(NMHDR*)lp;
        HWND lists[]={devices,pairs,history,changes,participants};
        for(int i=0;i<5;i++){
            if(n->hwndFrom==lists[i]&&n->code==NM_CUSTOMDRAW){NMLVCUSTOMDRAW *d=(NMLVCUSTOMDRAW*)lp;
                    if(d->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                    if(d->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){d->clrText=RGB(236,241,249);
                        d->clrTextBk=ListView_GetItemState(lists[i],(int)d->nmcd.dwItemSpec,LVIS_SELECTED)?RGB(53,77,122):RGB(22,29,42);
                        d->nmcd.uItemState&=~CDIS_SELECTED;
                        return CDRF_NEWFONT;
                        }}
            if(n->hwndFrom==ListView_GetHeader(lists[i])&&n->code==NM_CUSTOMDRAW){NMCUSTOMDRAW *d=(NMCUSTOMDRAW*)lp;
                    if(d->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                    if(d->dwDrawStage==CDDS_ITEMPREPAINT){FillRect(d->hdc,&d->rc,panel);
                        wchar_t label[128];
                        HDITEMW item={0};
                        item.mask=HDI_TEXT;
                        item.pszText=label;
                        item.cchTextMax=128;
                        Header_GetItem(n->hwndFrom,(int)d->dwItemSpec,&item);
                        RECT area=d->rc;
                        area.left+=px(8);
                        SetBkMode(d->hdc,TRANSPARENT);
                        SetTextColor(d->hdc,RGB(236,241,249));
                        HFONT old=SelectObject(d->hdc,font);
                        DrawTextW(d->hdc,label,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
                        SelectObject(d->hdc,old);
                        return CDRF_SKIPDEFAULT;
                        }}
        }

            if(n->hwndFrom==tab&&n->code==TCN_SELCHANGE){page=TabCtrl_GetCurSel(tab);
                layout();
                }if(n->hwndFrom==pairs&&n->code==LVN_ITEMCHANGED&&!refreshing&&!job){NMLISTVIEW *event=(NMLISTVIEW*)lp;
                if(event->iItem>=0){LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=event->iItem;ListView_GetItem(pairs,&item);
                    if(item.lParam>=0&&item.lParam<settings.count)folder_checked[item.lParam]=ListView_GetCheckState(pairs,event->iItem);
                }
                int index=ListView_GetNextItem(pairs,-1,LVNI_SELECTED);
                if(index>=0){LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=index;ListView_GetItem(pairs,&item);
                    if(item.lParam>=0&&item.lParam<settings.count){
                        if(item.lParam!=selected){editing=FALSE;select_pair((int)item.lParam);layout();}
                        if(folder_codes[item.lParam]!=SYNC_OK){wchar_t detail[2048];failure_message(detail,folder_codes[item.lParam],folder_details[item.lParam]);text(detail);}
                    }
                    else if(item.lParam<0){selected=-1;catalog_choice=(int)-item.lParam-1;editing=FALSE;render_changes();controls();layout();}
                }
                render_folder_detail();controls();
                }
            if(n->hwndFrom==changes&&n->code==LVN_ITEMCHANGED&&!refreshing)controls();
            if(n->hwndFrom==history&&(n->code==LVN_ITEMCHANGED||n->code==NM_DBLCLK)){
                int index=ListView_GetNextItem(history,-1,LVNI_SELECTED);
                if(index>=0&&index<24&&history_codes[index]!=SYNC_OK){wchar_t detail[2048];failure_message(detail,history_codes[index],history_details[index]);text(detail);
                    if(n->code==NM_DBLCLK)MessageBoxW(window,detail,nova_text(L"传输详情",L"Transfer details"),MB_OK|MB_ICONINFORMATION);
                }
            }
            if(n->hwndFrom==pairs&&n->code==NM_DBLCLK&&!job){if(catalog_choice>=0)subscribe_folder();else if(selected>=0){editing=TRUE;layout();}}
            return 0;
            }
    case WM_COMMAND:switch(LOWORD(wp)){
        case C_SERVER:page=3;
            TabCtrl_SetCurSel(tab,3);
            layout();
            controls();
            break;
        case C_TARGET:select_server();
            break;
        case C_SERVER_ADVANCED:server_advanced=!server_advanced;layout();break;
        case C_ADVANCED:advanced=!advanced;layout();break;
        case C_MY_FOLDERS:case C_CATALOG:
            if(job||editing)break;
            catalog_mode=LOWORD(wp)==C_CATALOG;catalog_choice=-1;
            select_pair(catalog_mode?-1:settings.count?0:-1);populate_pairs();layout();
            if(catalog_mode&&ListView_GetItemCount(pairs))ListView_SetItemState(pairs,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
            break;
        case C_FILES_VIEW:case C_DEVICES_VIEW:devices_mode=LOWORD(wp)==C_DEVICES_VIEW;render_folder_detail();layout();break;
        case C_CANCEL_EDIT:editing=advanced=FALSE;new_index=-1;select_pair(selected<settings.count?selected:settings.count?0:-1);populate_pairs();layout();break;
        case C_BIND:start_job(SYNC_JOB_BIND,TRUE);break;
        case C_EDIT:editing=!editing;layout();break;
        case C_SUBSCRIBE:subscribe_folder();break;
        case C_CHECK_ALL:start_batch(SYNC_JOB_CHECK);break;
        case C_SYNC_ALL:start_batch(SYNC_JOB_RUN);break;
        case C_PUSH_ALL:start_batch(SYNC_JOB_PUSH);break;
        case C_PULL_ALL:start_batch(SYNC_JOB_PULL);break;
        case C_LOCAL:case C_REMOTE:resolve_choice=LOWORD(wp)==C_LOCAL?1:2;start_job(SYNC_JOB_RESOLVE,TRUE);break;
        case C_RECOVERY:{wchar_t path[SYNC_PATH_CAP];int n=swprintf(path,SYNC_PATH_CAP,L"%ls\\sync\\client\\recovery",store_directory());
            if(n>0&&n<SYNC_PATH_CAP)ShellExecuteW(window,L"open",path,NULL,NULL,SW_SHOWNORMAL);
            break;
        }
        case C_PROBE:start_job(SYNC_JOB_SERVER_PROBE,TRUE);
            break;
        case C_DEPLOY:start_job(SYNC_JOB_SERVER_DEPLOY,TRUE);
            break;
        case C_ATTACH:start_job(SYNC_JOB_SERVER_ATTACH,TRUE);
            break;
        case C_MIGRATE:start_job(SYNC_JOB_SERVER_MIGRATE,TRUE);
            break;
        case C_RESTORE:start_job(SYNC_JOB_SERVER_RESTORE,TRUE);
            break;
        case C_FOLLOW:follow_server();
            break;
        case C_TARGET_PROJECT:if(HIWORD(wp)==EN_CHANGE&&!job&&target_root_edit){wchar_t value[SYNC_PATH_CAP],root[SYNC_PATH_CAP];
                GetWindowTextW(target_project_edit,value,SYNC_PATH_CAP);
                int n=swprintf(root,SYNC_PATH_CAP,L"%ls/runtime/broker",value);
                if(n>0&&n<SYNC_PATH_CAP)SetWindowTextW(target_root_edit,root);
                }break;
            case C_REGISTER:if(save_identity())start_job(SYNC_JOB_REGISTER,TRUE);
            break;
            case C_REFRESH:if(save_identity())start_job(SYNC_JOB_REFRESH,TRUE);
            break;
            case C_IMPORT:import_legacy();
            break;
            case C_NEW:new_pair();
            break;
            case C_SAVE:save_pair();
            break;
            case C_BROWSE:browse_folder();
            break;
        case C_REMOVE:if(!job&&selected>=0&&selected==new_index&&selected==settings.count){new_index=-1;
                select_pair(-1);
                break;
                }if(!job&&selected>=0&&selected<settings.count){
            if(settings.registered&&settings.server_id){start_job(SYNC_JOB_UNBIND,TRUE);
                    }
            else {SyncSettings next=settings;
                    sync_pair_remove(&next,selected);
                    if(persist(&next)){new_index=-1;
                        forget_folder_view(selected);
                        selected=min(selected,settings.count-1);
                        populate_pairs();
                        select_pair(selected);
                        text(nova_text(L"已移除绑定；本机和云端文件均未删除。",L"Binding removed; no local or server files were deleted."));
                        }}
        }break;
        case C_RUN:if(selected>=0&&settings.registered)start_job(SYNC_JOB_RUN,TRUE);
            break;
        case C_AUTO:if(selected>=0&&selected<settings.count&&!job){SyncSettings next=settings;
                next.pairs[selected].paused=!next.pairs[selected].paused;
                if(persist(&next)){last_run[selected]=0;
                    populate_pairs();
                    controls();
                    }}break;
        case C_PAUSE:batch_action=0;if(job){nova_log_write(NOVA_LOG_WARN,"sync_ui","cancel_requested",L"op=%ld",job->log.id);SetEvent(job->cancel);}controls();break;
        case C_CLOSE:SendMessageW(h,WM_CLOSE,0,0);
            break;
        }return 0;
    case PROGRESS_MESSAGE:if(job){SyncProgress p;
            EnterCriticalSection(&job->lock);
            p=job->latest;
            InterlockedExchange(&job->posted,0);
            LeaveCriticalSection(&job->lock);
            wchar_t value[2048],sent[40],received[40],reused[40];
            bytes(sent,40,p.stats.sent);
            bytes(received,40,p.stats.received);
            bytes(reused,40,p.stats.reused);
            swprintf(value,2048,nova_text(L"%ls · %.1400ls\r\n发送 %ls · 接收 %ls · 复用 %ls",L"%ls · %.1400ls\r\nSent %ls · Received %ls · Reused %ls"),phase_name(p.phase),p.path,sent,received,reused);
            text(value);
            SendMessageW(progress,PBM_SETPOS,p.total?(WPARAM)(100.0*(double)p.done/(double)p.total):0,0);
            }return 0;
    case RECYCLE_MESSAGE:recycle_on_ui();
        return 0;
    case DONE_MESSAGE:if(!shutting_down)finish_job();
        return 0;
    case WM_TIMER:if(wp==AUTO_TIMER&&!job&&!batch_action&&!editing&&ready&&settings.registered&&!shutting_down&&!migration_pending){for(int i=0;i<settings.count;i++)if(!settings.pairs[i].paused&&GetTickCount64()-last_run[i]>=(ULONGLONG)settings.interval*1000){
                if(!start_folder_job(SYNC_JOB_RUN,i,FALSE)){SyncSettings next=settings;
                    next.pairs[i].paused=true;
                    persist(&next);
                    }break;
                }}return 0;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
            wchar_t label[160]=L"";
            BOOL selected_state=(d->itemState&ODS_SELECTED)!=0;
        if(d->CtlType==ODT_TAB){TCITEMW item={0};
                item.mask=TCIF_TEXT;
                item.pszText=label;
                item.cchTextMax=160;
                TabCtrl_GetItem(tab,(int)d->itemID,&item);
                selected_state=TabCtrl_GetCurSel(tab)==(int)d->itemID;
                }
        else if(d->CtlType==ODT_BUTTON)GetWindowTextW(d->hwndItem,label,160);
            else break;
        BOOL primary=d->CtlID==C_SYNC_ALL||d->CtlID==C_DEPLOY||d->CtlID==C_REGISTER||d->CtlID==C_SAVE;
        if((d->CtlID==C_MY_FOLDERS&&!catalog_mode)||(d->CtlID==C_CATALOG&&catalog_mode)||(d->CtlID==C_FILES_VIEW&&!devices_mode&&!catalog_mode)||(d->CtlID==C_DEVICES_VIEW&&(devices_mode||catalog_mode)))selected_state=TRUE;
        COLORREF color=selected_state?RGB(53,77,122):primary&&!(d->itemState&ODS_DISABLED)?RGB(42,96,175):RGB(25,34,49);
            HBRUSH brush=CreateSolidBrush(color);
            FillRect(d->hDC,&d->rcItem,brush);
            DeleteObject(brush);
        SetBkMode(d->hDC,TRANSPARENT);
            SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?RGB(105,117,136):RGB(236,241,249));
            HFONT old=SelectObject(d->hDC,font);
            RECT area=d->rcItem;
            DrawTextW(d->hDC,label,-1,&area,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            if(d->itemState&ODS_FOCUS){InflateRect(&area,-3,-3);
                DrawFocusRect(d->hDC,&area);
                }SelectObject(d->hDC,old);
            return TRUE;
    }
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,RGB(236,241,249));SetBkMode((HDC)wp,TRANSPARENT);return (LRESULT)background;
    case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,RGB(236,241,249));
        SetBkColor((HDC)wp,RGB(22,29,42));
        return (LRESULT)panel;
    case WM_ERASEBKGND:{RECT r;
            GetClientRect(h,&r);
            FillRect((HDC)wp,&r,background);
            return 1;
            }
    case WM_CLOSE:ShowWindow(h,SW_HIDE);
        return 0;
    case WM_DESTROY:KillTimer(h,AUTO_TIMER);
        DeleteObject(font);
        DeleteObject(title_font);
        DeleteObject(background);
        DeleteObject(panel);
        window=NULL;
        return 0;
    }return DefWindowProcW(h,message,wp,lp);
}
BOOL sync_manager_open(HWND parent){
    if(window){ShowWindow(window,SW_SHOWNORMAL);
        SetForegroundWindow(window);
        return TRUE;
        }owner=parent;
    if(!loaded){if(!sync_settings_load(&settings))return FALSE;
        loaded=TRUE;
        }
    if(!settings.node_id[0])new_id(settings.node_id);
    if(!settings.name[0]){DWORD size=SYNC_NAME_CAP;
        if(!GetComputerNameW(settings.name,&size))lstrcpyW(settings.name,nova_text(L"本机电脑",L"This computer"));
    }
    wchar_t version[16];
    store_get(L"sync",L"Manager",L"Version",L"",version,16);
    if(!version[0]){wchar_t imported[MAX_PATH];
        swprintf(imported,MAX_PATH,L"%ls\\sync\\import.json",store_directory());
        SyncSettings *legacy=malloc(sizeof(*legacy));
        if(legacy&&GetFileAttributesW(imported)!=INVALID_FILE_ATTRIBUTES&&sync_settings_import(imported,legacy)){
            if(!sync_settings_save(legacy)){free(legacy);
                return FALSE;
                }
            settings=*legacy;
            }free(legacy);
        }
    runtime=ssh_runtime_acquire();
    if(!runtime)return FALSE;
    ready=TRUE;
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_TAB_CLASSES|ICC_PROGRESS_CLASS|ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&ic);
    WNDCLASSEXW wc={0};
    wc.cbSize=sizeof(wc);
    wc.hInstance=GetModuleHandleW(NULL);
    wc.lpfnWndProc=window_proc;
    wc.lpszClassName=SYNC_CLASS;
    wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
    wc.hIcon=(HICON)SendMessageW(parent,WM_GETICON,ICON_BIG,0);
    wc.hIconSm=(HICON)SendMessageW(parent,WM_GETICON,ICON_SMALL,0);
    RegisterClassExW(&wc);
    HDC dc=GetDC(NULL);
    dpi=GetDeviceCaps(dc,LOGPIXELSX);
    ReleaseDC(NULL,dc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=min(px(1140),work.right-work.left),height=min(px(820),work.bottom-work.top);
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,SYNC_CLASS,L"NOVA Desktop",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,parent,NULL,GetModuleHandleW(NULL),NULL);
    if(!h){ssh_runtime_release();
        runtime=FALSE;
        return FALSE;
        }ShowWindow(h,SW_SHOWNORMAL);
    UpdateWindow(h);
    return TRUE;
}
HWND sync_manager_window(void){return window;
    }
void sync_manager_startup(HWND parent){SyncSettings *value=malloc(sizeof(*value));
    if(value&&sync_settings_load(value)){for(int i=0;i<value->count;i++)if(!value->pairs[i].paused){if(sync_manager_open(parent))ShowWindow(window,SW_HIDE);
            break;
            }}free(value);
    }
void sync_manager_language_changed(void){
    if(!window)return;
    wchar_t name[SYNC_NAME_CAP],project[SYNC_PATH_CAP],broker[SYNC_PATH_CAP];
    GetWindowTextW(name_edit,name,SYNC_NAME_CAP);
    GetWindowTextW(project_edit,project,SYNC_PATH_CAP);
    GetWindowTextW(broker_edit,broker,SYNC_PATH_CAP);
    int role=(int)SendMessageW(role_combo,CB_GETCURSEL,0,0);
    localize();
    render_cloud();
    SetWindowTextW(name_edit,name);
    SetWindowTextW(project_edit,project);
    SetWindowTextW(broker_edit,broker);
    SendMessageW(role_combo,CB_SETCURSEL,role,0);
    server_labels();
    render_admin_note();
    render_changes();layout();
    if(!cloud_view)SetWindowTextW(cloud_text,nova_text(L"尚未连接云端",L"Cloud not connected"));
    for(int i=0;i<ListView_GetItemCount(history)&&i<24;i++){ListView_SetItemText(history,i,4,(LPWSTR)(history_codes[i]==SYNC_OK?nova_text(L"完成",L"Completed"):failure_name(history_codes[i])));ListView_SetItemText(history,i,5,(LPWSTR)action_name(history_actions[i]));}
    if(job)PostMessageW(window,PROGRESS_MESSAGE,0,0);
    else text(nova_text(L"就绪",L"Ready"));
}
void sync_manager_shutdown(void){
    shutting_down=TRUE;
    if(job){SetEvent(job->cancel);
        while(WaitForSingleObject(job->worker,0)!=WAIT_OBJECT_0){MSG m;
            while(PeekMessageW(&m,window,PROGRESS_MESSAGE,RECYCLE_MESSAGE,PM_REMOVE)){if(m.message==RECYCLE_MESSAGE)recycle_on_ui();
                }MsgWaitForMultipleObjects(1,&job->worker,FALSE,100,QS_ALLINPUT);
            }finish_job();
        }
    if(window)DestroyWindow(window);
    if(runtime)ssh_runtime_release();
    runtime=FALSE;
    ready=FALSE;
    shutting_down=FALSE;
    loaded=FALSE;
    binding_count=group_count=0;
    catalog_mode=devices_mode=metadata_known=FALSE;
    batch_action=batch_next=0;editing=advanced=server_advanced=FALSE;catalog_choice=-1;
    for(int i=0;i<SYNC_PAIR_LIMIT;i++){free(plans[i]);plans[i]=NULL;folder_checked[i]=inspected[i]=bind_failed[i]=FALSE;pending_push[i]=pending_pull[i]=pending_conflict[i]=pending_wait[i]=0;}
    ZeroMemory(folder_codes,sizeof(folder_codes));ZeroMemory(folder_details,sizeof(folder_details));
    ZeroMemory(history_codes,sizeof(history_codes));ZeroMemory(history_details,sizeof(history_details));batch_error[0]=0;
    free(cloud_view);
    cloud_view=NULL;
    selected=-1;
    new_index=-1;
    candidate_id=0;
    admin_probe_available=FALSE;
    admin_free=0;
    admin_python[0]=admin_phase[0]=0;
    migration_pending=FALSE;
    free(server_route);
    server_route=NULL;
}
