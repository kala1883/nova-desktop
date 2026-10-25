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
#include "sync_manager.h"
#include "ssh_view.h"
#include "../platform/sync_settings.h"
#include "../platform/sync_client.h"
#include "../platform/sync_json.h"
#include "../platform/storage.h"
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
 C_TARGET,C_TARGET_PROJECT,C_TARGET_ROOT,C_QUOTA,C_RESERVE,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW,C_TARGET_TEXT,C_ADMIN_NOTE };
typedef struct CloudGroup { wchar_t id[SYNC_ID_CAP],name[SYNC_NAME_CAP];
    } CloudGroup;
typedef struct CloudBinding { wchar_t dataset[SYNC_ID_CAP],path[SYNC_PATH_CAP];
    int role;
    } CloudBinding;
typedef struct SyncJob {
    SyncRequest request;
    SyncResult result;
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
static CloudBinding bindings[SYNC_PAIR_LIMIT*2];
static int binding_count;
static ULONGLONG last_run[SYNC_PAIR_LIMIT];
static SyncFailure history_codes[24];
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
        case SYNC_SCAN:return nova_text(L"分析内容块",L"Scanning content");
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
        default:return nova_text(L"同步协议校验失败",L"Synchronization protocol validation failed");
        }}
static BOOL persist(const SyncSettings *next){if(!sync_settings_save(next)){text(store_error());
        return FALSE;
        }settings=*next;
    return TRUE;
    }
static void new_id(wchar_t *out){unsigned char data[16];
    sqlite3_randomness(16,data);
    for(int i=0;i<16;i++)swprintf(out+i*2,3,L"%02x",data[i]);
    }
