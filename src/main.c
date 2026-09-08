#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdio.h>
#include <wchar.h>
#include "core/workspace.h"
#include "core/launch_queue.h"
#include "platform/shell_icons.h"
#include "ui/hold_drag.h"
#include "ui/file_manager.h"
#include "platform/storage.h"
#include "i18n.h"

#define APP_NAME L"NOVA Desktop"
#define APP_CLASS L"NovaDesktopWindowV3"
#define IDI_NOVA 101
#define WM_TRAY (WM_APP+1)
#define WM_RESTORE_NOVA (WM_APP+2)
#define ID_SPACES 101
#define ID_APPS 102
#define ID_ADD 103
#define ID_PIN 104
#define ID_STARTUP 105
#define ID_NEW 106
#define ID_RENAME 107
#define ID_DELETE 108
#define ID_SEARCH 109
#define ID_NAME 110
#define ID_FOLDER 111
#define ID_REMOVE 112
#define ID_OPEN 113
#define ID_QUIT 114
#define ID_HOTKEY 115
#define ID_SETTINGS 116
#define ID_MINIMIZE 117
#define ID_MAXIMIZE 118
#define ID_CLOSE 119
#define ID_FILES 120
#define ID_DESKTOP 121
#define ID_LAUNCH_ALL 122
#define ID_LANGUAGE_ZH 123
#define ID_LANGUAGE_EN 124
#define STATS_TIMER 1
#define START_PIN_TIMER 2
#define START_DESKTOP_TIMER 4

static HoldDrag hold_drag;
static Workspace spaces[MAX_WORKSPACES];
static int space_count=4, active_space, dpi=96;
static HWND main_window, space_list, app_list, search_edit, name_edit;
static HWND add_button, folder_button, launch_button, pin_button, desktop_button, settings_button, new_button, tooltip, hover_button;
static HWND minimize_button, maximize_button, close_button;
static HWND files_view;
static BOOL files_page;
static BOOL storage_failed;
static HFONT body_font, small_font, title_font, brand_font;
static HBRUSH background, panel_brush;
static HIMAGELIST images;
static NOTIFYICONDATAW tray;
static UINT taskbar_message;
static BOOL pinned, prefer_pin, desktop_mode, prefer_desktop, editing_name, startup_enabled, test_mode, bulk_launch_running;
BOOL nova_english=FALSE;
static int test_launch_count;
static unsigned paint_generation;
static RECT floating_rect;
static BOOL floating_was_zoomed;
static LONG_PTR floating_style, floating_exstyle;
static wchar_t config_path[MAX_PATH], notice[256]=L"拖入应用、快捷方式或文件夹，添加到当前工作区。";
static ULONGLONG prev_idle, prev_kernel, prev_user;
static int system_cpu, memory_load;
static const COLORREF BG=RGB(14,19,29), PANEL=RGB(22,29,42), TEXT=RGB(236,241,249), MUTED=RGB(167,181,202);

static int px(int x) { return MulDiv(x,dpi,96); }
static int caption_height(void){return px(30);}
static void refresh_apps(void);
static void layout_controls(void);
static BOOL save_config(void);
static BOOL set_pinned(BOOL value);
static BOOL set_desktop_mode(BOOL value);
static void refresh_spaces(void);
static void launch_workspace(void);
static void error_message(const wchar_t *message);
static void end_name_edit(BOOL commit);
static void apply_language(BOOL english);
static void show_workspace_page(void){
    if(active_space==0){if(IsWindow(files_view))SetFocus(files_view);return;}
    files_page=FALSE;file_manager_hide();
    HWND controls[]={search_edit,add_button,folder_button,launch_button,app_list};
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);i++)ShowWindow(controls[i],SW_SHOW);
    layout_controls();SetFocus(app_list);
}
static void open_file_manager(void){
    end_name_edit(TRUE);
    wchar_t directory[MAX_PATH];lstrcpynW(directory,config_path,MAX_PATH);
    wchar_t *slash=wcsrchr(directory,L'\\');if(slash)*slash=0;
    files_view=file_manager_open(main_window,directory);
    if(!files_view){error_message(nova_text(L"无法打开文件管理页面。",L"Unable to open the file manager."));return;}
    files_page=TRUE;
    HWND controls[]={search_edit,add_button,folder_button,launch_button,app_list,name_edit};
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);i++)ShowWindow(controls[i],SW_HIDE);
    layout_controls();SetFocus(files_view);
}

static void set_notice(const wchar_t *text) {
    lstrcpynW(notice,text,256);
    if(main_window) { RECT r; GetClientRect(main_window,&r); r.top=r.bottom-px(62); InvalidateRect(main_window,&r,FALSE); }
}
static void error_message(const wchar_t *message) { if(test_mode){fwprintf(stderr,L"NOVA error: %ls\n",message);return;} MessageBoxW(main_window,message,APP_NAME,MB_OK|MB_ICONWARNING); }

static void defaults(void) {
    ZeroMemory(spaces,sizeof(spaces)); space_count=4; active_space=0;
    const wchar_t *names[]={L"目录",L"开发",L"创作",L"专注"};
    for(int i=0;i<4;i++){lstrcpyW(spaces[i].name,names[i]);spaces[i].items_loaded=1;}
    spaces[1].app_count=1; lstrcpyW(spaces[1].apps[0].name,L"PowerShell"); lstrcpyW(spaces[1].apps[0].target,L"powershell.exe");
}

static BOOL legacy_placeholder(const wchar_t *value,int number) {
    const wchar_t *p=value;int marks=0;
    while(*p==L'?'){marks++;p++;}
    while(*p==L' ')p++;
    if(!marks)return FALSE;
    if(!*p)return TRUE;
    wchar_t *end=NULL;long parsed=wcstol(p,&end,10);
    while(end&&*end==L' ')end++;
    return parsed==number&&end&&!*end;
}
static BOOL repair_workspace_name(int index){
    if(index==0){if(!lstrcmpW(spaces[0].name,L"目录"))return FALSE;lstrcpyW(spaces[0].name,L"目录");return TRUE;}
    if(index<0||index>=space_count||!legacy_placeholder(spaces[index].name,index+1))return FALSE;
    const wchar_t *names[]={L"目录",L"开发",L"创作",L"专注"};
    if(index<4)lstrcpyW(spaces[index].name,names[index]);else swprintf(spaces[index].name,40,L"工作区 %d",index+1);
    return TRUE;
}
static BOOL repair_item_name(AppItem *item){
    if(!item||!legacy_placeholder(item->name,0))return FALSE;
    const wchar_t *targets[]={L"explorer.exe",L"https://www.bing.com",L"ms-settings:",L"notepad.exe",L"wt.exe",L"powershell.exe",L".",L"mspaint.exe",L"ms-photos:",L"ms-clock:"};
    const wchar_t *names[]={L"文件管理",L"浏览器",L"系统设置",L"记事本",L"终端",L"PowerShell",L"项目目录",L"画图",L"照片",L"时钟"};
    for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++)if(lstrcmpiW(item->target,targets[i])==0){lstrcpyW(item->name,names[i]);return TRUE;}
    const wchar_t *base=wcsrchr(item->target,L'\\');lstrcpynW(item->name,base?base+1:item->target,64);return TRUE;
}
static BOOL repair_loaded_names(void){
    BOOL repaired=FALSE;
    for(int i=0;i<space_count;i++){
        repaired=repair_workspace_name(i)||repaired;
        if(spaces[i].items_loaded)for(int j=0;j<spaces[i].app_count;j++)repaired=repair_item_name(&spaces[i].apps[j])||repaired;
    }
    return repaired;
}

