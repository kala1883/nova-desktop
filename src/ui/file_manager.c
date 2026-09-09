#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define COBJMACROS
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "file_manager.h"
#include "../platform/storage.h"
#include "../i18n.h"

#define FM_CLASS L"NovaFileManager"
#define PANE_CLASS L"NovaFilePane"
#define TAB_LIMIT 12
#define FAVORITE_LIMIT 32
#define LOCATION_SIZE 2048
#define HOME L"shell:MyComputerFolder"
#define START_FOLDER L"shell:Profile"
#define VIEW_IDLE_MS 30000
#define HISTORY_LIMIT 24
#define SAVE_TIMER 2
#define SPLIT_SCALE 10000
#define SPLIT_MIN_LOGICAL 96
enum { C_LAYOUT=200, C_FAVORITES, C_VIEW, C_COPY, C_CUT, C_PASTE, C_RENAME,
       C_DELETE, C_NEWFOLDER, C_HELP, C_BACK=240, C_FORWARD, C_UP, C_GO,
       C_NEWTAB, C_CLOSETAB, C_REFRESH, C_ADDRESS, C_TABS };
typedef struct Pane Pane;
typedef struct Tab {
    IExplorerBrowserEvents events;
    LONG references;
    Pane *pane;
    IExplorerBrowser *browser;
    HWND host;
    DWORD cookie;
    BOOL advised, pending;
    ULONGLONG hidden_since;
    wchar_t *history[HISTORY_LIMIT];
    int history_count, history_pos, travel_target;
    FOLDERVIEWMODE view_mode;
    int icon_size;
    wchar_t location[LOCATION_SIZE], title[128];
} Tab;
struct Pane {
    HWND window, address, tabs, back, forward, up, go, add, close, refresh;
    Tab *items[TAB_LIMIT];
    int count, selected, index;
};
typedef struct Splitter {
    BOOL vertical;
    int coordinate, range_start, range_end;
} Splitter;
static struct {
    HWND window, status, toolbar[10], tooltip, hot_button;
    HFONT font, icon_font;
    HBRUSH background, panel;
    Pane panes[4];
    int active, layout, dpi, splits[12][3];
    int drag_layout, drag_split, drag_original;
    wchar_t settings[MAX_PATH], favorites[FAVORITE_LIMIT][LOCATION_SIZE];
    int favorite_count;
    int tooltips_added;
    BOOL loading, closing, navigation_tree, dirty, test_mode, splitter_dragging;
    int command_runs;
    wchar_t last_command[LOCATION_SIZE],last_command_directory[LOCATION_SIZE];
    ULONGLONG notice_until;
} fm;
static const wchar_t *layout_names_zh[]={L"四窗格",L"双栏",L"双行",L"单窗格",L"左一右二",L"左二右一",L"上一下二",L"上二下一",L"四列",L"四行",L"三列",L"三行"};
static const wchar_t *layout_names_en[]={L"Four panes",L"Two columns",L"Two rows",L"Single pane",L"One left, two right",L"Two left, one right",L"One top, two bottom",L"Two top, one bottom",L"Four columns",L"Four rows",L"Three columns",L"Three rows"};
static const wchar_t *layout_name(int index){return nova_english?layout_names_en[index]:layout_names_zh[index];}
static int scale(int n){return MulDiv(n,fm.dpi?fm.dpi:96,96);}
static Tab *current(Pane *p){return p->count?p->items[p->selected]:NULL;}
static void arrange(void);
static void save_session(void);
static BOOL flush_session(void);
static BOOL ensure_browser(Tab *t);
static void select_tab(Pane *p,int index);
static void status_text(const wchar_t *s){SetWindowTextW(fm.status,s);fm.notice_until=GetTickCount64()+6000;}
static void failure(const wchar_t *action,HRESULT hr){
    wchar_t text[320];swprintf(text,320,nova_text(L"%ls（0x%08lX）。请检查路径、设备连接或访问权限。",L"%ls (0x%08lX). Check the path, device connection, and access permissions."),action,(unsigned long)hr);status_text(text);
}
static void activate(Pane *p){
    if(fm.active!=p->index){int old=fm.active;fm.active=p->index;InvalidateRect(fm.panes[old].window,NULL,FALSE);InvalidateRect(fm.panes[old].tabs,NULL,FALSE);InvalidateRect(p->window,NULL,FALSE);InvalidateRect(p->tabs,NULL,FALSE);}
}
static void focus_view(Tab *t){
    IShellView *view=NULL;
    if(t&&t->browser&&SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view))){
        IShellView_UIActivate(view,SVUIA_ACTIVATE_FOCUS);IShellView_Release(view);
    }
}
static HRESULT navigate(Tab *t,const wchar_t *location){
    if(!t||!t->browser)return E_FAIL;
    PIDLIST_ABSOLUTE id=NULL;
    HRESULT hr=SHParseDisplayName(location,NULL,&id,0,NULL);
    if(SUCCEEDED(hr)){hr=IExplorerBrowser_BrowseToIDList(t->browser,id,SBSP_ABSOLUTE);CoTaskMemFree(id);}
    if(FAILED(hr))failure(nova_text(L"无法打开目录",L"Unable to open the folder"),hr);
    return hr;
}
static BOOL filesystem_directory(const wchar_t *location,wchar_t *directory){
    wchar_t expanded[LOCATION_SIZE];DWORD n=ExpandEnvironmentStringsW(location,expanded,LOCATION_SIZE);
    if(!n||n>LOCATION_SIZE)lstrcpynW(expanded,location,LOCATION_SIZE);
    DWORD attributes=GetFileAttributesW(expanded);
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)){lstrcpynW(directory,expanded,LOCATION_SIZE);return TRUE;}
    PIDLIST_ABSOLUTE id=NULL;BOOL ok=FALSE;
    if(SUCCEEDED(SHParseDisplayName(expanded,NULL,&id,0,NULL))){
        if(SHGetPathFromIDListW(id,directory)){attributes=GetFileAttributesW(directory);ok=attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY);}
        CoTaskMemFree(id);
    }
    return ok;
}
static void command_directory(Tab *t,wchar_t *directory){
    if(t&&filesystem_directory(t->location,directory))return;
    DWORD n=GetEnvironmentVariableW(L"USERPROFILE",directory,LOCATION_SIZE);
    if(!n||n>=LOCATION_SIZE)GetCurrentDirectoryW(LOCATION_SIZE,directory);
}
static BOOL relative_directory(Tab *t,const wchar_t *input,wchar_t *resolved){
    wchar_t expanded[LOCATION_SIZE],base[LOCATION_SIZE];DWORD n=ExpandEnvironmentStringsW(input,expanded,LOCATION_SIZE);
    if(!n||n>LOCATION_SIZE)lstrcpynW(expanded,input,LOCATION_SIZE);
    if(filesystem_directory(expanded,resolved))return TRUE;
    if(!PathIsRelativeW(expanded)||!t||!filesystem_directory(t->location,base))return FALSE;
    size_t base_length=wcslen(base),input_length=wcslen(expanded);
    if(base_length+input_length+2>=LOCATION_SIZE)return FALSE;
    swprintf(resolved,LOCATION_SIZE,L"%ls%ls%ls",base,base_length&&base[base_length-1]==L'\\'?L"":L"\\",expanded);
    DWORD attributes=GetFileAttributesW(resolved);return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
static BOOL location_syntax(const wchar_t *value){
    return !PathIsRelativeW(value)||!wcsncmp(value,L"shell:",6)||!wcsncmp(value,L"::{",3)||value[0]==L'\\'||wcschr(value,L'\\')||wcschr(value,L'/');
}
static BOOL run_command(Tab *t,const wchar_t *command){
    wchar_t directory[LOCATION_SIZE],parameters[LOCATION_SIZE+32];command_directory(t,directory);
    if(wcslen(command)+16>=sizeof(parameters)/sizeof(parameters[0])){status_text(nova_text(L"命令过长，请缩短后重试。",L"The command is too long. Shorten it and try again."));return FALSE;}
    if(fm.test_mode){fm.command_runs++;lstrcpynW(fm.last_command,command,LOCATION_SIZE);lstrcpynW(fm.last_command_directory,directory,LOCATION_SIZE);return TRUE;}
    swprintf(parameters,sizeof(parameters)/sizeof(parameters[0]),L"/D /S /K \"%ls\"",command);
    SHELLEXECUTEINFOW execute={0};execute.cbSize=sizeof(execute);execute.fMask=SEE_MASK_FLAG_NO_UI;execute.hwnd=fm.window;execute.lpVerb=L"open";execute.lpFile=L"cmd.exe";execute.lpParameters=parameters;execute.lpDirectory=directory;execute.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&execute)){wchar_t message[320];swprintf(message,320,nova_text(L"无法在当前目录启动命令（Windows 错误 %lu）。",L"Unable to start the command in this folder (Windows error %lu)."),GetLastError());status_text(message);return FALSE;}
    wchar_t message[320];swprintf(message,320,nova_text(L"已在 %ls 中启动命令窗口。",L"Command window started in %ls."),directory);status_text(message);return TRUE;
}
static void open_address(Pane *p){
    Tab *t=current(p);if(!t)return;
    wchar_t input[LOCATION_SIZE],expanded[LOCATION_SIZE],resolved[LOCATION_SIZE];GetWindowTextW(p->address,input,LOCATION_SIZE);
    wchar_t *value=input;while(*value==L' '||*value==L'\t')value++;size_t length=wcslen(value);while(length&&(value[length-1]==L' '||value[length-1]==L'\t'))value[--length]=0;
    if(!*value){SetWindowTextW(p->address,t->location);return;}
    BOOL forced=*value==L'>';if(forced){value++;while(*value==L' '||*value==L'\t')value++;}
    DWORD n=ExpandEnvironmentStringsW(value,expanded,LOCATION_SIZE);if(!n||n>LOCATION_SIZE)lstrcpynW(expanded,value,LOCATION_SIZE);
    if(!forced&&relative_directory(t,value,resolved)){if(SUCCEEDED(navigate(t,resolved)))focus_view(t);return;}
    if(!forced&&location_syntax(expanded)){if(SUCCEEDED(navigate(t,expanded)))focus_view(t);return;}
    if(!*value){status_text(nova_text(L"请在 > 后输入命令。",L"Enter a command after >."));SetWindowTextW(p->address,t->location);return;}
    run_command(t,value);SetWindowTextW(p->address,t->location);
}
static HRESULT STDMETHODCALLTYPE event_query(IExplorerBrowserEvents *self,REFIID iid,void **out){
    if(!out)return E_POINTER;
    *out=NULL;
    if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IExplorerBrowserEvents)){*out=self;IExplorerBrowserEvents_AddRef(self);return S_OK;}
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE event_add(IExplorerBrowserEvents *self){return (ULONG)InterlockedIncrement(&((Tab*)self)->references);}
static ULONG STDMETHODCALLTYPE event_release(IExplorerBrowserEvents *self){
    Tab *t=(Tab*)self;LONG n=InterlockedDecrement(&t->references);if(!n)free(t);return (ULONG)n;
}
static HRESULT STDMETHODCALLTYPE event_pending(IExplorerBrowserEvents *self,PCIDLIST_ABSOLUTE id){
    (void)id;((Tab*)self)->pending=TRUE;return S_OK;
}
static HRESULT STDMETHODCALLTYPE event_created(IExplorerBrowserEvents *self,IShellView *view){
    Tab *t=(Tab*)self;IFolderView2 *folder=NULL;
    if(SUCCEEDED(IShellView_QueryInterface(view,&IID_IFolderView2,(void**)&folder))){IFolderView2_SetViewModeAndIconSize(folder,t->view_mode,t->icon_size);IFolderView2_Release(folder);}
    return S_OK;
}
static void record_history(Tab *t){
    if(t->travel_target>=0&&t->travel_target<t->history_count&&!lstrcmpiW(t->location,t->history[t->travel_target]))t->history_pos=t->travel_target;
    else if(!t->history_count||lstrcmpiW(t->location,t->history[t->history_pos])){
        wchar_t *path=_wcsdup(t->location);if(!path){t->travel_target=-1;return;}
        while(t->history_count>t->history_pos+1)free(t->history[--t->history_count]);
        if(t->history_count==HISTORY_LIMIT){free(t->history[0]);memmove(t->history,t->history+1,(HISTORY_LIMIT-1)*sizeof(t->history[0]));--t->history_count;}
        t->history[t->history_count++]=path;t->history_pos=t->history_count-1;
    }
    t->travel_target=-1;
}
static HRESULT STDMETHODCALLTYPE event_complete(IExplorerBrowserEvents *self,PCIDLIST_ABSOLUTE id){
    Tab *t=(Tab*)self;PWSTR value=NULL;t->pending=FALSE;
    if(SUCCEEDED(SHGetNameFromIDList(id,SIGDN_DESKTOPABSOLUTEPARSING,&value))){lstrcpynW(t->location,value,LOCATION_SIZE);CoTaskMemFree(value);}
    if(SUCCEEDED(SHGetNameFromIDList(id,SIGDN_NORMALDISPLAY,&value))){lstrcpynW(t->title,value,128);CoTaskMemFree(value);}
    record_history(t);
    Pane *p=t->pane;
    for(int i=0;i<p->count;i++)if(p->items[i]==t){TCITEMW item={0};item.mask=TCIF_TEXT;item.pszText=t->title;TabCtrl_SetItem(p->tabs,i,&item);}
    if(current(p)==t)SetWindowTextW(p->address,t->location);
    if(!fm.loading&&!fm.closing)PostMessageW(fm.window,WM_APP+41,0,0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE event_failed(IExplorerBrowserEvents *self,PCIDLIST_ABSOLUTE id){
    (void)id;Tab *t=(Tab*)self;t->pending=FALSE;t->travel_target=-1;
    failure(nova_text(L"目录加载失败；原目录保留，可在地址栏输入其他路径",L"Folder loading failed; the previous folder remains open. Enter another path in the address bar"),E_FAIL);return S_OK;
}
static IExplorerBrowserEventsVtbl event_vtable={event_query,event_add,event_release,event_pending,event_created,event_complete,event_failed};
static void suspend_tab(Tab *t){
    if(!t||!t->browser)return;
    IFolderView2 *view=NULL;
    if(SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IFolderView2,(void**)&view))){
        IFolderView2_GetViewModeAndIconSize(view,&t->view_mode,&t->icon_size);IFolderView2_Release(view);
    }
    if(t->advised)IExplorerBrowser_Unadvise(t->browser,t->cookie);
    t->advised=FALSE;IExplorerBrowser_Destroy(t->browser);IExplorerBrowser_Release(t->browser);t->browser=NULL;
    if(t->host)DestroyWindow(t->host);
    t->host=NULL;t->pending=FALSE;
}
static void release_tab(Tab *t){
    if(!t)return;
    suspend_tab(t);
    for(int i=0;i<t->history_count;i++)free(t->history[i]);
    IExplorerBrowserEvents_Release(&t->events);
}
static BOOL ensure_browser(Tab *t){
    if(!t)return FALSE;
    if(t->browser)return TRUE;
    t->host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",nova_text(L"文件视图",L"File view"),WS_CHILD|WS_CLIPCHILDREN,0,0,10,10,t->pane->window,NULL,GetModuleHandleW(NULL),NULL);
    HRESULT hr=t->host?CoCreateInstance(&CLSID_ExplorerBrowser,NULL,CLSCTX_INPROC_SERVER,&IID_IExplorerBrowser,(void**)&t->browser):E_OUTOFMEMORY;
    if(SUCCEEDED(hr)){
        RECT r={0,0,100,100};FOLDERSETTINGS fs={t->view_mode,FWF_AUTOARRANGE};
        IExplorerBrowser_SetOptions(t->browser,EBO_NOBORDER|(fm.navigation_tree?EBO_SHOWFRAMES:0));
        hr=IExplorerBrowser_Initialize(t->browser,t->host,&r,&fs);
    }
    if(SUCCEEDED(hr)){hr=IExplorerBrowser_Advise(t->browser,&t->events,&t->cookie);t->advised=SUCCEEDED(hr);}
    if(FAILED(hr)){failure(nova_text(L"无法创建 Windows 文件视图",L"Unable to create the Windows file view"),hr);suspend_tab(t);if(t->host){DestroyWindow(t->host);t->host=NULL;}return FALSE;}
    /* Do not replace unavailable removable/network paths with a different persisted folder. */
    navigate(t,t->location);return TRUE;
}
static BOOL add_tab(Pane *p,const wchar_t *location){
    if(p->count==TAB_LIMIT){status_text(nova_text(L"每个窗格最多 12 个标签，请先关闭不需要的标签。",L"Each pane supports up to 12 tabs. Close an unused tab first."));return FALSE;}
    Tab *t=calloc(1,sizeof(*t));if(!t)return FALSE;
    t->events.lpVtbl=&event_vtable;t->references=1;t->pane=p;t->travel_target=-1;t->view_mode=FVM_DETAILS;t->icon_size=16;
    lstrcpynW(t->location,location,LOCATION_SIZE);
    const wchar_t *base=wcsrchr(location,L'\\');lstrcpynW(t->title,base&&base[1]?base+1:location,128);
    int n=p->count++;p->items[n]=t;
    TCITEMW item={0};item.mask=TCIF_TEXT;item.pszText=t->title;TabCtrl_InsertItem(p->tabs,n,&item);
    select_tab(p,n);return TRUE;
}
static void select_tab(Pane *p,int index){
    if(index<0||index>=p->count)return;
    p->selected=index;TabCtrl_SetCurSel(p->tabs,index);
    for(int i=0;i<p->count;i++)ShowWindow(p->items[i]->host,i==index?SW_SHOW:SW_HIDE);
    SetWindowTextW(p->address,current(p)->location);EnableWindow(p->close,p->count>1);EnableWindow(p->add,p->count<TAB_LIMIT);InvalidateRect(p->tabs,NULL,FALSE);
    arrange();
}
static void close_tab(Pane *p){
    if(p->count<=1)return;
    int index=p->selected;Tab *t=p->items[index];
    for(int i=index;i<p->count-1;i++)p->items[i]=p->items[i+1];
    --p->count;TabCtrl_DeleteItem(p->tabs,index);release_tab(t);
    select_tab(p,index<p->count?index:p->count-1);save_session();focus_view(current(p));
}
static void shell_verb(Tab *t,const char *verb,BOOL background){
    IShellView *view=NULL;IContextMenu *menu=NULL;
    if(!t||!t->browser||FAILED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view)))return;
    HRESULT hr=IShellView_GetItemObject(view,background?SVGIO_BACKGROUND:SVGIO_SELECTION,&IID_IContextMenu,(void**)&menu);
    if(SUCCEEDED(hr)){
        HMENU popup=CreatePopupMenu();IContextMenu_QueryContextMenu(menu,popup,0,1,0x7fff,CMF_NORMAL|CMF_CANRENAME);
        CMINVOKECOMMANDINFO info={0};info.cbSize=sizeof(info);info.hwnd=fm.window;info.lpVerb=verb;info.nShow=SW_SHOWNORMAL;
        hr=IContextMenu_InvokeCommand(menu,&info);DestroyMenu(popup);IContextMenu_Release(menu);
    }
    IShellView_Release(view);
    if(FAILED(hr))failure(nova_text(L"操作未完成，请选择文件或使用文件视图的右键菜单",L"The operation did not complete. Select a file or use the file view context menu"),hr);
}
static void refresh(Tab *t){
    IShellView *view=NULL;if(t&&t->browser&&SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view))){IShellView_Refresh(view);IShellView_Release(view);}
}
static void popup_favorites(void){
    HMENU m=CreatePopupMenu();
    AppendMenuW(m,MF_STRING,1,nova_text(L"收藏当前目录",L"Add current folder to favorites"));AppendMenuW(m,MF_STRING,2,nova_text(L"移除当前目录收藏",L"Remove current folder from favorites"));AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING,3,nova_text(L"此电脑",L"This PC"));AppendMenuW(m,MF_STRING,4,nova_text(L"桌面",L"Desktop"));AppendMenuW(m,MF_STRING,5,nova_text(L"下载",L"Downloads"));AppendMenuW(m,MF_STRING,6,nova_text(L"用户文件夹",L"User folder"));
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    for(int i=0;i<fm.favorite_count;i++)AppendMenuW(m,MF_STRING,100+i,fm.favorites[i]);
    RECT r;GetWindowRect(fm.toolbar[1],&r);UINT id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,r.left,r.bottom,0,fm.window,NULL);DestroyMenu(m);
    Tab *t=current(&fm.panes[fm.active]);if(!t)return;
    if(id==1){
        for(int i=0;i<fm.favorite_count;i++)if(!lstrcmpiW(t->location,fm.favorites[i])){status_text(nova_text(L"此目录已在收藏中。",L"This folder is already a favorite."));return;}
        if(fm.favorite_count==FAVORITE_LIMIT){status_text(nova_text(L"收藏已满（32 项），请先移除不需要的收藏。",L"Favorites are full (32 items). Remove an unused favorite first."));return;}
        lstrcpynW(fm.favorites[fm.favorite_count++],t->location,LOCATION_SIZE);save_session();status_text(nova_text(L"已收藏当前目录。",L"Current folder added to favorites."));
    }else if(id==2){
        for(int i=0;i<fm.favorite_count;i++)if(!lstrcmpiW(t->location,fm.favorites[i])){
            for(int j=i;j<fm.favorite_count-1;j++)memcpy(fm.favorites[j],fm.favorites[j+1],sizeof(fm.favorites[j]));
            --fm.favorite_count;save_session();status_text(nova_text(L"已移除目录收藏。",L"Folder removed from favorites."));break;
        }
    }else if(id>=3&&id<=6){const wchar_t *places[]={HOME,L"shell:Desktop",L"shell:Downloads",L"shell:Profile"};navigate(t,places[id-3]);}
    else if(id>=100&&id<(UINT)(100+fm.favorite_count))navigate(t,fm.favorites[id-100]);
}
static void view_menu(void){
    HMENU m=CreatePopupMenu();const wchar_t *labels_zh[]={L"详细信息",L"列表",L"小图标",L"大图标 / 缩略图",L"内容",L"平铺"};
    const wchar_t *labels_en[]={L"Details",L"List",L"Small icons",L"Large icons / thumbnails",L"Content",L"Tiles"};const wchar_t **labels=nova_english?labels_en:labels_zh;
    FOLDERVIEWMODE modes[]={FVM_DETAILS,FVM_LIST,FVM_SMALLICON,FVM_ICON,FVM_CONTENT,FVM_TILE};
    for(int i=0;i<6;i++)AppendMenuW(m,MF_STRING,i+1,labels[i]);
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING|(fm.navigation_tree?MF_CHECKED:0),20,nova_text(L"显示导航栏 / 目录树",L"Show navigation pane / folder tree"));
    RECT r;GetWindowRect(fm.toolbar[2],&r);UINT id=TrackPopupMenu(m,TPM_RETURNCMD,r.left,r.bottom,0,fm.window,NULL);DestroyMenu(m);
    Tab *t=current(&fm.panes[fm.active]);IFolderView2 *v=NULL;
    if(id>=1&&id<=6&&t&&t->browser&&SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IFolderView2,(void**)&v))){IFolderView2_SetViewModeAndIconSize(v,modes[id-1],id==4?64:16);IFolderView2_Release(v);}
    if(id==20){fm.navigation_tree=!fm.navigation_tree;
        for(int i=0;i<4;i++)for(int j=0;j<fm.panes[i].count;j++)if(fm.panes[i].items[j]->browser)IExplorerBrowser_SetOptions(fm.panes[i].items[j]->browser,EBO_NOBORDER|(fm.navigation_tree?EBO_SHOWFRAMES:0));
        arrange();save_session();
    }
}
static void command(Pane *p,int id){
    Tab *t=current(p);activate(p);
    if(id==C_NEWTAB){wchar_t location[LOCATION_SIZE];lstrcpynW(location,t?t->location:HOME,LOCATION_SIZE);add_tab(p,location);save_session();return;}
    if(!t)return;
    if(!ensure_browser(t))return;
    switch(id){
    case C_CLOSETAB:close_tab(p);break;
    case C_GO:open_address(p);break;
    case C_BACK:case C_FORWARD:{
        int target=t->history_pos+(id==C_BACK?-1:1);
        if(target<0||target>=t->history_count){status_text(nova_text(L"此方向没有可打开的目录。",L"There is no folder to open in this direction."));break;}
        t->travel_target=target;if(FAILED(navigate(t,t->history[target])))t->travel_target=-1;break;}
    case C_UP:if(FAILED(IExplorerBrowser_BrowseToIDList(t->browser,NULL,SBSP_PARENT)))status_text(nova_text(L"此方向没有可打开的目录。",L"There is no folder to open in this direction."));break;
    case C_REFRESH:refresh(t);break;
    case C_COPY:shell_verb(t,"copy",FALSE);break;
    case C_CUT:shell_verb(t,"cut",FALSE);break;
    case C_PASTE:shell_verb(t,"paste",TRUE);break;
    case C_RENAME:focus_view(t);shell_verb(t,"rename",FALSE);break;
    case C_DELETE:if(MessageBoxW(fm.window,nova_text(L"删除当前窗格中选中的文件？Windows 将处理回收站和后续确认。",L"Delete the selected files in this pane? Windows will handle the Recycle Bin and any further confirmation."),nova_text(L"删除文件",L"Delete files"),MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES)shell_verb(t,"delete",FALSE);break;
    case C_NEWFOLDER:shell_verb(t,"NewFolder",TRUE);break;
    }
}
static HWND child(HWND parent,const wchar_t *cls,const wchar_t *title,DWORD style,int id){
    HWND h=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(h,WM_SETFONT,(WPARAM)fm.font,TRUE);return h;
}
static LRESULT CALLBACK button_proc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(message==WM_MOUSEMOVE&&fm.hot_button!=h&&IsWindowEnabled(h)){
        HWND old=fm.hot_button;fm.hot_button=h;if(old)InvalidateRect(old,NULL,TRUE);
        TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,NULL,TRUE);
    }
    if(message==WM_MOUSELEAVE){if(fm.hot_button==h)fm.hot_button=NULL;InvalidateRect(h,NULL,TRUE);}
    if(message==WM_ENABLE||message==WM_SETFOCUS||message==WM_KILLFOCUS)InvalidateRect(h,NULL,TRUE);
    if(message==WM_NCDESTROY){if(fm.hot_button==h)fm.hot_button=NULL;RemoveWindowSubclass(h,button_proc,1);}
    return DefSubclassProc(h,message,wp,lp);
}
static HWND button(HWND p,const wchar_t *title,int id){HWND h=child(p,L"BUTTON",title,BS_OWNERDRAW,id);SetWindowSubclass(h,button_proc,1,0);return h;}
static void add_tip(HWND control,const wchar_t *text){
    TOOLINFOW info={0};info.cbSize=TTTOOLINFOW_V2_SIZE;info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=GetParent(control);info.uId=(UINT_PTR)control;info.lpszText=(LPWSTR)text;
    if(SendMessageW(fm.tooltip,TTM_ADDTOOLW,0,(LPARAM)&info))fm.tooltips_added++;
}
static const wchar_t *button_glyph(int id){
    switch(id){
    case C_LAYOUT:return L"\xE8A9";case C_FAVORITES:return L"\xE734";case C_VIEW:return L"\xE890";
    case C_COPY:return L"\xE8C8";case C_CUT:return L"\xE8C6";case C_PASTE:return L"\xE77F";
    case C_RENAME:return L"\xE8AC";case C_DELETE:return L"\xE74D";case C_NEWFOLDER:return L"\xE8F4";case C_HELP:return L"\xE897";
    case C_BACK:return L"\xE72B";case C_FORWARD:return L"\xE72A";case C_UP:return L"\xE74A";case C_GO:return L"\xE8AD";
    case C_NEWTAB:return L"\xE710";case C_CLOSETAB:return L"\xE711";case C_REFRESH:return L"\xE72C";
    }return L"";
}
static LRESULT draw_button(DRAWITEMSTRUCT *d){
    RECT r=d->rcItem;BOOL pressed=(d->itemState&ODS_SELECTED)!=0,hot=fm.hot_button==d->hwndItem&&!(d->itemState&ODS_DISABLED);
    COLORREF fill=pressed?RGB(53,77,122):hot?RGB(43,57,78):RGB(25,34,49);HBRUSH brush=CreateSolidBrush(fill);FillRect(d->hDC,&r,brush);DeleteObject(brush);
    if(pressed)OffsetRect(&r,0,scale(1));
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?RGB(112,128,150):hot?RGB(255,255,255):RGB(218,227,241));
    HFONT old=(HFONT)SelectObject(d->hDC,fm.icon_font);DrawTextW(d->hDC,button_glyph(d->CtlID),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(d->hDC,old);
    if(d->itemState&ODS_FOCUS){RECT focus=d->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(d->hDC,&focus);}return TRUE;
}
static LRESULT draw_tab(Pane *p,DRAWITEMSTRUCT *d){
    if(d->itemID==(UINT)-1)return TRUE;
    wchar_t text[128]=L"";TCITEMW item={0};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=128;TabCtrl_GetItem(d->hwndItem,(int)d->itemID,&item);
    BOOL selected=(int)d->itemID==p->selected;
    COLORREF fill=selected?RGB(53,77,122):RGB(232,236,242);
    COLORREF ink=selected?RGB(255,255,255):RGB(35,45,59);
    HBRUSH brush=CreateSolidBrush(fill);FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
    RECT edge=d->rcItem;HBRUSH border=CreateSolidBrush(selected?(p->index==fm.active?RGB(105,151,224):RGB(82,111,157)):RGB(195,203,214));
    if(selected){edge.bottom=edge.top+scale(3);FillRect(d->hDC,&edge,border);}else{edge.left=edge.right-1;FillRect(d->hDC,&edge,border);}DeleteObject(border);
    RECT label=d->rcItem;label.left+=scale(8);label.right-=scale(8);label.top+=selected?scale(2):0;
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,ink);HFONT old=(HFONT)SelectObject(d->hDC,fm.font);
    DrawTextW(d->hDC,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(d->hDC,old);
    if(d->itemState&ODS_FOCUS){RECT focus=d->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(d->hDC,&focus);}return TRUE;
}
static void pane_layout(Pane *p){
    if(!p->window)return;
    RECT r;GetClientRect(p->window,&r);int w=r.right,h=r.bottom,s=scale(4),bh=scale(28);
    MoveWindow(p->tabs,s,s,w-scale(76)-s,bh,TRUE);
    MoveWindow(p->add,w-scale(68),s,scale(32),bh,TRUE);MoveWindow(p->close,w-scale(34),s,scale(30),bh,TRUE);
    int y=scale(36);HWND nav[]={p->back,p->forward,p->up,p->refresh};
    for(int i=0;i<4;i++)MoveWindow(nav[i],s+i*scale(36),y,scale(32),bh,TRUE);
    MoveWindow(p->address,s,scale(70),w-scale(42),bh,TRUE);MoveWindow(p->go,w-scale(36),scale(70),scale(32),bh,TRUE);
    for(int i=0;i<p->count;i++){
        Tab *t=p->items[i];if(!t->browser)continue;MoveWindow(t->host,s,scale(103),w-2*s,h-scale(107)>0?h-scale(107):1,TRUE);
        RECT rect;GetClientRect(t->host,&rect);IExplorerBrowser_SetRect(t->browser,NULL,rect);
    }
}
/* Same layout vocabulary as the reference. Geometry is shared with integration tests. */
static int split_count(int layout){
    if(layout==3)return 0;
    if(layout==8||layout==9)return 3;
    if(layout==0||(layout>=4&&layout<=7)||layout==10||layout==11)return 2;
    return 1;
}
static BOOL split_vertical(int layout,int index){
    if(layout==0)return index==0;
    if(layout==1||layout==8||layout==10)return TRUE;
    if(layout==2||layout==9||layout==11)return FALSE;
    if(layout==4||layout==5)return index==0;
    return index==1;
}
static int default_split(int layout,int index){
    if(layout==8||layout==9){const int values[]={2500,5000,7500};return values[index];}
    if(layout==10||layout==11){const int values[]={3333,6667};return values[index];}
    return 5000;
}
static int split_value(int layout,int index){int value=fm.splits[layout][index];return value>=200&&value<=9800?value:default_split(layout,index);}
static int split_pixel(int layout,int index,int total){return MulDiv(total,split_value(layout,index),SPLIT_SCALE);}
static int pane_rects(int layout,int width,int height,RECT out[4]){
    int count=4;
    for(int i=0;i<4;i++)SetRect(&out[i],0,0,width,height);
    if(layout==3)return 1;
    if(layout==1||layout==2)count=2;
    else if(layout>=4&&layout<=7)count=3;
    else if(layout==10||layout==11)count=3;
    int a=split_pixel(layout,0,split_vertical(layout,0)?width:height),b=split_pixel(layout,1,split_vertical(layout,1)?width:height);
    if(layout==0){SetRect(&out[0],0,0,a,b);SetRect(&out[1],a,0,width,b);SetRect(&out[2],0,b,a,height);SetRect(&out[3],a,b,width,height);}
    else if(layout==1){SetRect(&out[0],0,0,a,height);SetRect(&out[1],a,0,width,height);}
    else if(layout==2){SetRect(&out[0],0,0,width,a);SetRect(&out[1],0,a,width,height);}
    else if(layout==8||layout==10){int edges[5]={0,a,b,layout==8?split_pixel(layout,2,width):width,width};for(int i=0;i<count;i++)SetRect(&out[i],edges[i],0,edges[i+1],height);}
    else if(layout==9||layout==11){int edges[5]={0,a,b,layout==9?split_pixel(layout,2,height):height,height};for(int i=0;i<count;i++)SetRect(&out[i],0,edges[i],width,edges[i+1]);}
    else if(layout==4){SetRect(&out[0],0,0,a,height);SetRect(&out[1],a,0,width,b);SetRect(&out[2],a,b,width,height);}
    else if(layout==5){SetRect(&out[0],0,0,a,b);SetRect(&out[1],0,b,a,height);SetRect(&out[2],a,0,width,height);}
    else if(layout==6){SetRect(&out[0],0,0,width,a);SetRect(&out[1],0,a,b,height);SetRect(&out[2],b,a,width,height);}
    else if(layout==7){SetRect(&out[0],0,0,b,a);SetRect(&out[1],b,0,width,a);SetRect(&out[2],0,a,width,height);}
    return count;
}
static BOOL splitter_geometry(int layout,int index,int width,int height,Splitter *out){
    if(index<0||index>=split_count(layout))return FALSE;
    out->vertical=split_vertical(layout,index);out->coordinate=split_pixel(layout,index,out->vertical?width:height);
    out->range_start=0;out->range_end=out->vertical?height:width;
    if(index==1&&layout==4)out->range_start=split_pixel(layout,0,width);
    else if(index==1&&layout==5)out->range_end=split_pixel(layout,0,width);
    else if(index==1&&layout==6)out->range_start=split_pixel(layout,0,height);
    else if(index==1&&layout==7)out->range_end=split_pixel(layout,0,height);
    return TRUE;
}
static int hit_splitter(POINT point,int *split){
    RECT r;GetClientRect(fm.window,&r);int top=scale(46),height=r.bottom-top-scale(27),tolerance=scale(6);point.y-=top;
    if(point.y<0||point.y>height)return 0;
    for(int i=0;i<split_count(fm.layout);i++){Splitter s={0};if(!splitter_geometry(fm.layout,i,r.right,height,&s))continue;
        int across=s.vertical?point.y:point.x,delta=(s.vertical?point.x:point.y)-s.coordinate;
        if(across>=s.range_start&&across<=s.range_end&&delta>=-tolerance&&delta<=tolerance){*split=i;return s.vertical?1:2;}
    }return 0;
}
static void update_splitter(POINT point){
    RECT r;GetClientRect(fm.window,&r);int height=r.bottom-scale(46)-scale(27),vertical=split_vertical(fm.drag_layout,fm.drag_split);
    int total=vertical?r.right:height,coordinate=vertical?point.x:point.y-scale(46);if(total<=0)return;
    int count=split_count(fm.drag_layout),minimum=min(scale(SPLIT_MIN_LOGICAL),total/(count+1));if(minimum<scale(24))minimum=scale(24);
    int low=minimum,high=total-minimum;
    if((fm.drag_layout==8||fm.drag_layout==9||fm.drag_layout==10||fm.drag_layout==11)){
        if(fm.drag_split>0)low=split_pixel(fm.drag_layout,fm.drag_split-1,total)+minimum;
        if(fm.drag_split+1<count)high=split_pixel(fm.drag_layout,fm.drag_split+1,total)-minimum;
    }
    if(low>high)return;
    if(coordinate<low)coordinate=low;
    if(coordinate>high)coordinate=high;
    fm.splits[fm.drag_layout][fm.drag_split]=MulDiv(coordinate,SPLIT_SCALE,total);arrange();
}
static void cancel_splitter(BOOL restore){
    if(!fm.splitter_dragging)return;
    if(restore)fm.splits[fm.drag_layout][fm.drag_split]=fm.drag_original;
    fm.splitter_dragging=FALSE;if(GetCapture()==fm.window)ReleaseCapture();arrange();
}
static void arrange(void){
    if(!fm.window)return;
    RECT r;GetClientRect(fm.window,&r);int gap=scale(4),top=scale(46),bottom=scale(27);
    int x=scale(8),y=scale(8);
    for(int i=0;i<10;i++){
        MoveWindow(fm.toolbar[i],x,y,scale(34),scale(30),TRUE);x+=scale(40);
    }
    MoveWindow(fm.status,scale(8),r.bottom-bottom,r.right-scale(16),bottom,TRUE);
    RECT boxes[4];int count=pane_rects(fm.layout,r.right,r.bottom-top-bottom,boxes);
    if(fm.active>=count)fm.active=0;
    for(int i=0;i<4;i++){
        Pane *p=&fm.panes[i];ShowWindow(p->window,i<count?SW_SHOW:SW_HIDE);
        for(int j=0;j<p->count;j++){
            Tab *t=p->items[j];BOOL visible=i<count&&j==p->selected&&IsWindowVisible(fm.window)&&!IsIconic(GetAncestor(fm.window,GA_ROOT));
            if(visible){t->hidden_since=0;if(!fm.loading)ensure_browser(t);}
            else if(t->browser&&!t->hidden_since)t->hidden_since=GetTickCount64();
            if(t->host)ShowWindow(t->host,visible?SW_SHOW:SW_HIDE);
        }
        if(i<count){RECT q=boxes[i];MoveWindow(p->window,q.left+gap,q.top+top+gap,q.right-q.left-2*gap,q.bottom-q.top-2*gap,TRUE);pane_layout(p);}
    }
    SetWindowTextW(fm.toolbar[0],layout_name(fm.layout));
}
static int session_int(const wchar_t *section,const wchar_t *key,int fallback){return store_int(L"files",section,key,fallback);}
static void session_get(const wchar_t *section,const wchar_t *key,const wchar_t *fallback,wchar_t *value,int size){store_get(L"files",section,key,fallback,value,size);}
static void load_split_positions(void){
    for(int layout=0;layout<12;layout++){
        int count=split_count(layout),valid=TRUE;
        for(int i=0;i<count;i++){wchar_t key[32];swprintf(key,32,L"Split%d_%d",layout,i);fm.splits[layout][i]=session_int(L"Manager",key,default_split(layout,i));
            if(fm.splits[layout][i]<200||fm.splits[layout][i]>9800)valid=FALSE;
            if(i&&split_vertical(layout,i)==split_vertical(layout,i-1)&&fm.splits[layout][i]<=fm.splits[layout][i-1]+200)valid=FALSE;
        }
        if(!valid)for(int i=0;i<count;i++)fm.splits[layout][i]=default_split(layout,i);
    }
}
static BOOL migrate_session(void){
    if(session_int(L"Manager",L"Migrated",0))return TRUE;
    if(!store_begin())return FALSE;
    BOOL ok=TRUE;wchar_t section[32],key[32],value[LOCATION_SIZE];
    int layout=GetPrivateProfileIntW(L"Manager",L"Layout",0,fm.settings);
    ok=store_set_int(L"files",L"Manager",L"Layout",layout>=0&&layout<12?layout:0)&&ok;
    ok=store_set_int(L"files",L"Manager",L"NavigationTree",GetPrivateProfileIntW(L"Manager",L"NavigationTree",0,fm.settings)!=0)&&ok;
    int favorites=GetPrivateProfileIntW(L"Manager",L"FavoriteCount",0,fm.settings);
    if(favorites<0||favorites>FAVORITE_LIMIT)return store_end(FALSE);
    ok=store_set_int(L"files",L"Manager",L"FavoriteCount",favorites)&&ok;
    for(int i=0;i<favorites;i++){swprintf(key,32,L"Item%d",i);GetPrivateProfileStringW(L"Favorites",key,L"",value,LOCATION_SIZE,fm.settings);ok=store_set(L"files",L"Favorites",key,value)&&ok;}
    for(int i=0;i<4;i++){
        swprintf(section,32,L"Pane%d",i);int count=GetPrivateProfileIntW(section,L"Count",1,fm.settings);
        if(count<1||count>TAB_LIMIT)return store_end(FALSE);
        ok=store_set_int(L"files",section,L"Count",count)&&ok;
        int selected=GetPrivateProfileIntW(section,L"Selected",0,fm.settings);
        ok=store_set_int(L"files",section,L"Selected",selected>=0&&selected<count?selected:0)&&ok;
        for(int j=0;j<count;j++){swprintf(key,32,L"Tab%d",j);GetPrivateProfileStringW(section,key,START_FOLDER,value,LOCATION_SIZE,fm.settings);ok=store_set(L"files",section,key,value)&&ok;}
    }
    ok=store_set_int(L"files",L"Manager",L"Migrated",1)&&ok;return store_end(ok);
}
static void save_session(void){
    if(fm.loading||fm.closing)return;
    fm.dirty=TRUE;
    if(!SetTimer(fm.window,SAVE_TIMER,400,NULL))flush_session();
}
static BOOL flush_session(void){
    KillTimer(fm.window,SAVE_TIMER);if(!fm.dirty)return TRUE;
    if(!store_begin()){status_text(store_error());return FALSE;}
    BOOL ok=TRUE;wchar_t section[32],key[32];
    ok=store_set_int(L"files",L"Manager",L"Layout",fm.layout)&&ok;
    ok=store_set_int(L"files",L"Manager",L"NavigationTree",fm.navigation_tree)&&ok;
    for(int layout=0;layout<12;layout++)for(int i=0;i<split_count(layout);i++){swprintf(key,32,L"Split%d_%d",layout,i);ok=store_set_int(L"files",L"Manager",key,fm.splits[layout][i])&&ok;}
    ok=store_set_int(L"files",L"Manager",L"FavoriteCount",fm.favorite_count)&&ok;
    for(int i=0;i<fm.favorite_count;i++){swprintf(key,32,L"Item%d",i);ok=store_set(L"files",L"Favorites",key,fm.favorites[i])&&ok;}
    for(int i=0;i<4;i++){
        Pane *p=&fm.panes[i];swprintf(section,32,L"Pane%d",i);
        ok=store_set_int(L"files",section,L"Count",p->count)&&ok;ok=store_set_int(L"files",section,L"Selected",p->selected)&&ok;
        for(int j=0;j<p->count;j++){swprintf(key,32,L"Tab%d",j);ok=store_set(L"files",section,key,p->items[j]->location)&&ok;}
    }
    if(!store_end(ok)){status_text(store_error());return FALSE;}
    fm.dirty=FALSE;return TRUE;
}
static void evict_views(ULONGLONG now){
    BOOL hidden=!IsWindowVisible(fm.window)||IsIconic(GetAncestor(fm.window,GA_ROOT));
    for(int i=0;i<4;i++)for(int j=0;j<fm.panes[i].count;j++){
        Tab *t=fm.panes[i].items[j];
        if(hidden&&t->browser&&!t->hidden_since)t->hidden_since=now;
        if(t->browser&&t->hidden_since&&now-t->hidden_since>=VIEW_IDLE_MS)suspend_tab(t);
    }
}
static LRESULT CALLBACK pane_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    Pane *p=(Pane*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(msg==WM_NCCREATE){p=(Pane*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);p->window=h;}
    if(!p)return DefWindowProcW(h,msg,wp,lp);
    switch(msg){
    case WM_COMMAND:activate(p);if(HIWORD(wp)==BN_CLICKED)command(p,LOWORD(wp));return 0;
    case WM_NOTIFY:if(((NMHDR*)lp)->hwndFrom==p->tabs&&((NMHDR*)lp)->code==TCN_SELCHANGE){activate(p);select_tab(p,TabCtrl_GetCurSel(p->tabs));save_session();}return 0;
    case WM_SIZE:pane_layout(p);return 0;
    case WM_DRAWITEM:{DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT*)lp;return draw->hwndItem==p->tabs?draw_tab(p,draw):draw_button(draw);}
    case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(236,241,249));SetBkColor((HDC)wp,RGB(22,29,42));return (LRESULT)fm.panel;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,fm.panel);
        if(p->index==fm.active){HBRUSH b=CreateSolidBrush(RGB(105,151,224));FrameRect(dc,&r,b);DeleteObject(b);}EndPaint(h,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    }return DefWindowProcW(h,msg,wp,lp);
}
static void initialize_panes(void){
    for(int i=0;i<4;i++){
        Pane *p=&fm.panes[i];p->index=i;
        p->window=CreateWindowExW(WS_EX_CONTROLPARENT,PANE_CLASS,nova_text(L"目录窗格",L"Folder pane"),WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,300,300,fm.window,NULL,GetModuleHandleW(NULL),p);
        p->tabs=child(p->window,WC_TABCONTROLW,nova_text(L"目录标签",L"Folder tabs"),TCS_FOCUSNEVER|TCS_OWNERDRAWFIXED,C_TABS);
        p->add=button(p->window,nova_text(L"新页",L"New tab"),C_NEWTAB);p->close=button(p->window,nova_text(L"关页",L"Close tab"),C_CLOSETAB);
        p->back=button(p->window,nova_text(L"后退",L"Back"),C_BACK);p->forward=button(p->window,nova_text(L"前进",L"Forward"),C_FORWARD);p->up=button(p->window,nova_text(L"上级",L"Up"),C_UP);p->refresh=button(p->window,nova_text(L"刷新",L"Refresh"),C_REFRESH);
        p->address=child(p->window,L"EDIT",L"",ES_AUTOHSCROLL,C_ADDRESS);SendMessageW(p->address,EM_SETLIMITTEXT,LOCATION_SIZE-1,0);SendMessageW(p->address,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"目录或命令（> 强制命令）  Ctrl+L",L"Folder or command (> forces command)  Ctrl+L"));
        p->go=button(p->window,nova_text(L"转到",L"Go"),C_GO);
        add_tip(p->add,nova_text(L"新建标签 (Ctrl+T)",L"New tab (Ctrl+T)"));add_tip(p->close,nova_text(L"关闭标签 (Ctrl+W)",L"Close tab (Ctrl+W)"));add_tip(p->back,nova_text(L"后退 (Alt+←)",L"Back (Alt+Left)"));add_tip(p->forward,nova_text(L"前进 (Alt+→)",L"Forward (Alt+Right)"));add_tip(p->up,nova_text(L"上级目录 (Alt+↑)",L"Parent folder (Alt+Up)"));add_tip(p->refresh,nova_text(L"刷新 (F5)",L"Refresh (F5)"));add_tip(p->address,nova_text(L"输入目录可切换；输入命令可在当前目录启动 cmd；> 强制按命令执行",L"Enter a folder to navigate, or a command to start cmd here; > forces command mode"));add_tip(p->go,nova_text(L"打开目录或在当前目录执行命令",L"Open the folder or run the command in the current folder"));
        wchar_t section[32],key[32],location[LOCATION_SIZE];swprintf(section,32,L"Pane%d",i);
        int count=session_int(section,L"Count",1);if(count<1||count>TAB_LIMIT)count=1;
        for(int j=0;j<count;j++){swprintf(key,32,L"Tab%d",j);session_get(section,key,START_FOLDER,location,LOCATION_SIZE);if(!add_tab(p,location))break;}
        int selected=session_int(section,L"Selected",0);select_tab(p,selected>=0&&selected<p->count?selected:0);
    }
}
static void update_status(void){
    Tab *t=current(&fm.panes[fm.active]);IFolderView2 *v=NULL;
    if(GetTickCount64()<fm.notice_until||!t||!t->browser||t->pending||FAILED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IFolderView2,(void**)&v)))return;
    int all=0,selected=0;IFolderView2_ItemCount(v,SVGIO_ALLVIEW,&all);IFolderView2_ItemCount(v,SVGIO_SELECTION,&selected);IFolderView2_Release(v);
    wchar_t message[240];swprintf(message,240,nova_text(L"窗格 %d   ·   %d 项   ·   已选择 %d 项      Ctrl+L 目录/命令   Ctrl+T 新标签   F6 切换窗格",L"Pane %d   ·   %d items   ·   %d selected      Ctrl+L folder/command   Ctrl+T new tab   F6 next pane"),fm.active+1,all,selected);SetWindowTextW(fm.status,message);
}
static LRESULT CALLBACK manager_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{fm.window=h;fm.loading=TRUE;
        HDC dc=GetDC(h);fm.dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(h,dc);
        fm.background=CreateSolidBrush(RGB(14,19,29));fm.panel=CreateSolidBrush(RGB(22,29,42));
        fm.font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        fm.icon_font=CreateFontW(-scale(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe MDL2 Assets");
        fm.tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,h,NULL,GetModuleHandleW(NULL),NULL);
        const wchar_t *names_zh[]={L"四窗格",L"目录收藏",L"视图",L"复制",L"剪切",L"粘贴",L"重命名",L"删除",L"新建文件夹",L"帮助"};
        const wchar_t *names_en[]={L"Four panes",L"Favorites",L"View",L"Copy",L"Cut",L"Paste",L"Rename",L"Delete",L"New folder",L"Help"};
        const wchar_t *tips_zh[]={L"切换窗格布局",L"目录收藏",L"切换文件视图",L"复制 (Ctrl+C)",L"剪切 (Ctrl+X)",L"粘贴 (Ctrl+V)",L"重命名 (F2)",L"删除 (Delete)",L"新建文件夹 (Ctrl+Shift+N)",L"帮助与快捷键"};
        const wchar_t *tips_en[]={L"Change pane layout",L"Folder favorites",L"Change file view",L"Copy (Ctrl+C)",L"Cut (Ctrl+X)",L"Paste (Ctrl+V)",L"Rename (F2)",L"Delete (Delete)",L"New folder (Ctrl+Shift+N)",L"Help and keyboard shortcuts"};
        const wchar_t **names=nova_english?names_en:names_zh,**tips=nova_english?tips_en:tips_zh;
        for(int i=0;i<10;i++){fm.toolbar[i]=button(h,names[i],C_LAYOUT+i);add_tip(fm.toolbar[i],tips[i]);}
        fm.status=child(h,L"STATIC",nova_text(L"正在加载目录…",L"Loading folders…"),SS_LEFTNOWORDWRAP,300);
        fm.layout=session_int(L"Manager",L"Layout",0);if(fm.layout<0||fm.layout>11)fm.layout=0;load_split_positions();
        fm.navigation_tree=session_int(L"Manager",L"NavigationTree",0)!=0;
        fm.favorite_count=session_int(L"Manager",L"FavoriteCount",0);if(fm.favorite_count<0||fm.favorite_count>FAVORITE_LIMIT)fm.favorite_count=0;
        for(int i=0;i<fm.favorite_count;i++){wchar_t key[32];swprintf(key,32,L"Item%d",i);session_get(L"Favorites",key,L"",fm.favorites[i],LOCATION_SIZE);}
        initialize_panes();fm.loading=FALSE;fm.active=0;arrange();SetTimer(h,1,1500,NULL);
        BOOL dark=TRUE;DwmSetWindowAttribute(h,20,&dark,sizeof(dark));return 0;}
    case WM_SIZE:arrange();return 0;
    case WM_SETCURSOR:
        if(LOWORD(lp)==HTCLIENT){POINT point;int split;GetCursorPos(&point);ScreenToClient(h,&point);int axis=hit_splitter(point,&split);if(axis){SetCursor(LoadCursorW(NULL,axis==1?IDC_SIZEWE:IDC_SIZENS));return TRUE;}}
        break;
    case WM_LBUTTONDOWN:{POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int split,axis=hit_splitter(point,&split);if(axis){
        fm.splitter_dragging=TRUE;fm.drag_layout=fm.layout;fm.drag_split=split;fm.drag_original=fm.splits[fm.layout][split];SetCapture(h);SetCursor(LoadCursorW(NULL,axis==1?IDC_SIZEWE:IDC_SIZENS));return 0;}break;}
    case WM_MOUSEMOVE:if(fm.splitter_dragging){POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};update_splitter(point);SetCursor(LoadCursorW(NULL,split_vertical(fm.drag_layout,fm.drag_split)?IDC_SIZEWE:IDC_SIZENS));return 0;}break;
    case WM_LBUTTONUP:if(fm.splitter_dragging){POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};update_splitter(point);fm.splitter_dragging=FALSE;if(GetCapture()==h)ReleaseCapture();save_session();return 0;}break;
    case WM_CANCELMODE:cancel_splitter(TRUE);return 0;
    case WM_CAPTURECHANGED:if(fm.splitter_dragging)cancel_splitter(TRUE);return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)lp)->ptMinTrackSize.x=scale(880);((MINMAXINFO*)lp)->ptMinTrackSize.y=scale(640);return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==C_LAYOUT){
            HMENU m=CreatePopupMenu();for(int i=0;i<12;i++)AppendMenuW(m,MF_STRING|(fm.layout==i?MF_CHECKED:0),i+1,layout_name(i));
            RECT r;GetWindowRect(fm.toolbar[0],&r);UINT id=TrackPopupMenu(m,TPM_RETURNCMD,r.left,r.bottom,0,h,NULL);DestroyMenu(m);
            if(id){fm.layout=id-1;arrange();save_session();}return 0;
        }
        if(LOWORD(wp)==C_FAVORITES){popup_favorites();return 0;}
        if(LOWORD(wp)==C_VIEW){view_menu();return 0;}
        if(LOWORD(wp)==C_HELP){MessageBoxW(h,nova_text(L"每个窗格独立浏览目录，可使用系统右键菜单、排序、拖放和缩略图。拖动窗格边界可调整大小。\n\nCtrl+L：目录/命令地址栏（支持环境变量）\n输入目录：在当前窗格切换\n输入命令：以当前目录为 cmd 工作目录执行\n> 命令：强制按命令执行\nAlt+左 / 右：后退 / 前进\nAlt+上：上级目录\nCtrl+T / Ctrl+W：新建 / 关闭标签\nCtrl+Tab：下一个标签\nF6：下一个窗格\nF5：刷新\nCtrl+C / X / V：复制 / 剪切 / 粘贴\nF2：重命名    Delete：删除\nCtrl+Shift+N：新建文件夹\n\n复制后点击目标窗格再粘贴；拖放行为和覆盖提示由 Windows 处理。\n目录标签、布局、窗格比例和收藏在关闭后恢复。",L"Each pane browses independently and supports Windows context menus, sorting, drag and drop, and thumbnails. Drag a pane divider to resize it.\n\nCtrl+L: folder/command address bar (environment variables supported)\nEnter a folder: navigate the current pane\nEnter a command: run it with the current folder as the cmd working directory\n> command: force command mode\nAlt+Left / Right: back / forward\nAlt+Up: parent folder\nCtrl+T / Ctrl+W: new / close tab\nCtrl+Tab: next tab\nF6: next pane\nF5: refresh\nCtrl+C / X / V: copy / cut / paste\nF2: rename    Delete: delete\nCtrl+Shift+N: new folder\n\nAfter copying, click the destination pane and paste. Windows handles drag-and-drop behavior and overwrite prompts.\nFolder tabs, layouts, pane proportions, and favorites are restored after closing."),nova_text(L"NOVA 文件管理",L"NOVA File Manager"),MB_OK);return 0;}
        command(&fm.panes[fm.active],LOWORD(wp));return 0;
    case WM_DRAWITEM:return draw_button((DRAWITEMSTRUCT*)lp);
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,RGB(167,181,202));SetBkColor((HDC)wp,RGB(14,19,29));return (LRESULT)fm.background;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT r;GetClientRect(h,&r);FillRect(dc,&r,fm.background);EndPaint(h,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_TIMER:if(wp==SAVE_TIMER){flush_session();return 0;}evict_views(GetTickCount64());if(IsWindowVisible(h)&&!IsIconic(GetAncestor(h,GA_ROOT)))update_status();return 0;
    case WM_SHOWWINDOW:if(wp)PostMessageW(h,WM_APP+42,0,0);return 0;
    case WM_APP+42:arrange();return 0;
    case WM_APP+41:save_session();return 0;
    case WM_CLOSE:cancel_splitter(FALSE);save_session();flush_session();fm.closing=TRUE;DestroyWindow(h);return 0;
    case WM_DESTROY:
        fm.closing=TRUE;KillTimer(h,1);KillTimer(h,SAVE_TIMER);
        for(int i=0;i<4;i++){for(int j=0;j<fm.panes[i].count;j++)release_tab(fm.panes[i].items[j]);fm.panes[i].count=0;}
        DeleteObject(fm.font);DeleteObject(fm.icon_font);DeleteObject(fm.background);DeleteObject(fm.panel);fm.window=NULL;return 0;
    }return DefWindowProcW(h,msg,wp,lp);
}
HWND file_manager_open(HWND owner,const wchar_t *settings_directory){
    if(!IsWindow(owner)||!store_open(settings_directory))return NULL;
    if(fm.window){ShowWindow(fm.window,SW_SHOW);arrange();return fm.window;}
    ZeroMemory(&fm,sizeof(fm));fm.dpi=96;
    if(wcslen(settings_directory)+20>=MAX_PATH)return NULL;
    swprintf(fm.settings,MAX_PATH,L"%ls\\file-manager.ini",settings_directory);
    if(!migrate_session())return NULL;
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_WIN95_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSW wc={0};wc.hInstance=GetModuleHandleW(NULL);wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.lpfnWndProc=manager_proc;wc.lpszClassName=FM_CLASS;RegisterClassW(&wc);
    wc.lpfnWndProc=pane_proc;wc.lpszClassName=PANE_CLASS;RegisterClassW(&wc);
    RECT area;GetClientRect(owner,&area);
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT,FM_CLASS,nova_text(L"NOVA 文件管理",L"NOVA File Manager"),WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,area.right,area.bottom,owner,NULL,wc.hInstance,NULL);
    if(h){ShowWindow(h,SW_SHOW);arrange();UpdateWindow(h);}return h;
}
BOOL file_manager_message(MSG *msg){
    if(!fm.window||!IsWindowVisible(fm.window)||!(msg->hwnd==fm.window||IsChild(fm.window,msg->hwnd)))return FALSE;
    if(fm.splitter_dragging&&(msg->message==WM_KEYDOWN||msg->message==WM_SYSKEYDOWN)&&msg->wParam==VK_ESCAPE){SendMessageW(fm.window,WM_CANCELMODE,0,0);return TRUE;}
    Pane *p=&fm.panes[fm.active];
    for(int i=0;i<4;i++)if(msg->hwnd==fm.panes[i].window||IsChild(fm.panes[i].window,msg->hwnd)){
        p=&fm.panes[i];
        /* Paint, timers and shell notifications must never steal the active pane. */
        if(msg->message==WM_KEYDOWN||msg->message==WM_SYSKEYDOWN||
           msg->message==WM_LBUTTONDOWN||msg->message==WM_RBUTTONDOWN||msg->message==WM_MBUTTONDOWN)activate(p);
        break;
    }
    if(msg->message==WM_KEYDOWN||msg->message==WM_SYSKEYDOWN){
        BOOL ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        int id=0;
        if(ctrl&&msg->wParam=='L'){SetFocus(p->address);SendMessageW(p->address,EM_SETSEL,0,-1);return TRUE;}
        if(ctrl&&msg->wParam=='T')id=C_NEWTAB;
        else if(ctrl&&msg->wParam=='W')id=C_CLOSETAB;
        else if(ctrl&&msg->wParam==VK_TAB){select_tab(p,(p->selected+1)%max(p->count,1));save_session();focus_view(current(p));return TRUE;}
        else if(msg->wParam==VK_F6){RECT r[4];int n=pane_rects(fm.layout,100,100,r);p=&fm.panes[(fm.active+1)%n];activate(p);focus_view(current(p));return TRUE;}
        else if(alt&&msg->wParam==VK_LEFT)id=C_BACK;
        else if(alt&&msg->wParam==VK_RIGHT)id=C_FORWARD;
        else if(alt&&msg->wParam==VK_UP)id=C_UP;
        else if(msg->hwnd==p->address&&msg->wParam==VK_RETURN)id=C_GO;
        else if(msg->wParam==VK_F5)id=C_REFRESH;
        if(id){command(p,id);return TRUE;}
    }
    Tab *t=current(p);IShellView *view=NULL;
    if(msg->message>=WM_KEYFIRST&&msg->message<=WM_KEYLAST&&t&&t->browser&&IsChild(t->host,msg->hwnd)&&SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view))){
        HRESULT hr=IShellView_TranslateAccelerator(view,msg);IShellView_Release(view);if(hr==S_OK)return TRUE;
    }
    if(!IsDialogMessageW(fm.window,msg)){TranslateMessage(msg);DispatchMessageW(msg);}return TRUE;
}
void file_manager_close(void){if(fm.window)SendMessageW(fm.window,WM_CLOSE,0,0);}
void file_manager_hide(void){if(fm.window){cancel_splitter(FALSE);save_session();flush_session();ShowWindow(fm.window,SW_HIDE);evict_views(GetTickCount64());}}
void file_manager_update_visibility(void){if(fm.window)arrange();}