static void select_pair(int index){
    selected=index;
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
    controls();
}
static void populate_pairs(void){
    refreshing=TRUE;
    ListView_DeleteAllItems(pairs);
    for(int i=0;i<settings.count;i++){SyncPair *p=&settings.pairs[i];
        row(pairs,i,p->name);
        ListView_SetItemText(pairs,i,1,p->directory);
        const wchar_t *peer=nova_text(L"等待对端绑定目录",L"Waiting for peer folder");
        for(int j=0;j<binding_count;j++)if(bindings[j].role!=settings.role&&!wcscmp(bindings[j].dataset,p->dataset)){peer=bindings[j].path;
            break;
            }ListView_SetItemText(pairs,i,2,(LPWSTR)peer);
        ListView_SetItemText(pairs,i,3,(LPWSTR)(job&&job->index==i?nova_text(L"同步中",L"Running"):p->paused?nova_text(L"已暂停",L"Paused"):nova_text(L"自动同步",L"Automatic")));
        }
    if(selected>=0&&selected<settings.count)ListView_SetItemState(pairs,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    refreshing=FALSE;
}
typedef struct SyncCloudView {
    CloudGroup groups[SYNC_PAIR_LIMIT];
    CloudBinding bindings[SYNC_PAIR_LIMIT*2];
    wchar_t names[2][SYNC_NAME_CAP];
    wchar_t nodes[2][SYNC_ID_CAP];
    BOOL present[2];
    int groups_count,bindings_count;
    uint64_t stored,quota;
    int64_t pending;
} SyncCloudView;
static SyncCloudView *cloud_view;
static void render_cloud(void){
    if(!cloud_view)return;
    ListView_DeleteAllItems(devices);
    int row_index=0;
    for(int i=0;i<2;i++)if(cloud_view->present[i]){
        wchar_t role[2]={(wchar_t)(L'A'+i),0};
        row(devices,row_index,role);
        ListView_SetItemText(devices,row_index,1,cloud_view->names[i]);
        ListView_SetItemText(devices,row_index,2,(LPWSTR)(settings.registered&&i==settings.role&&!wcscmp(cloud_view->nodes[i],settings.node_id)?nova_text(L"本机",L"This device"):nova_text(L"已登记",L"Registered")));
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
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT json_type(?1,'$.devices')='array' AND json_array_length(?1,'$.devices')<=2 AND json_type(?1,'$.datasets')='array' AND json_array_length(?1,'$.datasets')<=16 AND (json_type(?1,'$.bindings') IS NULL OR (json_type(?1,'$.bindings')='array' AND json_array_length(?1,'$.bindings')<=32))",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        ok=sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_int(s,0)==1;
        }sqlite3_finalize(s);
    s=NULL;
    if(ok)ok=sqlite3_prepare_v2(db,"SELECT value FROM json_each(?1,'$.devices')",-1,&s,NULL)==SQLITE_OK;
    if(ok)sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
    while(ok&&sqlite3_step(s)==SQLITE_ROW){
        const char *record=(const char*)sqlite3_column_text(s,0);
        char *role=sync_json_string(db,record,"$.role"),*name=sync_json_string(db,record,"$.name");
        ok=role&&name&&(!strcmp(role,"A")||!strcmp(role,"B"));
        if(ok){int i=role[0]=='B';
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
        ok=at<SYNC_PAIR_LIMIT*2&&dataset&&path&&role&&(!strcmp(role,"A")||!strcmp(role,"B"))&&sync_wide(dataset,next->bindings[at].dataset,SYNC_ID_CAP)&&sync_identifier(next->bindings[at].dataset)&&sync_wide(path,next->bindings[at].path,SYNC_PATH_CAP);
        if(ok){next->bindings[at].role=role[0]=='B';
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
        int choice=(int)SendMessageW(group_combo,CB_GETCURSEL,0,0);
        if(choice>0&&choice<=group_count)lstrcpyW(choice_id,groups[choice-1].id);
        free(cloud_view);
        cloud_view=next;
        next=NULL;
        group_count=cloud_view->groups_count;
        binding_count=cloud_view->bindings_count;
        memcpy(groups,cloud_view->groups,sizeof(groups));
        memcpy(bindings,cloud_view->bindings,sizeof(bindings));
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
    run_backend(&j->request,j->action,&j->result);
    PostMessageW(j->target,DONE_MESSAGE,0,0);
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
static BOOL start_job(int action,BOOL interactive){
    if(job||!ready)return FALSE;
    if(action<SYNC_JOB_SERVER_PROBE&&migration_pending){text(nova_text(L"请先继续迁移或恢复旧服务器，断点未解决前暂停普通同步。",L"Resume migration or restore the source before ordinary synchronization."));
        return FALSE;
        }
    if(action<SYNC_JOB_SERVER_PROBE&&!settings.server_id){page=3;
        TabCtrl_SetCurSel(tab,3);
        layout();
        text(nova_text(L"请先在服务器页部署或连接服务器，再登记设备。",L"Deploy or connect a server on the Servers page before registering this device."));
        return FALSE;
        }
    if((action==SYNC_JOB_RUN||action==SYNC_JOB_BIND||action==SYNC_JOB_UNBIND)&&(selected<0||selected>=settings.count))return FALSE;
    SyncJob *j=calloc(1,sizeof(*j));
    if(!j)return FALSE;
    j->action=action;
    j->index=(action==SYNC_JOB_RUN||action==SYNC_JOB_BIND||action==SYNC_JOB_UNBIND)?selected:-1;
    j->interactive=interactive;
    j->target=window;
    BOOL admin=action>=SYNC_JOB_SERVER_PROBE;
    if(admin&&action!=SYNC_JOB_SERVER_RESTORE&&!candidate_id){select_server();
        if(!candidate_id){free(j);
            return FALSE;
            }}
    if((!admin||action==SYNC_JOB_SERVER_MIGRATE||action==SYNC_JOB_SERVER_RESTORE)&&!login_backend(interactive,&j->request.login)){SecureZeroMemory(j,sizeof(*j));
        free(j);
        text(nova_text(L"请选择服务器并在本机保存登录信息。自动任务未连接。",L"Select a server and save login details locally. Automatic work did not connect."));
        return FALSE;
        }
    if(admin&&action!=SYNC_JOB_SERVER_RESTORE&&!target_login_backend(candidate_id,interactive,&j->request.target_login)){SecureZeroMemory(j,sizeof(*j));
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
    ResumeThread(j->worker);
    controls();
    populate_pairs();
    text(nova_text(L"正在连接；本机已保存的块和断点会保留。",L"Connecting; locally saved blocks and checkpoints are retained."));
    return TRUE;
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
            if(item)IShellItem_Release(item);
            if(operation)IFileOperation_Release(operation);
        }
    }SetEvent(j->delete_done);
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
                        free(server_route);
                        server_route=NULL;
                        SetWindowTextW(project_edit,settings.project);
                        SetWindowTextW(broker_edit,settings.broker);
                        text(nova_text(L"已连接目标服务器；本机身份、配对和同步记录保留。目录保持原有暂停状态。",L"Destination connected; device identity, folder pairs and records preserved. Paused folders remain paused."));
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
        if(j->result.failure==SYNC_OK){if(j->action==SYNC_JOB_UNBIND&&j->index>=0&&j->index<settings.count){SyncSettings next=settings;
                sync_pair_remove(&next,j->index);
                if(!persist(&next))j->result.failure=SYNC_STORAGE;
                selected=min(selected,settings.count-1);
                new_index=-1;
                }
            if(j->action==SYNC_JOB_REGISTER){SyncSettings next=settings;
                next.registered=true;
                if(!persist(&next))j->result.failure=SYNC_STORAGE;
                }if(j->result.failure!=SYNC_OK)text(failure_name(j->result.failure));
            else if(j->result.cloud&&!load_cloud(j->result.cloud))text(nova_text(L"云端结果超限或不完整，未截断显示。",L"Cloud results exceed limits or are incomplete; no truncated view was installed."));
            else text(nova_text(L"操作完成，云端已确认结果。",L"Operation completed; the cloud confirmed its result."));
            }
        else if(!retry){wchar_t message[2048];
            swprintf(message,2048,L"%ls\r\n%.1000ls%.700ls",failure_name(j->result.failure),j->result.detail,j->result.ssh.error);
            text(message);
            if(j->index>=0&&j->index<settings.count){SyncSettings next=settings;
                next.pairs[j->index].paused=true;
                persist(&next);
                }}
        if(j->action==SYNC_JOB_RUN){memmove(&history_codes[1],&history_codes[0],23*sizeof(history_codes[0]));
            history_codes[0]=j->result.failure;
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
            if(j->index>=0)last_run[j->index]=GetTickCount64();
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
    if(retry)start_job(action,TRUE);
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
        lstrcpyW(active,nova_text(L"当前尚未连接同步服务器；请在服务器页选择并连接。",L"No synchronization server connected; choose and connect on the Servers page."));
        lstrcpyW(target,nova_text(L"尚未选择目标服务器。",L"No destination server selected."));
        for(int i=0;i<servers->count;i++){SshServer *s=&servers->items[i];
            if(s->id==settings.server_id)swprintf(active,768,nova_text(L"当前服务器：%ls · %ls@%ls:%d",L"Current server: %ls · %ls@%ls:%d"),s->name,s->user,s->host,s->port);
            if(s->id==candidate_id)swprintf(target,768,nova_text(L"目标服务器：%ls · %ls@%ls:%d",L"Destination: %ls · %ls@%ls:%d"),s->name,s->user,s->host,s->port);
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
        swprintf(value,512,nova_text(L"检查结果：Python %ls · 可用 %ls · %ls\r\n新建队列使用下方配额；已有队列及迁移保留原配额。",L"Probe: Python %ls · free %ls · %ls\r\nQuotas apply to new queues; existing queues and migrations retain their quotas."),admin_python,size,state);
        SetWindowTextW(admin_note,value);
        return;
        }
    SetWindowTextW(admin_note,nova_text(L"首次使用：选择服务器 → 一键部署或连接已有队列 → 设备登记 → 目录配对。\r\n迁移会冻结旧端，校验全部记录和待接收内容后切换；中断可继续，旧端数据保留。\r\n服务器需支持 SSH 和 Python 3.9+，本机无需脚本或 Python。",L"First use: choose server → deploy or connect → register device → pair folders.\r\nMigration freezes the source and verifies records and pending content before cutover. Resume after interruption; source data is retained.\r\nRequires SSH and Python 3.9+ on the server; no local scripts or Python."));
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
    int role=(int)SendMessageW(role_combo,CB_GETCURSEL,0,0);
    if(settings.registered&&role!=settings.role)return FALSE;
    next.role=role;
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
    lstrcpyW(draft_pair.name,nova_text(L"新同步目录",L"New sync folder"));
    lstrcpyW(draft_pair.group,draft_pair.name);
    lstrcpyW(draft_pair.excludes,L".git;node_modules;.venv;__pycache__;.next;.nova-sync-*;.cloudsync-*;*.tmp");
    draft_pair.paused=true;
    new_index=settings.count;
    refreshing=TRUE;
    ListView_SetItemState(pairs,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    refreshing=FALSE;
    select_pair(new_index);
    text(nova_text(L"填写目录后保存；尚未建立绑定或传输文件。",L"Choose a folder and save; no binding or file transfer has started."));
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
    if(persist(&next)){new_index=-1;
        populate_pairs();
        select_pair(selected);
        text(nova_text(L"配对已保存；保存不上传工作文件。",L"Pair saved; saving does not upload working files."));
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
    if(sync_settings_import(path,next)&&persist(next)){populate_pairs();
        select_pair(settings.count?0:-1);
        localize();
        text(nova_text(L"已导入设备身份和目录；所有目录保持暂停。",L"Device identity and folders imported; every folder remains paused."));
        }else text(nova_text(L"导入格式不兼容或超出容量，原配置未修改。",L"Import is incompatible or over capacity; configuration unchanged."));
    free(next);
    }
static void controls(void){
    BOOL idle=job==NULL,has=selected>=0&&(selected<settings.count||(selected==new_index&&selected==settings.count));
    EnableWindow(pairs,idle);
    const int edit_ids[]={C_SERVER,C_NAME,C_ROLE,C_REGISTER,C_REFRESH,C_IMPORT,C_NEW,C_REMOVE,C_PAIR_NAME,C_GROUP,C_PATH,C_BROWSE,C_FILTER,C_DELETE,C_SAVE,C_RUN,C_AUTO,C_PROJECT,C_BROKER};
    for(size_t i=0;i<sizeof(edit_ids)/sizeof(edit_ids[0]);i++)EnableWindow(GetDlgItem(window,edit_ids[i]),idle);
    EnableWindow(role_combo,idle&&!settings.registered);
    for(int id=C_PAIR_NAME;id<=C_SAVE;id++)if(GetDlgItem(window,id))EnableWindow(GetDlgItem(window,id),idle&&has);
    EnableWindow(group_combo,idle&&has&&selected==new_index);
    EnableWindow(GetDlgItem(window,C_RUN),idle&&!migration_pending&&has&&selected<settings.count&&settings.registered);
    EnableWindow(GetDlgItem(window,C_AUTO),idle&&!migration_pending&&has&&selected<settings.count&&settings.registered);
    EnableWindow(GetDlgItem(window,C_PAUSE),job!=NULL||has);
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
}
static void localize(void){
    if(!window)return;
    SetWindowTextW(window,nova_text(L"设备与同步 · NOVA Desktop",L"Devices and synchronization · NOVA Desktop"));
    SetWindowTextW(devices,nova_text(L"已登记设备",L"Registered devices"));
    SetWindowTextW(pairs,nova_text(L"目录配对",L"Folder pairs"));
    SetWindowTextW(history,nova_text(L"同步记录",L"Synchronization history"));
    SetWindowTextW(progress,nova_text(L"同步及服务器管理进度",L"Synchronization and server management progress"));
    const wchar_t *tabs[]={nova_text(L"设备登记",L"Devices"),nova_text(L"目录配对",L"Folder pairs"),nova_text(L"同步记录",L"Transfers"),nova_text(L"服务器部署与迁移",L"Servers")};
    for(int i=0;i<4;i++){TCITEMW item={0};
        item.mask=TCIF_TEXT;
        item.pszText=(LPWSTR)tabs[i];
        TabCtrl_SetItem(tab,i,&item);
        }
    const int ids[]={C_SERVER,C_REGISTER,C_REFRESH,C_IMPORT,C_NEW,C_REMOVE,C_BROWSE,C_SAVE,C_RUN,C_AUTO,C_PAUSE,C_CLOSE};
    const wchar_t *names[]={nova_text(L"选择服务器…",L"Choose server…"),nova_text(L"登记 / 更新本机",L"Register / update device"),nova_text(L"刷新云端",L"Refresh cloud"),nova_text(L"导入旧配置…",L"Import legacy…"),nova_text(L"添加目录",L"Add folder"),nova_text(L"移除绑定",L"Remove binding"),nova_text(L"浏览…",L"Browse…"),nova_text(L"保存配对",L"Save pair"),nova_text(L"同步一次",L"Sync once"),nova_text(L"开启自动",L"Enable automatic"),nova_text(L"暂停任务",L"Pause task"),nova_text(L"关闭",L"Close")};
    for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++)SetWindowTextW(GetDlgItem(window,ids[i]),names[i]);
    SetWindowTextW(GetDlgItem(window,C_SERVER),nova_text(L"服务器设置…",L"Server setup…"));
    const int admin_ids[]={C_TARGET,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW};
    const wchar_t *admin_names[]={nova_text(L"选择目标服务器…",L"Choose destination…"),nova_text(L"检查服务器",L"Check server"),nova_text(L"一键部署",L"One-click deploy"),nova_text(L"连接 / 安全切换",L"Connect / safe switch"),nova_text(L"迁移 / 继续并切换",L"Migrate / resume and switch"),nova_text(L"恢复旧服务器",L"Restore source"),nova_text(L"跟随已迁移目标",L"Follow migrated destination")};
    for(size_t i=0;i<sizeof(admin_ids)/sizeof(admin_ids[0]);i++)SetWindowTextW(GetDlgItem(window,admin_ids[i]),admin_names[i]);
    const wchar_t *admin_text[]={nova_text(L"目标项目目录",L"Destination project"),nova_text(L"目标队列目录",L"Destination queue"),nova_text(L"新队列配额（GiB）",L"New queue quota (GiB)"),nova_text(L"磁盘保留（GiB）",L"Free reserve (GiB)")};
    for(int i=0;i<4;i++)SetWindowTextW(admin_labels[i],admin_text[i]);
    const wchar_t *label_text[]={nova_text(L"本机名称",L"Device name"),nova_text(L"设备位",L"Device slot"),nova_text(L"云端项目目录",L"Server project directory"),nova_text(L"云端队列目录",L"Server queue directory"),nova_text(L"配对名称",L"Pair name"),nova_text(L"云端同步组",L"Sync group"),nova_text(L"本机目录",L"Local folder"),nova_text(L"排除规则（分号分隔）",L"Exclusions (semicolon-separated)"),nova_text(L"保存目录仅记录配置；明确同步后才会传输工作文件。",L"Saving records configuration only; work files transfer only after explicit synchronization.")};
    for(int i=0;i<9;i++)SetWindowTextW(labels[i],label_text[i]);
    SetWindowTextW(delete_check,nova_text(L"传播删除（使用 Windows 回收站及系统确认）",L"Propagate deletions (Windows Recycle Bin and system confirmation)"));
    SetWindowTextW(name_edit,settings.name);
    SendMessageW(role_combo,CB_RESETCONTENT,0,0);
    SendMessageW(role_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"设备 A",L"Device A"));
    SendMessageW(role_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"设备 B",L"Device B"));
    SendMessageW(role_combo,CB_SETCURSEL,settings.role,0);
    SetWindowTextW(project_edit,settings.project);
    SetWindowTextW(broker_edit,settings.broker);
    const wchar_t *device_columns[]={nova_text(L"设备位",L"Slot"),nova_text(L"设备名称",L"Device"),nova_text(L"登记状态",L"Registration")};
    const wchar_t *pair_columns[]={nova_text(L"名称",L"Name"),nova_text(L"本机目录",L"Local folder"),nova_text(L"对端目录",L"Peer folder"),nova_text(L"状态",L"Status")};
    const wchar_t *history_columns[]={nova_text(L"同步目录",L"Folder"),nova_text(L"发送内容",L"Sent content"),nova_text(L"接收内容",L"Received content"),nova_text(L"复用内容",L"Reused content"),nova_text(L"结果",L"Result")};
    for(int i=0;i<3;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)device_columns[i];
        ListView_SetColumn(devices,i,&c);
        }for(int i=0;i<4;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)pair_columns[i];
        ListView_SetColumn(pairs,i,&c);
        }for(int i=0;i<5;i++){LVCOLUMNW c={0};
        c.mask=LVCF_TEXT;
        c.pszText=(LPWSTR)history_columns[i];
        ListView_SetColumn(history,i,&c);
        }populate_pairs();
    controls();
}
static void layout(void){
    RECT r;
    GetClientRect(window,&r);
    int w=MulDiv(r.right,96,dpi),h=MulDiv(r.bottom,96,dpi);
    move(tab,18,16,w-36,32);
    move(server_text,22,58,w-44,24);
    move(cloud_text,22,88,w-44,24);
    const int device_ids[]={C_SERVER,C_NAME,C_ROLE,C_REGISTER,C_REFRESH,C_IMPORT,C_DEVICES,C_PROJECT,C_BROKER};
    for(size_t i=0;i<sizeof(device_ids)/sizeof(device_ids[0]);i++)ShowWindow(GetDlgItem(window,device_ids[i]),page==0?SW_SHOW:SW_HIDE);
    for(int i=0;i<4;i++)ShowWindow(labels[i],page==0?SW_SHOW:SW_HIDE);
    const int pair_ids[]={C_PAIRS,C_NEW,C_REMOVE,C_PAIR_NAME,C_GROUP,C_PATH,C_BROWSE,C_FILTER,C_DELETE,C_SAVE,C_RUN,C_AUTO};
    for(size_t i=0;i<sizeof(pair_ids)/sizeof(pair_ids[0]);i++)ShowWindow(GetDlgItem(window,pair_ids[i]),page==1?SW_SHOW:SW_HIDE);
    for(int i=4;i<9;i++)ShowWindow(labels[i],page==1?SW_SHOW:SW_HIDE);
    ShowWindow(history,page==2?SW_SHOW:SW_HIDE);
    const int admin_ids[]={C_TARGET,C_TARGET_PROJECT,C_TARGET_ROOT,C_QUOTA,C_RESERVE,C_PROBE,C_DEPLOY,C_ATTACH,C_MIGRATE,C_RESTORE,C_FOLLOW,C_TARGET_TEXT,C_ADMIN_NOTE};
    for(size_t i=0;i<sizeof(admin_ids)/sizeof(admin_ids[0]);i++)ShowWindow(GetDlgItem(window,admin_ids[i]),page==3?SW_SHOW:SW_HIDE);
    for(int i=0;i<4;i++)ShowWindow(admin_labels[i],page==3?SW_SHOW:SW_HIDE);
    move(GetDlgItem(window,C_TARGET),22,128,210,32);
    move(target_text,248,132,w-270,26);
    move(admin_labels[0],22,184,210,24);
    move(target_project_edit,242,179,w-264,32);
    move(admin_labels[1],22,229,210,24);
    move(target_root_edit,242,224,w-264,32);
    move(admin_labels[2],22,278,210,24);
    move(quota_edit,242,272,110,32);
    move(admin_labels[3],390,278,210,24);
    move(reserve_edit,610,272,110,32);
    move(GetDlgItem(window,C_PROBE),22,326,170,34);
    move(GetDlgItem(window,C_DEPLOY),204,326,190,34);
    move(GetDlgItem(window,C_ATTACH),406,326,220,34);
    move(GetDlgItem(window,C_MIGRATE),22,377,250,34);
    move(GetDlgItem(window,C_RESTORE),284,377,210,34);
    move(GetDlgItem(window,C_FOLLOW),506,377,260,34);
    move(admin_note,22,436,w-44,max(65,h-610));
    move(GetDlgItem(window,C_SERVER),22,128,170,32);
    move(labels[0],212,130,100,24);
    move(name_edit,312,126,230,30);
    move(labels[1],562,130,80,24);
    move(role_combo,644,126,150,200);
    move(GetDlgItem(window,C_REGISTER),22,174,200,32);
    move(GetDlgItem(window,C_REFRESH),234,174,150,32);
    move(GetDlgItem(window,C_IMPORT),396,174,170,32);
    move(devices,22,224,w-44,154);
    move(labels[2],22,400,220,24);
    move(project_edit,252,395,w-274,32);
    move(labels[3],22,448,220,24);
    move(broker_edit,252,443,w-274,32);
    int table_height=max(130,h-455);
    move(pairs,22,142,w-44,table_height-18);
    int y=134+table_height;
    move(GetDlgItem(window,C_NEW),22,y,130,31);
    move(GetDlgItem(window,C_REMOVE),164,y,130,31);
    move(GetDlgItem(window,C_RUN),w-474,y,130,31);
    move(GetDlgItem(window,C_AUTO),w-332,y,145,31);
    move(GetDlgItem(window,C_PAUSE),w-175,y,153,31);
    move(labels[4],22,y+44,100,22);
    move(pair_name,126,y+39,300,30);
    move(labels[5],446,y+44,150,22);
    move(group_combo,600,y+39,w-622,220);
    move(labels[6],22,y+84,100,22);
    move(path_edit,126,y+79,w-270,30);
    move(GetDlgItem(window,C_BROWSE),w-132,y+79,110,30);
    move(labels[7],22,y+126,240,22);
    move(filter_edit,265,y+119,w-287,30);
    move(delete_check,22,y+160,w-190,28);
    move(GetDlgItem(window,C_SAVE),w-162,y+159,140,32);
    move(labels[8],22,116,w-44,20);
    move(history,22,124,w-44,h-294);
    ListView_SetColumnWidth(pairs,0,px(170));
    ListView_SetColumnWidth(pairs,1,px((w-338)/2));
    ListView_SetColumnWidth(pairs,2,px((w-338)/2));
    ListView_SetColumnWidth(pairs,3,px(120));
    ListView_SetColumnWidth(devices,1,px(w-334));
    if(page!=1)move(GetDlgItem(window,C_PAUSE),22,h-155,210,32);
    ShowWindow(GetDlgItem(window,C_PAUSE),(page==0||page==3)&&!job?SW_HIDE:SW_SHOW);
    move(progress,22,h-109,w-44,8);
    move(status_text,22,h-91,w-190,72);
    move(GetDlgItem(window,C_CLOSE),w-145,h-64,123,34);
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
            add_column(pairs,0,L"",170);
            add_column(pairs,1,L"",340);
            add_column(pairs,2,L"",340);
            add_column(pairs,3,L"",120);
        button(C_NEW,L"");
            button(C_REMOVE,L"");
            button(C_RUN,L"");
            button(C_AUTO,L"");
            button(C_PAUSE,L"");
            pair_name=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_PAIR_NAME);
            group_combo=control(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST,C_GROUP);
            path_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_PATH);
            filter_edit=control(L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,C_FILTER);
            button(C_BROWSE,L"");
            delete_check=control(L"BUTTON",L"",WS_TABSTOP|BS_AUTOCHECKBOX,C_DELETE);
            button(C_SAVE,L"");
        history=table(C_LOG);
            for(int i=0;i<5;i++)add_column(history,i,L"",i==0?240:i==4?300:140);
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
            text(nova_text(L"目录保存不传输文件；请先核对两台设备的同步组及删除策略。",L"Saving folders does not transfer files; review both devices' groups and deletion policy first."));
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
        HWND lists[]={devices,pairs,history};
        for(int i=0;i<3;i++){
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
                }if(n->hwndFrom==pairs&&n->code==LVN_ITEMCHANGED&&!refreshing&&!job){int index=ListView_GetNextItem(pairs,-1,LVNI_SELECTED);
                if(index>=0&&index!=selected)select_pair(index);
                }return 0;
            }
    case WM_COMMAND:switch(LOWORD(wp)){
        case C_SERVER:page=3;
            TabCtrl_SetCurSel(tab,3);
            layout();
            controls();
            break;
        case C_TARGET:select_server();
            break;
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
        case C_PAUSE:if(job)SetEvent(job->cancel);
            if(selected>=0&&selected<settings.count){SyncSettings next=settings;
                next.pairs[selected].paused=true;
                persist(&next);
                populate_pairs();
                controls();
                }break;
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
    case WM_TIMER:if(wp==AUTO_TIMER&&!job&&ready&&settings.registered&&!shutting_down&&!migration_pending){for(int i=0;i<settings.count;i++)if(!settings.pairs[i].paused&&GetTickCount64()-last_run[i]>=(ULONGLONG)settings.interval*1000){selected=i;
                select_pair(i);
                if(!start_job(SYNC_JOB_RUN,FALSE)){SyncSettings next=settings;
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
        COLORREF color=selected_state?RGB(53,77,122):RGB(25,34,49);
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
    case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,RGB(236,241,249));
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
    if(!settings.name[0])lstrcpyW(settings.name,nova_text(L"本机电脑",L"This computer"));
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
    if(!cloud_view)SetWindowTextW(cloud_text,nova_text(L"尚未连接云端",L"Cloud not connected"));
    for(int i=0;i<ListView_GetItemCount(history)&&i<24;i++)ListView_SetItemText(history,i,4,(LPWSTR)(history_codes[i]==SYNC_OK?nova_text(L"完成",L"Completed"):failure_name(history_codes[i])));
    if(job)PostMessageW(window,PROGRESS_MESSAGE,0,0);
    else text(nova_text(L"就绪；目录配置与任务记录保留。",L"Ready; folder configuration and transfer records retained."));
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