static BOOL ensure_storage(void){
    wchar_t directory[MAX_PATH];lstrcpynW(directory,config_path,MAX_PATH);
    wchar_t *slash=wcsrchr(directory,L'\\');if(!slash)return FALSE;*slash=0;
    if(!store_open(directory)){storage_failed=TRUE;set_notice(store_error());return FALSE;}return TRUE;
}
static BOOL ensure_items(int index){
    if(index<0||index>=space_count)return FALSE;
    if(spaces[index].items_loaded)return TRUE;
    if(!store_load_items(&spaces[index])){storage_failed=TRUE;set_notice(store_error());return FALSE;}return TRUE;
}
static BOOL save_config(void){
    if(storage_failed||!ensure_storage())return FALSE;
    BOOL ok=store_save_workspaces(spaces,space_count,active_space,prefer_pin);
    if(ok)ok=store_set_int(L"app",L"Nova",L"DesktopMode",prefer_desktop);
    if(ok)ok=store_set_int(L"app",L"Nova",L"Language",nova_english?1:0);
    if(!ok)set_notice(store_error());
    return ok;
}

static void load_legacy_config(void) {
    defaults();
    if(GetFileAttributesW(config_path)==INVALID_FILE_ATTRIBUTES)return;
    int n=(int)GetPrivateProfileIntW(L"Nova",L"WorkspaceCount",4,config_path);
    if(n<1||n>MAX_WORKSPACES){storage_failed=TRUE;set_notice(nova_text(L"旧配置的工作区数量无效，已停止迁移并保留原文件。",L"The legacy workspace count is invalid. Migration stopped and the original file was preserved."));return;}
    space_count=n;
    active_space=(int)GetPrivateProfileIntW(L"Nova",L"Active",0,config_path);
    if(active_space<0||active_space>=space_count)active_space=0;
    prefer_pin=GetPrivateProfileIntW(L"Nova",L"AlwaysOnTop",0,config_path)!=0;
    for(int s=0;s<space_count;s++){
        wchar_t section[32],key[32],fallback[40]; swprintf(section,32,L"Workspace%d",s);swprintf(fallback,40,L"工作区 %d",s+1);
        GetPrivateProfileStringW(section,L"Name",fallback,spaces[s].name,40,config_path);
        n=(int)GetPrivateProfileIntW(section,L"AppCount",0,config_path);
        if(n<0||n>MAX_APPS){storage_failed=TRUE;set_notice(nova_text(L"旧配置的项目数量超过容量，已停止迁移并保留原文件。",L"The legacy item count exceeds capacity. Migration stopped and the original file was preserved."));return;}
        spaces[s].app_count=n;
        for(int a=0;a<spaces[s].app_count;a++){
            swprintf(key,32,L"App%dName",a);GetPrivateProfileStringW(section,key,L"启动项",spaces[s].apps[a].name,64,config_path);
            swprintf(key,32,L"App%dTarget",a);GetPrivateProfileStringW(section,key,L"",spaces[s].apps[a].target,MAX_PATH,config_path);
        }
    }
    /* Older releases wrote ANSI INIs. Repair only recognizable placeholder names. */
    BOOL repaired=repair_loaded_names();
    if(repaired)set_notice(nova_text(L"已在数据库迁移中修复旧版默认名称；原 INI 保留。",L"Legacy default names were repaired during database migration; the original INI was preserved."));
}

static void load_config(void){
    storage_failed=FALSE;defaults();if(!ensure_storage())return;
    nova_english=store_int(L"app",L"Nova",L"Language",0)!=0;
    lstrcpynW(notice,nova_text(L"拖入应用、快捷方式或文件夹，添加到当前工作区。",L"Drop apps, shortcuts, files, or folders here to add them to this workspace."),256);
    int count=store_load_workspaces(spaces);
    if(count<0){storage_failed=TRUE;set_notice(store_error());return;}
    if(count==0){
        load_legacy_config();
        if(storage_failed)return;
        for(int i=0;i<space_count;i++)spaces[i].items_loaded=1;
        if(!save_config()){storage_failed=TRUE;return;}
        if(!store_backup())set_notice(nova_text(L"数据库已保存，但备份失败；原 INI 仍然保留。",L"The database was saved, but backup failed; the original INI is still preserved."));
        count=store_load_workspaces(spaces);
    }
    if(count<1){storage_failed=TRUE;return;}
    space_count=count;active_space=store_int(L"app",L"Nova",L"Active",0);
    if(active_space<0||active_space>=space_count)active_space=0;
    prefer_pin=store_int(L"app",L"Nova",L"AlwaysOnTop",0)!=0;
    prefer_desktop=store_int(L"app",L"Nova",L"DesktopMode",0)!=0;
    if(prefer_desktop)prefer_pin=FALSE;
    if(!ensure_items(active_space))return;
    if(repair_loaded_names()){
        save_config();
        set_notice(nova_text(L"已修复旧版留下的乱码默认名称。",L"Corrupted legacy default names were repaired."));
    }
}

static BOOL startup_command(wchar_t *command,size_t count) {
    wchar_t exe[MAX_PATH]; DWORD n=GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(!n||n>=MAX_PATH)return FALSE;
    return swprintf(command,count,L"\"%ls\" --startup",exe)>0;
}
static BOOL read_startup(void) {
    HKEY key; wchar_t stored[MAX_PATH+40],expected[MAX_PATH+40]; DWORD bytes=sizeof(stored),type=0;
    if(!startup_command(expected,MAX_PATH+40))return FALSE;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return FALSE;
    LONG result=RegQueryValueExW(key,L"NOVA Desktop",NULL,&type,(BYTE*)stored,&bytes);RegCloseKey(key);
    stored[MAX_PATH+39]=0;
    return result==ERROR_SUCCESS&&type==REG_SZ&&lstrcmpiW(stored,expected)==0;
}
static void toggle_startup(void) {
    HKEY key; wchar_t command[MAX_PATH+40];
    if(!startup_command(command,MAX_PATH+40))return;
    LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
    if(result==ERROR_SUCCESS){
        result=startup_enabled?RegDeleteValueW(key,L"NOVA Desktop"):RegSetValueExW(key,L"NOVA Desktop",0,REG_SZ,(BYTE*)command,(DWORD)((wcslen(command)+1)*sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if(result!=ERROR_SUCCESS&&result!=ERROR_FILE_NOT_FOUND){error_message(nova_text(L"无法更新开机启动设置。请检查当前用户的注册表访问权限。",L"Unable to update startup settings. Check access to the current user's registry."));return;}
    startup_enabled=read_startup();
    set_notice(startup_enabled?nova_text(L"已开启：下次登录 Windows 时启动 NOVA，并恢复固定状态。",L"Startup enabled: NOVA will open at the next Windows sign-in and restore its window mode."):nova_text(L"已关闭开机启动。你仍可手动打开 NOVA。",L"Startup disabled. You can still open NOVA manually."));
}

static void redraw_surface(void){
    layout_controls();
    RedrawWindow(main_window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN|RDW_UPDATENOW);
}
static BOOL set_desktop_mode(BOOL value){
    if(value==desktop_mode)return TRUE;
    if(value){
        if(pinned&&!set_pinned(FALSE))return FALSE;
        GetWindowRect(main_window,&floating_rect);floating_was_zoomed=IsZoomed(main_window);
        if(IsZoomed(main_window))ShowWindow(main_window,SW_RESTORE);
        floating_style=GetWindowLongPtrW(main_window,GWL_STYLE);floating_exstyle=GetWindowLongPtrW(main_window,GWL_EXSTYLE);
        LONG_PTR style=floating_style&~(WS_MAXIMIZE|WS_MINIMIZE);
        LONG_PTR exstyle=(floating_exstyle&~(WS_EX_TOPMOST|WS_EX_APPWINDOW))|WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(main_window,GWL_STYLE,style);SetWindowLongPtrW(main_window,GWL_EXSTYLE,exstyle);
        RECT area;if(!SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0))SetRect(&area,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
        int margin=px(32),width=px(900),height=px(640);
        if(width>area.right-area.left-margin*2)width=area.right-area.left-margin*2;
        if(height>area.bottom-area.top-px(80))height=area.bottom-area.top-px(80);
        int x=area.right-width-margin,y=area.top+px(44);if(x<area.left)x=area.left;if(y+height>area.bottom)y=area.top+(area.bottom-area.top-height)/2;
        desktop_mode=TRUE;prefer_desktop=TRUE;unsigned before_paint=paint_generation;
        BOOL placed=SetWindowPos(main_window,HWND_BOTTOM,x,y,width,height,SWP_FRAMECHANGED|SWP_SHOWWINDOW|SWP_NOACTIVATE);
        redraw_surface();
        if(!placed||!IsWindowVisible(main_window)||paint_generation==before_paint){
            desktop_mode=FALSE;prefer_desktop=FALSE;SetWindowLongPtrW(main_window,GWL_STYLE,floating_style);SetWindowLongPtrW(main_window,GWL_EXSTYLE,floating_exstyle);
            SetWindowPos(main_window,HWND_NOTOPMOST,floating_rect.left,floating_rect.top,floating_rect.right-floating_rect.left,floating_rect.bottom-floating_rect.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            if(floating_was_zoomed)ShowWindow(main_window,SW_MAXIMIZE);
            redraw_surface();
            error_message(nova_text(L"桌面围栏没有正常绘制，NOVA 已恢复为普通窗口。",L"Desktop fence mode did not render correctly. NOVA has returned to a normal window."));return FALSE;
        }
        SetWindowTextW(desktop_button,nova_text(L"退出桌面围栏",L"Leave desktop fence"));InvalidateRect(desktop_button,NULL,TRUE);save_config();
        set_notice(nova_text(L"已放到桌面围栏，普通应用窗口会显示在 NOVA 上方。",L"NOVA is now on the desktop; normal app windows will appear above it."));return TRUE;
    }
    desktop_mode=FALSE;prefer_desktop=FALSE;
    SetWindowLongPtrW(main_window,GWL_STYLE,floating_style);SetWindowLongPtrW(main_window,GWL_EXSTYLE,floating_exstyle&~WS_EX_TOPMOST);
    SetWindowPos(main_window,HWND_NOTOPMOST,floating_rect.left,floating_rect.top,floating_rect.right-floating_rect.left,floating_rect.bottom-floating_rect.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    if(floating_was_zoomed)ShowWindow(main_window,SW_MAXIMIZE);
    redraw_surface();SetWindowTextW(desktop_button,nova_text(L"放到桌面",L"Place on desktop"));InvalidateRect(desktop_button,NULL,TRUE);save_config();
    set_notice(nova_text(L"已恢复为普通窗口。",L"Restored as a normal window."));return TRUE;
}

/* A WeChat-style pin changes only the topmost band; geometry and ownership stay unchanged. */
static BOOL set_pinned(BOOL value) {
    if(value&&desktop_mode&&!set_desktop_mode(FALSE))return FALSE;
    if(value==pinned)return TRUE;
    LONG_PTR exstyle=GetWindowLongPtrW(main_window,GWL_EXSTYLE);
    SetWindowLongPtrW(main_window,GWL_EXSTYLE,value?(exstyle|WS_EX_TOPMOST):(exstyle&~WS_EX_TOPMOST));
    if(!SetWindowPos(main_window,value?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED)){
        SetWindowLongPtrW(main_window,GWL_EXSTYLE,exstyle);
        error_message(nova_text(L"无法改变窗口置顶状态，请重试。",L"Unable to change the always-on-top state. Try again."));return FALSE;
    }
    pinned=value;prefer_pin=value;save_config();SetWindowTextW(pin_button,pinned?nova_text(L"取消置顶 (F11)",L"Unpin (F11)"):nova_text(L"窗口置顶 (F11)",L"Always on top (F11)"));
    InvalidateRect(pin_button,NULL,TRUE);
    set_notice(pinned?nova_text(L"已置顶。窗口仍可拖动和缩放；再次点击图钉取消。",L"Always on top is enabled. The window can still be moved and resized; click the pin again to disable it."):nova_text(L"已取消置顶。",L"Always on top is disabled."));
    return TRUE;
}

static void text(HDC dc,const wchar_t *str,RECT rect,HFONT font,COLORREF color,UINT flags) {
    HFONT old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,str,-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,old);
}
static RECT box(int x,int y,int w,int h){RECT r={x,y,x+w,y+h};return r;}
static void paint(HDC dc,RECT client) {
    RECT title=box(0,0,client.right,caption_height());HBRUSH titlebrush=CreateSolidBrush(RGB(32,33,33));FillRect(dc,&title,titlebrush);DeleteObject(titlebrush);
    text(dc,APP_NAME,box(px(12),0,client.right-px(296),caption_height()),small_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    int saved=SaveDC(dc);IntersectClipRect(dc,0,caption_height(),client.right,client.bottom);
    SetViewportOrgEx(dc,0,caption_height(),NULL);client.bottom-=caption_height();
    FillRect(dc,&client,background);RECT side=box(0,0,px(220),client.bottom);FillRect(dc,&side,panel_brush);
    text(dc,L"NOVA",box(px(28),px(27),px(164),px(38)),brand_font,TEXT,DT_LEFT);
    text(dc,nova_text(L"工作区",L"WORKSPACES"),box(px(28),px(78),px(160),px(24)),small_font,MUTED,DT_LEFT);
    int left=px(254),width=client.right-left-px(32);
    text(dc,active_space==0?nova_text(L"目录",L"Files"):spaces[active_space].name,box(left,px(27),width-px(148),px(47)),title_font,TEXT,DT_LEFT|DT_END_ELLIPSIS);
    text(dc,nova_text(L"启动项",L"LAUNCH ITEMS"),box(left,px(157),width,px(25)),body_font,TEXT,DT_LEFT);
    RECT status=box(left,client.bottom-px(56),width,px(50));FillRect(dc,&status,background);
    text(dc,notice,box(left,client.bottom-px(49),width,px(23)),small_font,MUTED,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
    wchar_t metrics[80];swprintf(metrics,80,nova_text(L"系统 CPU %d%%    内存 %d%%",L"System CPU %d%%    Memory %d%%"),system_cpu,memory_load);
    text(dc,metrics,box(px(20),client.bottom-px(38),px(188),px(25)),small_font,MUTED,DT_LEFT);
    RestoreDC(dc,saved);
}
static void create_fonts(void) {
    body_font=CreateFontW(-px(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    small_font=CreateFontW(-px(12),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    title_font=CreateFontW(-px(32),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    brand_font=CreateFontW(-px(29),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
}
static HWND control(const wchar_t *cls,const wchar_t *caption,DWORD style,int id) {
    HWND h=CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,10,10,main_window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(h,WM_SETFONT,(WPARAM)body_font,TRUE);return h;
}
static HWND button(const wchar_t *caption,int id){return control(L"BUTTON",caption,BS_OWNERDRAW,id);}
static void add_tooltip(HWND h,const wchar_t *caption){
    TOOLINFOW t={0};t.cbSize=sizeof(t);t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.hwnd=main_window;t.uId=(UINT_PTR)h;t.lpszText=(LPWSTR)caption;
    SendMessageW(tooltip,TTM_ADDTOOLW,0,(LPARAM)&t);
}
static void update_tooltip(HWND h,const wchar_t *caption){
    TOOLINFOW t={0};t.cbSize=sizeof(t);t.uFlags=TTF_IDISHWND;t.hwnd=main_window;t.uId=(UINT_PTR)h;t.lpszText=(LPWSTR)caption;
    SendMessageW(tooltip,TTM_UPDATETIPTEXTW,0,(LPARAM)&t);
}
static void apply_language(BOOL english){
    if(nova_english==english&&main_window)return;
    nova_english=english;
    if(!main_window)return;
    SetWindowTextW(new_button,nova_text(L"新建工作区",L"New workspace"));
    SetWindowTextW(settings_button,nova_text(L"设置",L"Settings"));
    SetWindowTextW(pin_button,pinned?nova_text(L"取消置顶 (F11)",L"Unpin (F11)"):nova_text(L"窗口置顶 (F11)",L"Always on top (F11)"));
    SetWindowTextW(desktop_button,desktop_mode?nova_text(L"退出桌面围栏",L"Leave desktop fence"):nova_text(L"放到桌面",L"Place on desktop"));
    SetWindowTextW(minimize_button,nova_text(L"最小化",L"Minimize"));
    SetWindowTextW(maximize_button,IsZoomed(main_window)?nova_text(L"还原",L"Restore"):nova_text(L"最大化",L"Maximize"));
    SetWindowTextW(close_button,nova_text(L"关闭",L"Close"));
    SetWindowTextW(add_button,nova_text(L"添加文件",L"Add file"));SetWindowTextW(folder_button,nova_text(L"添加文件夹",L"Add folder"));SetWindowTextW(launch_button,nova_text(L"全部启动",L"Launch all"));
    SetWindowTextW(app_list,nova_text(L"工作区启动项",L"Workspace launch items"));
    SendMessageW(search_edit,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"搜索当前工作区  Ctrl+K",L"Search this workspace  Ctrl+K"));
    update_tooltip(desktop_button,nova_text(L"桌面围栏 / 恢复普通窗口",L"Desktop fence / restore normal window"));
    update_tooltip(pin_button,nova_text(L"置顶 / 取消置顶 (F11)",L"Always on top / unpin (F11)"));
    update_tooltip(settings_button,nova_text(L"设置：语言、开机启动、工作区管理",L"Settings: language, startup, and workspace management"));
    update_tooltip(new_button,nova_text(L"新建工作区（最多 8 个）",L"New workspace (up to 8)"));
    update_tooltip(launch_button,nova_text(L"将当前工作区的每个项目各打开一次",L"Open every item in this workspace once"));
    update_tooltip(minimize_button,nova_text(L"最小化",L"Minimize"));update_tooltip(maximize_button,nova_text(L"最大化 / 还原",L"Maximize / restore"));update_tooltip(close_button,nova_text(L"关闭",L"Close"));
    refresh_spaces();
    file_manager_close();files_view=NULL;
    if(files_page)open_file_manager();
    set_notice(nova_text(L"界面语言已切换为简体中文。",L"Interface language changed to English."));
    save_config();layout_controls();InvalidateRect(main_window,NULL,TRUE);
}
static void draw_icon(DRAWITEMSTRUCT *d){
    BOOL active=(d->CtlID==ID_PIN&&pinned)||(d->CtlID==ID_DESKTOP&&desktop_mode);
    BOOL caption=d->CtlID!=ID_NEW;
    COLORREF fill=active?RGB(53,77,122):(hover_button==d->hwndItem||(d->itemState&ODS_SELECTED))?RGB(39,51,71):d->CtlID==ID_NEW?PANEL:BG;
    if(caption)fill=active?RGB(59,70,82):(hover_button==d->hwndItem||(d->itemState&ODS_SELECTED))?(d->CtlID==ID_CLOSE?RGB(196,43,28):RGB(57,58,58)):RGB(32,33,33);
    HBRUSH bg=CreateSolidBrush(fill);FillRect(d->hDC,&d->rcItem,bg);DeleteObject(bg);
    COLORREF color=(d->itemState&ODS_DISABLED)?RGB(112,128,150):active?RGB(193,213,255):TEXT;
    HPEN pen=CreatePen(PS_SOLID,caption?px(1):px(2),color);HGDIOBJ oldpen=SelectObject(d->hDC,pen),oldbrush=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
    int x=(d->rcItem.left+d->rcItem.right)/2,y=(d->rcItem.top+d->rcItem.bottom)/2;
    if(d->CtlID==ID_NEW){MoveToEx(d->hDC,x-px(7),y,NULL);LineTo(d->hDC,x+px(7),y);MoveToEx(d->hDC,x,y-px(7),NULL);LineTo(d->hDC,x,y+px(7));}
    else if(d->CtlID==ID_MINIMIZE){MoveToEx(d->hDC,x-px(5),y,NULL);LineTo(d->hDC,x+px(5),y);}
    else if(d->CtlID==ID_CLOSE){MoveToEx(d->hDC,x-px(5),y-px(5),NULL);LineTo(d->hDC,x+px(5),y+px(5));MoveToEx(d->hDC,x+px(5),y-px(5),NULL);LineTo(d->hDC,x-px(5),y+px(5));}
    else if(d->CtlID==ID_MAXIMIZE){
        if(IsZoomed(main_window)){Rectangle(d->hDC,x-px(3),y-px(5),x+px(5),y+px(3));HBRUSH b=CreateSolidBrush(fill);RECT r=box(x-px(5),y-px(3),px(8),px(8));FillRect(d->hDC,&r,b);DeleteObject(b);Rectangle(d->hDC,r.left,r.top,r.right,r.bottom);}
        else Rectangle(d->hDC,x-px(5),y-px(5),x+px(5),y+px(5));
    }
    else if(d->CtlID==ID_PIN){
        POINT p[]={{-4,-9},{5,-9},{4,-3},{8,2},{-7,2},{-3,-3},{-4,-9}};
        for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);i++){p[i].x=x+MulDiv(px(p[i].x),2,3);p[i].y=y+MulDiv(px(p[i].y),2,3);}Polyline(d->hDC,p,7);
        MoveToEx(d->hDC,x,y+px(1),NULL);LineTo(d->hDC,x,y+px(7));
    }else if(d->CtlID==ID_DESKTOP){
        Rectangle(d->hDC,x-px(8),y-px(7),x+px(8),y+px(7));
        MoveToEx(d->hDC,x-px(4),y-px(3),NULL);LineTo(d->hDC,x-px(1),y-px(3));
        MoveToEx(d->hDC,x+px(2),y-px(3),NULL);LineTo(d->hDC,x+px(5),y-px(3));
        MoveToEx(d->hDC,x-px(4),y+px(2),NULL);LineTo(d->hDC,x-px(1),y+px(2));
        MoveToEx(d->hDC,x+px(2),y+px(2),NULL);LineTo(d->hDC,x+px(5),y+px(2));
    }else{
        POINT p[]={{-3,-10},{3,-10},{3,-7},{5,-5},{8,-6},{11,-1},{8,1},{7,4},{9,6},{5,10},{3,7},{-1,8},{-2,11},{-7,8},{-6,5},{-8,2},{-11,2},{-11,-3},{-8,-3},{-6,-6},{-7,-9},{-3,-10}};
        for(unsigned i=0;i<sizeof(p)/sizeof(p[0]);i++){p[i].x=x+MulDiv(px(p[i].x),2,3);p[i].y=y+MulDiv(px(p[i].y),2,3);}Polyline(d->hDC,p,22);
        Ellipse(d->hDC,x-px(2),y-px(2),x+px(3),y+px(3));
    }
    SelectObject(d->hDC,oldbrush);SelectObject(d->hDC,oldpen);DeleteObject(pen);
    if(d->itemState&ODS_FOCUS){RECT r=d->rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d->hDC,&r);}
}
static void move(HWND h,int x,int y,int w,int height){MoveWindow(h,x,y+caption_height(),w,height,TRUE);}
static void layout_controls(void) {
    if(!app_list)return;
    RECT r;GetClientRect(main_window,&r);r.bottom-=caption_height();int left=px(254),width=r.right-left-px(32);
    move(space_list,px(16),px(112),px(188),r.bottom-px(170));
    move(new_button,px(162),px(68),px(40),px(36));
    HWND caption_buttons[]={desktop_button,pin_button,settings_button,minimize_button,maximize_button,close_button};
    for(int i=0;i<6;i++)MoveWindow(caption_buttons[i],r.right-px(46)*(6-i),0,px(46),caption_height(),TRUE);
    SetWindowTextW(maximize_button,IsZoomed(main_window)?nova_text(L"还原",L"Restore"):nova_text(L"最大化",L"Maximize"));
    move(launch_button,r.right-px(148),px(27),px(116),px(40));
    move(search_edit,left,px(100),width-px(220),px(32));
    move(add_button,r.right-px(236),px(97),px(96),px(38));move(folder_button,r.right-px(132),px(97),px(100),px(38));
    move(app_list,left,px(190),width,r.bottom-px(255));
    move(name_edit,left,px(32),width-px(104),px(39));
    if(IsWindow(files_view))move(files_view,px(228),0,r.right-px(236),r.bottom-px(8));
    if(files_page)file_manager_update_visibility();
    InvalidateRect(main_window,NULL,FALSE);
}
static void refresh_spaces(void) {
    SendMessageW(space_list,LB_RESETCONTENT,0,0);
    for(int i=0;i<space_count;i++)SendMessageW(space_list,LB_ADDSTRING,0,(LPARAM)(i==0?nova_text(L"目录",L"Files"):spaces[i].name));
    SendMessageW(space_list,LB_SETCURSEL,active_space,0);
    EnableWindow(new_button,space_count<MAX_WORKSPACES);
}
static BOOL contains(const wchar_t *value,const wchar_t *query) {
    if(!*query)return TRUE;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE,value,-1,query,-1,NULL,NULL,NULL,0)>=0;
}
static void refresh_apps(void) {
    if(!app_list||!ensure_items(active_space))return;
    if(repair_loaded_names()){save_config();set_notice(nova_text(L"已修复旧版留下的乱码默认名称。",L"Corrupted legacy default names were repaired."));refresh_spaces();}
    wchar_t query[128];GetWindowTextW(search_edit,query,128);
    hold_drag.enabled=!*query;
    SendMessageW(app_list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(app_list);
    HIMAGELIST next=ImageList_Create(px(40),px(40),ILC_COLOR32|ILC_MASK,MAX_APPS,1);
    Workspace *ws=&spaces[active_space];int row=0;
    for(int i=0;i<ws->app_count;i++){
        AppItem *a=&ws->apps[i];if(!contains(a->name,query)&&!contains(a->target,query))continue;
        wchar_t resolved[MAX_PATH];const wchar_t *target=a->target;
        if(!wcschr(target,L'\\')&&SearchPathW(NULL,target,NULL,MAX_PATH,resolved,NULL))target=resolved;
        HICON owned=shell_icon_without_overlay(target);
        HICON icon=owned?owned:LoadIconW(NULL,IDI_APPLICATION);int image_index=ImageList_AddIcon(next,icon);if(owned)DestroyIcon(owned);
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;item.iItem=row++;item.pszText=a->name;item.iImage=image_index;item.lParam=i;
        SendMessageW(app_list,LVM_INSERTITEMW,0,(LPARAM)&item);
    }
    ListView_SetImageList(app_list,next,LVSIL_NORMAL);if(images)ImageList_Destroy(images);images=next;
    EnableWindow(add_button,ws->app_count<MAX_APPS);EnableWindow(folder_button,ws->app_count<MAX_APPS);
    EnableWindow(launch_button,ws->app_count>0&&!bulk_launch_running);
    SendMessageW(app_list,WM_SETREDRAW,TRUE,0);InvalidateRect(app_list,NULL,TRUE);InvalidateRect(main_window,NULL,FALSE);
}
/* Returns 1 on insertion, 0 for duplicate, -1 invalid, -2 full. Never launches dropped files. */
static int insert_path(const wchar_t *path) {
    if(active_space==0)return -3;
    if(!ensure_items(active_space))return -1;
    Workspace *ws=&spaces[active_space];if(!path||!*path||wcslen(path)>=MAX_PATH)return -1;
    for(int i=0;i<ws->app_count;i++)if(lstrcmpiW(ws->apps[i].target,path)==0)return 0;
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return -1;
    if(ws->app_count==MAX_APPS)return -2;
    AppItem *a=&ws->apps[ws->app_count++];ZeroMemory(a,sizeof(*a));lstrcpynW(a->target,path,MAX_PATH);
    const wchar_t *base=wcsrchr(path,L'\\');lstrcpynW(a->name,base&&base[1]?base+1:path,64);
    if(!(GetFileAttributesW(path)&FILE_ATTRIBUTE_DIRECTORY)) {wchar_t *dot=wcsrchr(a->name,L'.');if(dot&&dot!=a->name)*dot=0;}
    return 1;
}
static void finish_import(int added,int skipped) {
    wchar_t message[220];swprintf(message,220,nova_text(L"已添加 %d 项到「%ls」；跳过 %d 项（重复、无效路径或超过 20 项上限）。",L"Added %d items to \"%ls\"; skipped %d (duplicate, invalid, or beyond the 20-item limit)."),added,spaces[active_space].name,skipped);
    set_notice(message);if(save_config()){SetWindowTextW(search_edit,L"");refresh_apps();}
}
static void handle_drop(HDROP drop) {
    UINT n=DragQueryFileW(drop,0xffffffff,NULL,0);int added=0;
    for(UINT i=0;i<n;i++){
        wchar_t path[MAX_PATH];UINT len=DragQueryFileW(drop,i,NULL,0);
        if(len<MAX_PATH&&DragQueryFileW(drop,i,path,MAX_PATH)&&insert_path(path)==1)added++;
    }
    DragFinish(drop);finish_import(added,(int)n-added);
}
static void pick_file(void) {
    wchar_t path[MAX_PATH]=L"";OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=main_window;ofn.lpstrFile=path;ofn.nMaxFile=MAX_PATH;
    ofn.lpstrTitle=nova_text(L"添加应用、快捷方式或启动文件",L"Add an app, shortcut, or launch file");ofn.lpstrFilter=nova_text(L"所有文件\0*.*\0应用与快捷方式\0*.exe;*.lnk;*.bat;*.cmd\0",L"All files\0*.*\0Apps and shortcuts\0*.exe;*.lnk;*.bat;*.cmd\0");
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_NODEREFERENCELINKS;
    if(GetOpenFileNameW(&ofn)){int added=insert_path(path)==1;finish_import(added,!added);}
}
static void pick_folder(void) {
    BROWSEINFOW bi={0};bi.hwndOwner=main_window;bi.lpszTitle=nova_text(L"选择要添加到当前工作区的文件夹",L"Choose a folder to add to the current workspace");bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&bi);if(!id)return;
    wchar_t path[MAX_PATH];if(SHGetPathFromIDListW(id,path)){int added=insert_path(path)==1;finish_import(added,!added);}CoTaskMemFree(id);
}
static int selected_app(void) {
    int index=ListView_GetNextItem(app_list,-1,LVNI_SELECTED);if(index<0)return -1;
    LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=index;SendMessageW(app_list,LVM_GETITEMW,0,(LPARAM)&item);return (int)item.lParam;
}
static void moved_item(int source,int destination,int at,void *context){
    (void)context;if(destination<0)destination=active_space;
    if(destination==0){set_notice(nova_text(L"「目录」是固定的文件管理工作区，不接收启动项。",L"Files is a fixed file-management workspace and cannot receive launch items."));return;}
    if(!ensure_items(active_space)||!ensure_items(destination))return;
    int result=workspace_move(spaces,space_count,active_space,source,destination,at);
    if(result<0){set_notice(nova_text(L"无法移动：目标工作区已满或已有此项目。",L"Unable to move: the destination is full or already contains this item."));return;}
    if(result>0){if(save_config())set_notice(destination==active_space?nova_text(L"已保存新的图标顺序。",L"The new icon order has been saved."):nova_text(L"已移动到目标工作区。原文件位置不变。",L"Moved to the destination workspace. The original file remains in place."));refresh_apps();}
}
static int open_item(const AppItem *item){
    if(!item||!*item->target)return 0;
    if(test_mode){test_launch_count++;return 1;}
    if(!lstrcmpiW(item->target,L"explorer.exe")){open_file_manager();return files_page&&IsWindow(files_view);}
    return (INT_PTR)ShellExecuteW(main_window,L"open",item->target,NULL,NULL,SW_SHOWNORMAL)>32;
}
static void launch_workspace(void){
    if(!ensure_items(active_space))return;
    if(bulk_launch_running){set_notice(nova_text(L"当前工作区已在执行全部启动。",L"Launch all is already running for this workspace."));return;}
    Workspace snapshot=spaces[active_space];
    if(snapshot.app_count<1){set_notice(nova_text(L"当前工作区没有可启动的项目。",L"This workspace has no items to launch."));return;}
    bulk_launch_running=TRUE;
    EnableWindow(launch_button,FALSE);
    int failed=0;for(int i=0;i<snapshot.app_count;i++)if(!open_item(&snapshot.apps[i]))failed++;
    bulk_launch_running=FALSE;EnableWindow(launch_button,spaces[active_space].app_count>0);
    wchar_t message[180];swprintf(message,180,nova_text(L"全部启动完成：已对 %d 个项目各执行一次打开，失败 %d 个。",L"Launch all finished: opened %d items once each; %d failed."),snapshot.app_count,failed);set_notice(message);
}
static void open_app(void) {
    int i=selected_app();if(i<0)return;
    if(!open_item(&spaces[active_space].apps[i]))error_message(nova_text(L"启动失败。请检查文件是否被移动、删除，或是否存在对应的默认打开程序。",L"Launch failed. Check whether the file was moved or deleted and whether a default app is available."));
}
static void remove_app(void) {
    int i=selected_app();if(i<0)return;
    if(MessageBoxW(main_window,nova_text(L"从此工作区移除选中的启动项？原文件会保留。",L"Remove the selected launch item from this workspace? The original file will remain."),APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    Workspace *ws=&spaces[active_space];for(int j=i;j<ws->app_count-1;j++)ws->apps[j]=ws->apps[j+1];ws->app_count--;save_config();refresh_apps();set_notice(nova_text(L"已移除启动项，原文件保持不变。",L"Launch item removed. The original file was not changed."));
}
static void end_name_edit(BOOL commit) {
    if(!editing_name)return;
    editing_name=FALSE;
    if(commit){wchar_t value[40];GetWindowTextW(name_edit,value,40);size_t n=wcslen(value);while(n&&value[n-1]==L' ')value[--n]=0;
        if(n){lstrcpyW(spaces[active_space].name,value);save_config();refresh_spaces();}}
    editing_name=FALSE;ShowWindow(name_edit,SW_HIDE);SetFocus(space_list);InvalidateRect(main_window,NULL,FALSE);
}
static void edit_name(void){if(active_space==0)return;editing_name=TRUE;SetWindowTextW(name_edit,spaces[active_space].name);ShowWindow(name_edit,SW_SHOW);SetFocus(name_edit);SendMessageW(name_edit,EM_SETSEL,0,-1);}
static void new_space(void){if(space_count==MAX_WORKSPACES)return;end_name_edit(TRUE);active_space=space_count++;ZeroMemory(&spaces[active_space],sizeof(Workspace));spaces[active_space].items_loaded=1;swprintf(spaces[active_space].name,40,nova_text(L"工作区 %d",L"Workspace %d"),space_count);save_config();refresh_spaces();SetWindowTextW(search_edit,L"");if(files_page)show_workspace_page();refresh_apps();edit_name();}
static void delete_space(void){
    if(active_space==0||space_count<=1)return;
    if(MessageBoxW(main_window,nova_text(L"删除当前工作区及其中的启动项？原始文件和应用不会删除。",L"Delete the current workspace and its launch items? Original files and apps will not be deleted."),APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    for(int i=active_space;i<space_count-1;i++)spaces[i]=spaces[i+1];
    space_count--;if(active_space>=space_count)active_space=space_count-1;
    save_config();refresh_spaces();SetWindowTextW(search_edit,L"");refresh_apps();
}

static LRESULT CALLBACK child_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(hwnd==app_list&&hold_drag_message(&hold_drag,msg,wp,lp))return 0;
    if(hwnd==pin_button||hwnd==desktop_button||hwnd==settings_button||hwnd==new_button||hwnd==minimize_button||hwnd==maximize_button||hwnd==close_button){
        if(msg==WM_MOUSEMOVE&&hover_button!=hwnd){hover_button=hwnd;TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd,0};TrackMouseEvent(&t);InvalidateRect(hwnd,NULL,TRUE);}
        if(msg==WM_MOUSELEAVE){if(hover_button==hwnd)hover_button=NULL;InvalidateRect(hwnd,NULL,TRUE);}
    }
    if(msg==WM_DROPFILES){handle_drop((HDROP)wp);return 0;}
    if(msg==WM_KEYDOWN){
        if(wp==VK_F11){set_pinned(!pinned);return 0;}
        if(wp=='K'&&(GetKeyState(VK_CONTROL)&0x8000)){SetFocus(search_edit);return 0;}
        if(hwnd==name_edit&&wp==VK_RETURN){end_name_edit(TRUE);return 0;}
        if(hwnd==name_edit&&wp==VK_ESCAPE){end_name_edit(FALSE);return 0;}
        if(hwnd==app_list&&wp==VK_RETURN){open_app();return 0;}
        if(hwnd==app_list&&wp==VK_DELETE){remove_app();return 0;}
        if(wp==VK_ESCAPE&&pinned){set_pinned(FALSE);return 0;}
    }
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,child_proc,1);
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static void add_tray(void){
    ZeroMemory(&tray,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=main_window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;
    tray.hIcon=(HICON)LoadImageW(GetModuleHandleW(NULL),MAKEINTRESOURCEW(IDI_NOVA),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(!tray.hIcon)tray.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    lstrcpyW(tray.szTip,APP_NAME);Shell_NotifyIconW(NIM_ADD,&tray);
}
static void restore_window(void){ShowWindow(main_window,SW_RESTORE);SetForegroundWindow(main_window);if(files_page)file_manager_update_visibility();}
static void settings_menu(void){
    startup_enabled=read_startup();HMENU menu=CreatePopupMenu();
    HMENU language=CreatePopupMenu();
    AppendMenuW(language,MF_STRING|(!nova_english?MF_CHECKED:0),ID_LANGUAGE_ZH,L"简体中文");
    AppendMenuW(language,MF_STRING|(nova_english?MF_CHECKED:0),ID_LANGUAGE_EN,L"English");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)language,nova_text(L"语言",L"Language"));
    AppendMenuW(menu,MF_STRING|(startup_enabled?MF_CHECKED:0),ID_STARTUP,nova_text(L"开机启动",L"Start with Windows"));
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING|(active_space==0?MF_GRAYED:0),ID_RENAME,nova_text(L"重命名当前工作区",L"Rename current workspace"));
    AppendMenuW(menu,MF_STRING|(active_space==0||space_count<=1?MF_GRAYED:0),ID_DELETE,nova_text(L"删除当前工作区…",L"Delete current workspace…"));
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,ID_QUIT,nova_text(L"退出 NOVA",L"Exit NOVA"));
    RECT r;GetWindowRect(settings_button,&r);SetForegroundWindow(main_window);
    UINT id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_RIGHTBUTTON,r.right,r.bottom,0,main_window,NULL);
    DestroyMenu(menu);if(id)SendMessageW(main_window,WM_COMMAND,id,0);
}
static void tray_menu(void){
    HMENU m=CreatePopupMenu();POINT p;GetCursorPos(&p);
    AppendMenuW(m,MF_STRING,ID_OPEN,nova_text(L"打开 NOVA 窗口",L"Open NOVA window"));AppendMenuW(m,MF_STRING|(pinned?MF_CHECKED:0),ID_PIN,nova_text(L"窗口置顶",L"Always on top"));
    AppendMenuW(m,MF_STRING|(startup_enabled?MF_CHECKED:0),ID_STARTUP,nova_text(L"开机启动",L"Start with Windows"));AppendMenuW(m,MF_SEPARATOR,0,NULL);AppendMenuW(m,MF_STRING,ID_QUIT,nova_text(L"退出 NOVA",L"Exit NOVA"));
    SetForegroundWindow(main_window);UINT id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,main_window,NULL);DestroyMenu(m);if(id)SendMessageW(main_window,WM_COMMAND,id,0);PostMessageW(main_window,WM_NULL,0,0);
}
static ULONGLONG ft(FILETIME v){ULARGE_INTEGER n;n.LowPart=v.dwLowDateTime;n.HighPart=v.dwHighDateTime;return n.QuadPart;}
static void sample_stats(void){
    FILETIME a,b,c;if(GetSystemTimes(&a,&b,&c)){ULONGLONG idle=ft(a),kernel=ft(b),user=ft(c),total=kernel-prev_kernel+user-prev_user,rest=idle-prev_idle;
        if(prev_kernel&&total&&rest<=total)system_cpu=(int)(100*(total-rest)/total);
        prev_idle=idle;prev_kernel=kernel;prev_user=user;}
    MEMORYSTATUSEX ms={0};ms.dwLength=sizeof(ms);if(GlobalMemoryStatusEx(&ms))memory_load=(int)ms.dwMemoryLoad;
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(taskbar_message&&msg==taskbar_message){add_tray();return 0;}
    switch(msg){
    case WM_CREATE:{
        main_window=hwnd;HDC dc=GetDC(hwnd);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(hwnd,dc);create_fonts();
        space_list=control(L"LISTBOX",nova_text(L"工作区",L"Workspaces"),LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL,ID_SPACES);SendMessageW(space_list,LB_SETITEMHEIGHT,0,px(44));
        new_button=button(nova_text(L"新建工作区",L"New workspace"),ID_NEW);
        startup_enabled=read_startup();settings_button=button(nova_text(L"设置",L"Settings"),ID_SETTINGS);pin_button=button(nova_text(L"窗口置顶 (F11)",L"Always on top (F11)"),ID_PIN);desktop_button=button(nova_text(L"放到桌面",L"Place on desktop"),ID_DESKTOP);
        minimize_button=button(nova_text(L"最小化",L"Minimize"),ID_MINIMIZE);maximize_button=button(nova_text(L"最大化",L"Maximize"),ID_MAXIMIZE);close_button=button(nova_text(L"关闭",L"Close"),ID_CLOSE);
        search_edit=control(L"EDIT",L"",ES_AUTOHSCROLL,ID_SEARCH);SendMessageW(search_edit,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"搜索当前工作区  Ctrl+K",L"Search this workspace  Ctrl+K"));SendMessageW(search_edit,EM_SETLIMITTEXT,127,0);
        add_button=button(nova_text(L"添加文件",L"Add file"),ID_ADD);folder_button=button(nova_text(L"添加文件夹",L"Add folder"),ID_FOLDER);launch_button=button(nova_text(L"全部启动",L"Launch all"),ID_LAUNCH_ALL);
        app_list=control(WC_LISTVIEWW,nova_text(L"工作区启动项",L"Workspace launch items"),LVS_ICON|LVS_AUTOARRANGE|LVS_SINGLESEL|LVS_SHOWSELALWAYS,ID_APPS);
        hold_drag_init(&hold_drag,app_list,space_list,moved_item,NULL);
        ListView_SetBkColor(app_list,BG);ListView_SetTextBkColor(app_list,BG);ListView_SetTextColor(app_list,TEXT);ListView_SetExtendedListViewStyle(app_list,LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);ListView_SetIconSpacing(app_list,px(136),px(112));SetWindowTheme(app_list,L"DarkMode_Explorer",NULL);
        name_edit=control(L"EDIT",L"",ES_AUTOHSCROLL,ID_NAME);SendMessageW(name_edit,EM_SETLIMITTEXT,39,0);ShowWindow(name_edit,SW_HIDE);
        HWND children[]={space_list,new_button,settings_button,pin_button,desktop_button,minimize_button,maximize_button,close_button,search_edit,add_button,folder_button,launch_button,app_list,name_edit};
        for(unsigned i=0;i<sizeof(children)/sizeof(children[0]);i++){SetWindowSubclass(children[i],child_proc,1,0);DragAcceptFiles(children[i],TRUE);}DragAcceptFiles(hwnd,TRUE);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        add_tooltip(desktop_button,nova_text(L"桌面围栏 / 恢复普通窗口",L"Desktop fence / restore normal window"));add_tooltip(pin_button,nova_text(L"置顶 / 取消置顶 (F11)",L"Always on top / unpin (F11)"));add_tooltip(settings_button,nova_text(L"设置：语言、开机启动、工作区管理",L"Settings: language, startup, and workspace management"));add_tooltip(new_button,nova_text(L"新建工作区（最多 8 个）",L"New workspace (up to 8)"));
        add_tooltip(launch_button,nova_text(L"将当前工作区的每个项目各打开一次",L"Open every item in this workspace once"));
        add_tooltip(minimize_button,nova_text(L"最小化",L"Minimize"));add_tooltip(maximize_button,nova_text(L"最大化 / 还原",L"Maximize / restore"));add_tooltip(close_button,nova_text(L"关闭",L"Close"));
        refresh_spaces();refresh_apps();layout_controls();sample_stats();SetTimer(hwnd,STATS_TIMER,3000,NULL);if(prefer_desktop)SetTimer(hwnd,START_DESKTOP_TIMER,800,NULL);else if(prefer_pin)SetTimer(hwnd,START_PIN_TIMER,800,NULL);
        if(!test_mode){add_tray();RegisterHotKey(hwnd,ID_HOTKEY,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'N');}
        BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));return 0;
    }
    case WM_WINDOWPOSCHANGING:if(desktop_mode){WINDOWPOS *position=(WINDOWPOS*)lp;if(!(position->flags&SWP_NOZORDER))position->hwndInsertAfter=HWND_BOTTOM;}break;
    case WM_NCCALCSIZE:if(wp){RECT original=((NCCALCSIZE_PARAMS*)lp)->rgrc[0];DefWindowProcW(hwnd,msg,wp,lp);((NCCALCSIZE_PARAMS*)lp)->rgrc[0].top=original.top+(IsZoomed(hwnd)?GetSystemMetrics(SM_CYSIZEFRAME)+GetSystemMetrics(SM_CXPADDEDBORDER):1);return 0;}break;
    case WM_NCHITTEST:{LRESULT result=DefWindowProcW(hwnd,msg,wp,lp);if(result!=HTCLIENT)return result;POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);RECT r;GetClientRect(hwnd,&r);if(!IsZoomed(hwnd)&&p.y<px(4))return HTTOP;if(p.y<caption_height()&&p.x<r.right-px(276))return HTCAPTION;return HTCLIENT;}
    case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;m->ptMinTrackSize.x=px(780);m->ptMinTrackSize.y=px(600);return 0;}
    case WM_SIZE:layout_controls();if(wp==SIZE_MINIMIZED&&!pinned)ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);paint(dc,r);paint_generation++;EndPaint(hwnd,&ps);return 0;}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,PANEL);return (LRESULT)panel_brush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;wchar_t caption[80];BOOL selected=(d->itemState&ODS_SELECTED)!=0;
        if(d->CtlID==ID_PIN||d->CtlID==ID_DESKTOP||d->CtlID==ID_SETTINGS||d->CtlID==ID_NEW||d->CtlID==ID_MINIMIZE||d->CtlID==ID_MAXIMIZE||d->CtlID==ID_CLOSE){draw_icon(d);return TRUE;}
        COLORREF fill=selected?RGB(48,65,95):PANEL;
        if(d->CtlID==ID_ADD||d->CtlID==ID_LAUNCH_ALL)fill=selected?RGB(76,103,159):RGB(53,77,122);
        if(d->CtlID==ID_SPACES){if(d->itemID==(UINT)-1)return TRUE;SendMessageW(space_list,LB_GETTEXT,d->itemID,(LPARAM)caption);if((int)d->itemID==active_space)fill=RGB(42,56,79);}
        else GetWindowTextW(d->hwndItem,caption,80);
        HBRUSH b=CreateSolidBrush(fill);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);
        RECT t=d->rcItem;t.left+=px(12);t.right-=px(8);
        text(d->hDC,caption,t,body_font,(d->itemState&ODS_DISABLED)?RGB(112,128,150):TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(d->CtlID==ID_SPACES?DT_LEFT:DT_CENTER));
        if(d->itemState&ODS_FOCUS){RECT f=d->rcItem;InflateRect(&f,-3,-3);DrawFocusRect(d->hDC,&f);}return TRUE;
    }
    case WM_COMMAND:{int id=LOWORD(wp);
        if(id==ID_SPACES&&HIWORD(wp)==LBN_DBLCLK){if(active_space==0)open_file_manager();else launch_workspace();return 0;}
        if(id==ID_MINIMIZE){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        if(id==ID_MAXIMIZE){ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);return 0;}
        if(id==ID_CLOSE){PostMessageW(hwnd,WM_CLOSE,0,0);return 0;}
        if(id==ID_SETTINGS){settings_menu();return 0;}
        if(id==ID_DESKTOP){set_desktop_mode(!desktop_mode);return 0;}
        if(id==ID_LAUNCH_ALL){launch_workspace();return 0;}
        if(id==ID_FILES){if(active_space!=0){active_space=0;save_config();refresh_spaces();}open_file_manager();return 0;}
        if(id==ID_SPACES&&HIWORD(wp)==LBN_SELCHANGE){end_name_edit(TRUE);int i=(int)SendMessageW(space_list,LB_GETCURSEL,0,0);if(i>=0){active_space=i;save_config();SetWindowTextW(search_edit,L"");if(active_space==0)open_file_manager();else{if(files_page)show_workspace_page();refresh_apps();}}return 0;}
        if(id==ID_SEARCH&&HIWORD(wp)==EN_CHANGE){refresh_apps();return 0;}
        if(id==ID_NAME&&HIWORD(wp)==EN_KILLFOCUS){end_name_edit(TRUE);return 0;}
        switch(id){case ID_ADD:pick_file();break;case ID_FOLDER:pick_folder();break;case ID_PIN:set_pinned(!pinned);break;case ID_STARTUP:toggle_startup();break;case ID_LANGUAGE_ZH:apply_language(FALSE);break;case ID_LANGUAGE_EN:apply_language(TRUE);break;case ID_NEW:new_space();break;case ID_RENAME:edit_name();break;case ID_DELETE:delete_space();break;case ID_REMOVE:remove_app();break;case ID_OPEN:restore_window();break;case ID_QUIT:DestroyWindow(hwnd);break;}return 0;
    }
    case WM_NOTIFY:{NMHDR *n=(NMHDR*)lp;
        if(n->idFrom==ID_APPS&&n->code==NM_DBLCLK&&!hold_drag.dragging)open_app();
        if(n->idFrom==ID_APPS&&n->code==LVN_GETINFOTIPW){NMLVGETINFOTIPW *tip=(NMLVGETINFOTIPW*)lp;LVITEMW i={0};i.mask=LVIF_PARAM;i.iItem=tip->iItem;SendMessageW(app_list,LVM_GETITEMW,0,(LPARAM)&i);lstrcpynW(tip->pszText,spaces[active_space].apps[i.lParam].target,tip->cchTextMax);}
        if(n->idFrom==ID_APPS&&n->code==LVN_GETEMPTYMARKUP){NMLVEMPTYMARKUP *m=(NMLVEMPTYMARKUP*)lp;m->dwFlags=EMF_CENTERED;lstrcpyW(m->szMarkup,spaces[active_space].app_count?nova_text(L"没有匹配的启动项。清空搜索以查看全部。",L"No matching launch items. Clear the search to show all items."):nova_text(L"把应用或文件拖到这里\n也可以点击上方「添加文件」。",L"Drop an app or file here\nor click Add file above."));return TRUE;}
        return 0;
    }
    case WM_CONTEXTMENU:if((HWND)wp==app_list&&selected_app()>=0){HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,ID_REMOVE,nova_text(L"从工作区移除",L"Remove from workspace"));POINT p;GetCursorPos(&p);UINT id=TrackPopupMenu(m,TPM_RETURNCMD,p.x,p.y,0,hwnd,NULL);DestroyMenu(m);if(id==ID_REMOVE)remove_app();}return 0;
    case WM_DROPFILES:handle_drop((HDROP)wp);return 0;
    case WM_KEYDOWN:if(wp==VK_F11)set_pinned(!pinned);if(wp==VK_ESCAPE&&pinned)set_pinned(FALSE);return 0;
    case WM_TIMER:
        if(wp==START_DESKTOP_TIMER){KillTimer(hwnd,START_DESKTOP_TIMER);set_desktop_mode(TRUE);return 0;}
        if(wp==START_PIN_TIMER){KillTimer(hwnd,START_PIN_TIMER);set_pinned(TRUE);return 0;}if(IsWindowVisible(hwnd)&&!IsIconic(hwnd)){sample_stats();RECT r;GetClientRect(hwnd,&r);r.top=r.bottom-px(60);InvalidateRect(hwnd,&r,FALSE);}return 0;
    case WM_RESTORE_NOVA:case WM_HOTKEY:restore_window();return 0;
    case WM_TRAY:if(lp==WM_LBUTTONUP)restore_window();if(lp==WM_RBUTTONUP)tray_menu();return 0;
    case WM_CLOSE:file_manager_close();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:file_manager_close();save_config();KillTimer(hwnd,STATS_TIMER);KillTimer(hwnd,START_PIN_TIMER);KillTimer(hwnd,START_DESKTOP_TIMER);UnregisterHotKey(hwnd,ID_HOTKEY);if(!test_mode)Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show){
    (void)previous;SetProcessDPIAware();OleInitialize(NULL);
    HDC screen=GetDC(NULL);dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"NOVA_DESKTOP_SINGLE_INSTANCE");
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(APP_CLASS,NULL);if(existing){PostMessageW(existing,WM_RESTORE_NOVA,0,0);if(command&&wcsstr(command,L"--files"))PostMessageW(existing,WM_COMMAND,ID_FILES,0);}CloseHandle(mutex);OleUninitialize();return 0;}
    wchar_t dir[MAX_PATH];if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA,NULL,SHGFP_TYPE_CURRENT,dir))||wcslen(dir)>MAX_PATH-40)return 1;
    wcscat(dir,L"\\NOVA Desktop");CreateDirectoryW(dir,NULL);swprintf(config_path,MAX_PATH,L"%ls\\config.ini",dir);load_config();
    if(storage_failed){MessageBoxW(NULL,notice,APP_NAME,MB_OK|MB_ICONERROR);store_close();CloseHandle(mutex);OleUninitialize();return 4;}
    if(!store_backup())set_notice(nova_text(L"无法更新数据库备份，请检查数据目录权限。",L"Unable to update the database backup. Check permissions for the data folder."));
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    background=CreateSolidBrush(BG);panel_brush=CreateSolidBrush(PANEL);taskbar_message=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=window_proc;wc.lpszClassName=APP_CLASS;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
    wc.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_NOVA),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED);
    wc.hIconSm=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_NOVA),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(!wc.hIcon)wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    if(!wc.hIconSm)wc.hIconSm=wc.hIcon;
    wc.hbrBackground=background;
    if(!RegisterClassExW(&wc))return 2;
    HWND hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,APP_CLASS,APP_NAME,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1100),px(760),NULL,NULL,instance,NULL);
    if(!hwnd)return 3;
    if(command&&wcsstr(command,L"--files")&&active_space!=0){active_space=0;save_config();refresh_spaces();}
    ShowWindow(hwnd,show==SW_HIDE?SW_HIDE:SW_MAXIMIZE);UpdateWindow(hwnd);
    if(active_space==0)open_file_manager();
    MSG msg={0};while(GetMessageW(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_F11){set_pinned(!pinned);continue;}
        if(msg.message==WM_KEYDOWN&&msg.wParam=='K'&&(GetKeyState(VK_CONTROL)&0x8000)&&files_page)show_workspace_page();
        if(file_manager_message(&msg))continue;
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE&&(hold_drag.dragging||hold_drag.armed)){hold_drag_cancel(&hold_drag);continue;}
        if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_F11||(msg.wParam=='K'&&(GetKeyState(VK_CONTROL)&0x8000)))){if(msg.wParam==VK_F11)set_pinned(!pinned);else SetFocus(search_edit);continue;}
        if(msg.message==WM_KEYDOWN&&((msg.hwnd==name_edit&&(msg.wParam==VK_RETURN||msg.wParam==VK_ESCAPE))||(msg.hwnd==app_list&&(msg.wParam==VK_RETURN||msg.wParam==VK_DELETE))||(pinned&&msg.wParam==VK_ESCAPE))){DispatchMessageW(&msg);continue;}
        if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(images)ImageList_Destroy(images);
    DeleteObject(body_font);DeleteObject(small_font);DeleteObject(title_font);DeleteObject(brand_font);DeleteObject(background);DeleteObject(panel_brush);store_close();CloseHandle(mutex);OleUninitialize();return (int)msg.wParam;
}
