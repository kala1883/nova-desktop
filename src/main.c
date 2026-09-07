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

#define APP_NAME L"NOVA Desktop"
#define APP_CLASS L"NovaDesktopWindowV3"
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
#define STATS_TIMER 1
#define START_PIN_TIMER 2
#define LAUNCH_TIMER 3

static LaunchQueue launch_queue;
static HoldDrag hold_drag;
static Workspace spaces[MAX_WORKSPACES];
static int space_count=4, active_space, dpi=96;
static HWND main_window, space_list, app_list, search_edit, name_edit;
static HWND add_button, folder_button, pin_button, settings_button, new_button, tooltip, hover_button;
static HWND minimize_button, maximize_button, close_button;
static HFONT body_font, small_font, title_font, brand_font;
static HBRUSH background, panel_brush;
static HIMAGELIST images;
static NOTIFYICONDATAW tray;
static UINT taskbar_message;
static BOOL pinned, prefer_pin, editing_name, startup_enabled, test_mode;
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
static void refresh_spaces(void);
static void launch_workspace(void);

static void set_notice(const wchar_t *text) {
    lstrcpynW(notice,text,256);
    if(main_window) { RECT r; GetClientRect(main_window,&r); r.top=r.bottom-px(62); InvalidateRect(main_window,&r,FALSE); }
}
static void error_message(const wchar_t *message) { if(test_mode){fwprintf(stderr,L"NOVA error: %ls\n",message);return;} MessageBoxW(main_window,message,APP_NAME,MB_OK|MB_ICONWARNING); }

static void defaults(void) {
    ZeroMemory(spaces,sizeof(spaces)); space_count=4; active_space=0;
    const wchar_t *names[]={L"日常",L"开发",L"创作",L"专注"};
    for(int i=0;i<4;i++) lstrcpyW(spaces[i].name,names[i]);
    const wchar_t *app_names[]={L"文件管理",L"浏览器",L"系统设置",L"记事本"};
    const wchar_t *targets[]={L"explorer.exe",L"https://www.bing.com",L"ms-settings:",L"notepad.exe"};
    spaces[0].app_count=4;
    for(int i=0;i<4;i++){lstrcpyW(spaces[0].apps[i].name,app_names[i]);lstrcpyW(spaces[0].apps[i].target,targets[i]);}
    spaces[1].app_count=1; lstrcpyW(spaces[1].apps[0].name,L"PowerShell"); lstrcpyW(spaces[1].apps[0].target,L"powershell.exe");
}

/* UTF-16 INI plus atomic replacement: keep the old file if a write fails. */
static BOOL save_config(void) {
    wchar_t temp[MAX_PATH+8],value[32],section[32],key[32];
    swprintf(temp,MAX_PATH+8,L"%ls.tmp",config_path);
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE){set_notice(L"配置保存失败：请检查配置目录权限。当前改动仍在内存中。");return FALSE;}
    WORD bom=0xfeff; DWORD bytes=0; BOOL ok=WriteFile(file,&bom,2,&bytes,NULL)&&bytes==2; CloseHandle(file);
#define SAVE(section_,key_,value_) do { if(!WritePrivateProfileStringW(section_,key_,value_,temp)) ok=FALSE; } while(0)
    swprintf(value,32,L"%d",space_count); SAVE(L"Nova",L"WorkspaceCount",value);
    swprintf(value,32,L"%d",active_space); SAVE(L"Nova",L"Active",value);
    SAVE(L"Nova",L"AlwaysOnTop",prefer_pin?L"1":L"0");
    for(int s=0;s<space_count;s++){
        swprintf(section,32,L"Workspace%d",s); SAVE(section,L"Name",spaces[s].name);
        swprintf(value,32,L"%d",spaces[s].app_count); SAVE(section,L"AppCount",value);
        for(int a=0;a<spaces[s].app_count;a++){
            swprintf(key,32,L"App%dName",a); SAVE(section,key,spaces[s].apps[a].name);
            swprintf(key,32,L"App%dTarget",a); SAVE(section,key,spaces[s].apps[a].target);
        }
    }
    WritePrivateProfileStringW(NULL,NULL,NULL,temp);
    if(ok)ok=MoveFileExW(temp,config_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok)set_notice(L"配置未保存成功：原配置已保留，请检查磁盘空间与权限。");
    return ok;
#undef SAVE
}

static void load_config(void) {
    defaults();
    if(GetFileAttributesW(config_path)==INVALID_FILE_ATTRIBUTES)return;
    int n=(int)GetPrivateProfileIntW(L"Nova",L"WorkspaceCount",4,config_path);
    if(n<1||n>MAX_WORKSPACES)return;
    space_count=n;
    active_space=(int)GetPrivateProfileIntW(L"Nova",L"Active",0,config_path);
    if(active_space<0||active_space>=space_count)active_space=0;
    prefer_pin=GetPrivateProfileIntW(L"Nova",L"AlwaysOnTop",0,config_path)!=0;
    for(int s=0;s<space_count;s++){
        wchar_t section[32],key[32],fallback[40]; swprintf(section,32,L"Workspace%d",s);swprintf(fallback,40,L"工作区 %d",s+1);
        GetPrivateProfileStringW(section,L"Name",fallback,spaces[s].name,40,config_path);
        n=(int)GetPrivateProfileIntW(section,L"AppCount",0,config_path);spaces[s].app_count=n<0?0:n>MAX_APPS?MAX_APPS:n;
        for(int a=0;a<spaces[s].app_count;a++){
            swprintf(key,32,L"App%dName",a);GetPrivateProfileStringW(section,key,L"启动项",spaces[s].apps[a].name,64,config_path);
            swprintf(key,32,L"App%dTarget",a);GetPrivateProfileStringW(section,key,L"",spaces[s].apps[a].target,MAX_PATH,config_path);
        }
    }
    /* Older releases wrote ANSI INIs. Repair only strings consisting of '?'. */
    BOOL repaired=FALSE;
    for(int s=0;s<space_count;s++){
        if(*spaces[s].name&&wcsspn(spaces[s].name,L"?")==wcslen(spaces[s].name)){
            const wchar_t *names[]={L"日常",L"开发",L"创作",L"专注"};
            if(s<4)lstrcpyW(spaces[s].name,names[s]);else swprintf(spaces[s].name,40,L"工作区 %d",s+1);
            repaired=TRUE;
        }
        for(int a=0;a<spaces[s].app_count;a++){
            AppItem *item=&spaces[s].apps[a];
            if(*item->name&&wcsspn(item->name,L"?")==wcslen(item->name)){
                const wchar_t *targets[]={L"explorer.exe",L"https://www.bing.com",L"ms-settings:",L"notepad.exe",L"wt.exe",L"powershell.exe",L".",L"mspaint.exe",L"ms-photos:",L"ms-clock:"};
                const wchar_t *names[]={L"文件管理",L"浏览器",L"系统设置",L"记事本",L"终端",L"PowerShell",L"项目目录",L"画图",L"照片",L"时钟"};
                BOOL known=FALSE;for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++)if(lstrcmpiW(item->target,targets[i])==0){lstrcpyW(item->name,names[i]);known=TRUE;break;}
                if(!known){const wchar_t *base=wcsrchr(item->target,L'\\');lstrcpynW(item->name,base?base+1:item->target,64);}
                repaired=TRUE;
            }
        }
    }
    if(repaired){wchar_t backup[MAX_PATH+32];swprintf(backup,MAX_PATH+32,L"%ls.pre-unicode.bak",config_path);
        if(CopyFileW(config_path,backup,TRUE)||GetLastError()==ERROR_FILE_EXISTS){save_config();set_notice(L"已修复旧版默认名称，原配置保存在 config.ini.pre-unicode.bak。");}}
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
    if(result!=ERROR_SUCCESS&&result!=ERROR_FILE_NOT_FOUND){error_message(L"无法更新开机启动设置。请检查当前用户的注册表访问权限。");return;}
    startup_enabled=read_startup();
    set_notice(startup_enabled?L"已开启：下次登录 Windows 时启动 NOVA，并恢复固定状态。":L"已关闭开机启动。你仍可手动打开 NOVA。");
}

/* A WeChat-style pin changes only the topmost band; geometry and ownership stay unchanged. */
static BOOL set_pinned(BOOL value) {
    if(value==pinned)return TRUE;
    LONG_PTR exstyle=GetWindowLongPtrW(main_window,GWL_EXSTYLE);
    SetWindowLongPtrW(main_window,GWL_EXSTYLE,value?(exstyle|WS_EX_TOPMOST):(exstyle&~WS_EX_TOPMOST));
    if(!SetWindowPos(main_window,value?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED)){
        SetWindowLongPtrW(main_window,GWL_EXSTYLE,exstyle);
        error_message(L"无法改变窗口置顶状态，请重试。");return FALSE;
    }
    pinned=value;prefer_pin=value;save_config();SetWindowTextW(pin_button,pinned?L"取消置顶 (F11)":L"窗口置顶 (F11)");
    InvalidateRect(pin_button,NULL,TRUE);
    set_notice(pinned?L"已置顶。窗口仍可拖动和缩放；再次点击图钉取消。":L"已取消置顶。");
    return TRUE;
}

static void text(HDC dc,const wchar_t *str,RECT rect,HFONT font,COLORREF color,UINT flags) {
    HFONT old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,str,-1,&rect,flags|DT_NOPREFIX);SelectObject(dc,old);
}
static RECT box(int x,int y,int w,int h){RECT r={x,y,x+w,y+h};return r;}
static void paint(HDC dc,RECT client) {
    RECT title=box(0,0,client.right,caption_height());HBRUSH titlebrush=CreateSolidBrush(RGB(32,33,33));FillRect(dc,&title,titlebrush);DeleteObject(titlebrush);
    text(dc,APP_NAME,box(px(12),0,client.right-px(250),caption_height()),small_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    int saved=SaveDC(dc);IntersectClipRect(dc,0,caption_height(),client.right,client.bottom);
    SetViewportOrgEx(dc,0,caption_height(),NULL);client.bottom-=caption_height();
    FillRect(dc,&client,background);RECT side=box(0,0,px(220),client.bottom);FillRect(dc,&side,panel_brush);
    text(dc,L"NOVA",box(px(28),px(27),px(164),px(38)),brand_font,TEXT,DT_LEFT);
    text(dc,L"桌面工作区",box(px(29),px(71),px(165),px(26)),body_font,MUTED,DT_LEFT);
    text(dc,L"工作区",box(px(28),px(125),px(160),px(24)),small_font,MUTED,DT_LEFT);
    int left=px(254),width=client.right-left-px(32);
    text(dc,spaces[active_space].name,box(left,px(27),width-px(104),px(47)),title_font,TEXT,DT_LEFT|DT_END_ELLIPSIS);
    wchar_t subtitle[100];swprintf(subtitle,100,L"%d 个启动项  /  拖入文件、文件夹或快捷方式",spaces[active_space].app_count);
    text(dc,subtitle,box(left,px(84),width,px(28)),body_font,MUTED,DT_LEFT|DT_END_ELLIPSIS);
    text(dc,L"启动项",box(left,px(190),width,px(25)),body_font,TEXT,DT_LEFT);
    RECT status=box(left,client.bottom-px(56),width,px(50));FillRect(dc,&status,background);
    text(dc,notice,box(left,client.bottom-px(49),width,px(23)),small_font,MUTED,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
    wchar_t metrics[80];swprintf(metrics,80,L"系统 CPU %d%%    内存 %d%%",system_cpu,memory_load);
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
static void draw_icon(DRAWITEMSTRUCT *d){
    BOOL active=d->CtlID==ID_PIN&&pinned;
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
    move(space_list,px(16),px(160),px(188),r.bottom-px(218));
    move(new_button,px(162),px(115),px(40),px(36));
    HWND caption_buttons[]={pin_button,settings_button,minimize_button,maximize_button,close_button};
    for(int i=0;i<5;i++)MoveWindow(caption_buttons[i],r.right-px(46)*(5-i),0,px(46),caption_height(),TRUE);
    SetWindowTextW(maximize_button,IsZoomed(main_window)?L"还原":L"最大化");
    move(search_edit,left,px(133),width-px(220),px(32));
    move(add_button,r.right-px(236),px(130),px(96),px(38));move(folder_button,r.right-px(132),px(130),px(100),px(38));
    move(app_list,left,px(225),width,r.bottom-px(290));
    move(name_edit,left,px(32),width-px(104),px(39));
    InvalidateRect(main_window,NULL,FALSE);
}
static void refresh_spaces(void) {
    SendMessageW(space_list,LB_RESETCONTENT,0,0);
    for(int i=0;i<space_count;i++)SendMessageW(space_list,LB_ADDSTRING,0,(LPARAM)spaces[i].name);
    SendMessageW(space_list,LB_SETCURSEL,active_space,0);
    EnableWindow(new_button,space_count<MAX_WORKSPACES);
}
static BOOL contains(const wchar_t *value,const wchar_t *query) {
    if(!*query)return TRUE;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,FIND_FROMSTART|NORM_IGNORECASE,value,-1,query,-1,NULL,NULL,NULL,0)>=0;
}
static void refresh_apps(void) {
    if(!app_list)return;
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
    SendMessageW(app_list,WM_SETREDRAW,TRUE,0);InvalidateRect(app_list,NULL,TRUE);InvalidateRect(main_window,NULL,FALSE);
}
/* Returns 1 on insertion, 0 for duplicate, -1 invalid, -2 full. Never launches dropped files. */
static int insert_path(const wchar_t *path) {
    Workspace *ws=&spaces[active_space];if(!path||!*path||wcslen(path)>=MAX_PATH)return -1;
    for(int i=0;i<ws->app_count;i++)if(lstrcmpiW(ws->apps[i].target,path)==0)return 0;
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return -1;
    if(ws->app_count==MAX_APPS)return -2;
    AppItem *a=&ws->apps[ws->app_count++];lstrcpynW(a->target,path,MAX_PATH);
    const wchar_t *base=wcsrchr(path,L'\\');lstrcpynW(a->name,base&&base[1]?base+1:path,64);
    if(!(GetFileAttributesW(path)&FILE_ATTRIBUTE_DIRECTORY)) {wchar_t *dot=wcsrchr(a->name,L'.');if(dot&&dot!=a->name)*dot=0;}
    return 1;
}
static void finish_import(int added,int skipped) {
    wchar_t message[180];swprintf(message,180,L"已添加 %d 项到「%ls」；跳过 %d 项（重复、无效路径或超过 20 项上限）。",added,spaces[active_space].name,skipped);
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
    ofn.lpstrTitle=L"添加应用、快捷方式或启动文件";ofn.lpstrFilter=L"所有文件\0*.*\0应用与快捷方式\0*.exe;*.lnk;*.bat;*.cmd\0";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_NODEREFERENCELINKS;
    if(GetOpenFileNameW(&ofn)){int added=insert_path(path)==1;finish_import(added,!added);}
}
static void pick_folder(void) {
    BROWSEINFOW bi={0};bi.hwndOwner=main_window;bi.lpszTitle=L"选择要添加到当前工作区的文件夹";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&bi);if(!id)return;
    wchar_t path[MAX_PATH];if(SHGetPathFromIDListW(id,path)){int added=insert_path(path)==1;finish_import(added,!added);}CoTaskMemFree(id);
}
static int selected_app(void) {
    int index=ListView_GetNextItem(app_list,-1,LVNI_SELECTED);if(index<0)return -1;
    LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=index;SendMessageW(app_list,LVM_GETITEMW,0,(LPARAM)&item);return (int)item.lParam;
}
static void moved_item(int source,int destination,int at,void *context){
    (void)context;if(destination<0)destination=active_space;
    int result=workspace_move(spaces,space_count,active_space,source,destination,at);
    if(result<0){set_notice(L"无法移动：目标工作区已满或已有此项目。");return;}
    if(result>0){if(save_config())set_notice(destination==active_space?L"已保存新的图标顺序。":L"已移动到目标工作区。原文件位置不变。");refresh_apps();}
}
static int dispatch_launch(const AppItem *item,void *context){
    (void)context;
    return (INT_PTR)ShellExecuteW(main_window,L"open",item->target,NULL,NULL,SW_SHOWNORMAL)>32;
}
static void launch_workspace(void){
    if(launch_queue.running){set_notice(L"正在启动工作区，请等待完成；Esc 可取消剩余启动。");return;}
    if(!launch_queue_begin(&launch_queue,&spaces[active_space])){set_notice(L"当前工作区没有可启动的项目。");return;}
    if(!SetTimer(main_window,LAUNCH_TIMER,300,NULL)){launch_queue_cancel(&launch_queue);set_notice(L"无法启动任务，请重试。");return;}
    set_notice(L"正在依次启动整个工作区；Esc 可取消尚未发出的启动。");
}
static void open_app(void) {
    int i=selected_app();if(i<0)return;
    const wchar_t *target=spaces[active_space].apps[i].target;
    HINSTANCE result=ShellExecuteW(main_window,L"open",target,NULL,NULL,SW_SHOWNORMAL);
    if((INT_PTR)result<=32)error_message(L"启动失败。请检查文件是否被移动、删除，或是否存在对应的默认打开程序。");
}
static void remove_app(void) {
    int i=selected_app();if(i<0)return;
    if(MessageBoxW(main_window,L"从此工作区移除选中的启动项？原文件会保留。",APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    Workspace *ws=&spaces[active_space];for(int j=i;j<ws->app_count-1;j++)ws->apps[j]=ws->apps[j+1];ws->app_count--;save_config();refresh_apps();set_notice(L"已移除启动项，原文件保持不变。");
}
static void end_name_edit(BOOL commit) {
    if(!editing_name)return;
    editing_name=FALSE;
    if(commit){wchar_t value[40];GetWindowTextW(name_edit,value,40);size_t n=wcslen(value);while(n&&value[n-1]==L' ')value[--n]=0;
        if(n){lstrcpyW(spaces[active_space].name,value);save_config();refresh_spaces();}}
    editing_name=FALSE;ShowWindow(name_edit,SW_HIDE);SetFocus(space_list);InvalidateRect(main_window,NULL,FALSE);
}
static void edit_name(void){editing_name=TRUE;SetWindowTextW(name_edit,spaces[active_space].name);ShowWindow(name_edit,SW_SHOW);SetFocus(name_edit);SendMessageW(name_edit,EM_SETSEL,0,-1);}
static void new_space(void){if(space_count==MAX_WORKSPACES)return;end_name_edit(TRUE);active_space=space_count++;ZeroMemory(&spaces[active_space],sizeof(Workspace));swprintf(spaces[active_space].name,40,L"工作区 %d",space_count);save_config();refresh_spaces();SetWindowTextW(search_edit,L"");refresh_apps();edit_name();}
static void delete_space(void){
    if(space_count<=1)return;
    if(MessageBoxW(main_window,L"删除当前工作区及其中的启动项？原始文件和应用不会删除。",APP_NAME,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    for(int i=active_space;i<space_count-1;i++)spaces[i]=spaces[i+1];
    space_count--;if(active_space>=space_count)active_space=space_count-1;
    save_config();refresh_spaces();SetWindowTextW(search_edit,L"");refresh_apps();
}

static LRESULT CALLBACK child_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(hwnd==app_list&&hold_drag_message(&hold_drag,msg,wp,lp))return 0;
    if(hwnd==pin_button||hwnd==settings_button||hwnd==new_button||hwnd==minimize_button||hwnd==maximize_button||hwnd==close_button){
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
    ZeroMemory(&tray,sizeof(tray));tray.cbSize=sizeof(tray);tray.hWnd=main_window;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=LoadIconW(NULL,IDI_APPLICATION);lstrcpyW(tray.szTip,APP_NAME);Shell_NotifyIconW(NIM_ADD,&tray);
}
static void restore_window(void){ShowWindow(main_window,SW_RESTORE);SetForegroundWindow(main_window);}
static void settings_menu(void){
    startup_enabled=read_startup();HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(startup_enabled?MF_CHECKED:0),ID_STARTUP,L"开机启动");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,ID_RENAME,L"重命名当前工作区");
    AppendMenuW(menu,MF_STRING|(space_count<=1?MF_GRAYED:0),ID_DELETE,L"删除当前工作区…");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,ID_QUIT,L"退出 NOVA");
    RECT r;GetWindowRect(settings_button,&r);SetForegroundWindow(main_window);
    UINT id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_RIGHTBUTTON,r.right,r.bottom,0,main_window,NULL);
    DestroyMenu(menu);if(id)SendMessageW(main_window,WM_COMMAND,id,0);
}
static void tray_menu(void){
    HMENU m=CreatePopupMenu();POINT p;GetCursorPos(&p);
    AppendMenuW(m,MF_STRING,ID_OPEN,L"打开 NOVA 窗口");AppendMenuW(m,MF_STRING|(pinned?MF_CHECKED:0),ID_PIN,L"窗口置顶");
    AppendMenuW(m,MF_STRING|(startup_enabled?MF_CHECKED:0),ID_STARTUP,L"开机启动");AppendMenuW(m,MF_SEPARATOR,0,NULL);AppendMenuW(m,MF_STRING,ID_QUIT,L"退出 NOVA");
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
        space_list=control(L"LISTBOX",L"工作区",LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL,ID_SPACES);SendMessageW(space_list,LB_SETITEMHEIGHT,0,px(44));
        new_button=button(L"新建工作区",ID_NEW);
        startup_enabled=read_startup();settings_button=button(L"设置",ID_SETTINGS);pin_button=button(L"窗口置顶 (F11)",ID_PIN);
        minimize_button=button(L"最小化",ID_MINIMIZE);maximize_button=button(L"最大化",ID_MAXIMIZE);close_button=button(L"关闭",ID_CLOSE);
        search_edit=control(L"EDIT",L"",ES_AUTOHSCROLL,ID_SEARCH);SendMessageW(search_edit,EM_SETCUEBANNER,TRUE,(LPARAM)L"搜索当前工作区  Ctrl+K");SendMessageW(search_edit,EM_SETLIMITTEXT,127,0);
        add_button=button(L"添加文件",ID_ADD);folder_button=button(L"添加文件夹",ID_FOLDER);
        app_list=control(WC_LISTVIEWW,L"工作区启动项",LVS_ICON|LVS_AUTOARRANGE|LVS_SINGLESEL|LVS_SHOWSELALWAYS,ID_APPS);
        hold_drag_init(&hold_drag,app_list,space_list,moved_item,NULL);
        ListView_SetBkColor(app_list,BG);ListView_SetTextBkColor(app_list,BG);ListView_SetTextColor(app_list,TEXT);ListView_SetExtendedListViewStyle(app_list,LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);ListView_SetIconSpacing(app_list,px(136),px(112));SetWindowTheme(app_list,L"DarkMode_Explorer",NULL);
        name_edit=control(L"EDIT",L"",ES_AUTOHSCROLL,ID_NAME);SendMessageW(name_edit,EM_SETLIMITTEXT,39,0);ShowWindow(name_edit,SW_HIDE);
        HWND children[]={space_list,new_button,settings_button,pin_button,minimize_button,maximize_button,close_button,search_edit,add_button,folder_button,app_list,name_edit};
        for(unsigned i=0;i<sizeof(children)/sizeof(children[0]);i++){SetWindowSubclass(children[i],child_proc,1,0);DragAcceptFiles(children[i],TRUE);}DragAcceptFiles(hwnd,TRUE);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,NULL,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        add_tooltip(pin_button,L"置顶 / 取消置顶 (F11)");add_tooltip(settings_button,L"设置：开机启动、工作区管理");add_tooltip(new_button,L"新建工作区（最多 8 个）");
        add_tooltip(minimize_button,L"最小化");add_tooltip(maximize_button,L"最大化 / 还原");add_tooltip(close_button,L"关闭");
        refresh_spaces();refresh_apps();layout_controls();sample_stats();SetTimer(hwnd,STATS_TIMER,3000,NULL);if(prefer_pin)SetTimer(hwnd,START_PIN_TIMER,800,NULL);
        if(!test_mode){add_tray();RegisterHotKey(hwnd,ID_HOTKEY,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'N');}
        BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));return 0;
    }
    case WM_NCCALCSIZE:if(wp){RECT original=((NCCALCSIZE_PARAMS*)lp)->rgrc[0];DefWindowProcW(hwnd,msg,wp,lp);((NCCALCSIZE_PARAMS*)lp)->rgrc[0].top=original.top+(IsZoomed(hwnd)?GetSystemMetrics(SM_CYSIZEFRAME)+GetSystemMetrics(SM_CXPADDEDBORDER):1);return 0;}break;
    case WM_NCHITTEST:{LRESULT result=DefWindowProcW(hwnd,msg,wp,lp);if(result!=HTCLIENT)return result;POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);RECT r;GetClientRect(hwnd,&r);if(!IsZoomed(hwnd)&&p.y<px(4))return HTTOP;if(p.y<caption_height()&&p.x<r.right-px(230))return HTCAPTION;return HTCLIENT;}
    case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;m->ptMinTrackSize.x=px(780);m->ptMinTrackSize.y=px(600);return 0;}
    case WM_SIZE:layout_controls();if(wp==SIZE_MINIMIZED&&!pinned)ShowWindow(hwnd,SW_HIDE);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);paint(dc,r);EndPaint(hwnd,&ps);return 0;}
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,PANEL);return (LRESULT)panel_brush;
    case WM_DRAWITEM:{
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;wchar_t caption[80];BOOL selected=(d->itemState&ODS_SELECTED)!=0;
        if(d->CtlID==ID_PIN||d->CtlID==ID_SETTINGS||d->CtlID==ID_NEW||d->CtlID==ID_MINIMIZE||d->CtlID==ID_MAXIMIZE||d->CtlID==ID_CLOSE){draw_icon(d);return TRUE;}
        COLORREF fill=selected?RGB(48,65,95):PANEL;
        if(d->CtlID==ID_ADD)fill=selected?RGB(76,103,159):RGB(53,77,122);
        if(d->CtlID==ID_SPACES){if(d->itemID==(UINT)-1)return TRUE;SendMessageW(space_list,LB_GETTEXT,d->itemID,(LPARAM)caption);if((int)d->itemID==active_space)fill=RGB(42,56,79);}
        else GetWindowTextW(d->hwndItem,caption,80);
        HBRUSH b=CreateSolidBrush(fill);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);
        RECT t=d->rcItem;t.left+=px(12);t.right-=px(8);
        text(d->hDC,caption,t,body_font,(d->itemState&ODS_DISABLED)?RGB(112,128,150):TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|(d->CtlID==ID_SPACES?DT_LEFT:DT_CENTER));
        if(d->itemState&ODS_FOCUS){RECT f=d->rcItem;InflateRect(&f,-3,-3);DrawFocusRect(d->hDC,&f);}return TRUE;
    }
    case WM_COMMAND:{int id=LOWORD(wp);
        if(id==ID_SPACES&&HIWORD(wp)==LBN_DBLCLK){launch_workspace();return 0;}
        if(id==ID_MINIMIZE){ShowWindow(hwnd,SW_MINIMIZE);return 0;}
        if(id==ID_MAXIMIZE){ShowWindow(hwnd,IsZoomed(hwnd)?SW_RESTORE:SW_MAXIMIZE);return 0;}
        if(id==ID_CLOSE){PostMessageW(hwnd,WM_CLOSE,0,0);return 0;}
        if(id==ID_SETTINGS){settings_menu();return 0;}
        if(id==ID_SPACES&&HIWORD(wp)==LBN_SELCHANGE){end_name_edit(TRUE);int i=(int)SendMessageW(space_list,LB_GETCURSEL,0,0);if(i>=0){active_space=i;save_config();SetWindowTextW(search_edit,L"");refresh_apps();}return 0;}
        if(id==ID_SEARCH&&HIWORD(wp)==EN_CHANGE){refresh_apps();return 0;}
        if(id==ID_NAME&&HIWORD(wp)==EN_KILLFOCUS){end_name_edit(TRUE);return 0;}
        switch(id){case ID_ADD:pick_file();break;case ID_FOLDER:pick_folder();break;case ID_PIN:set_pinned(!pinned);break;case ID_STARTUP:toggle_startup();break;case ID_NEW:new_space();break;case ID_RENAME:edit_name();break;case ID_DELETE:delete_space();break;case ID_REMOVE:remove_app();break;case ID_OPEN:restore_window();break;case ID_QUIT:DestroyWindow(hwnd);break;}return 0;
    }
    case WM_NOTIFY:{NMHDR *n=(NMHDR*)lp;
        if(n->idFrom==ID_APPS&&n->code==NM_DBLCLK&&!hold_drag.dragging)open_app();
        if(n->idFrom==ID_APPS&&n->code==LVN_GETINFOTIPW){NMLVGETINFOTIPW *tip=(NMLVGETINFOTIPW*)lp;LVITEMW i={0};i.mask=LVIF_PARAM;i.iItem=tip->iItem;SendMessageW(app_list,LVM_GETITEMW,0,(LPARAM)&i);lstrcpynW(tip->pszText,spaces[active_space].apps[i.lParam].target,tip->cchTextMax);}
        if(n->idFrom==ID_APPS&&n->code==LVN_GETEMPTYMARKUP){NMLVEMPTYMARKUP *m=(NMLVEMPTYMARKUP*)lp;m->dwFlags=EMF_CENTERED;lstrcpyW(m->szMarkup,spaces[active_space].app_count?L"没有匹配的启动项。清空搜索以查看全部。":L"把应用或文件拖到这里\n也可以点击上方「添加文件」。");return TRUE;}
        return 0;
    }
    case WM_CONTEXTMENU:if((HWND)wp==app_list&&selected_app()>=0){HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,ID_REMOVE,L"从工作区移除");POINT p;GetCursorPos(&p);UINT id=TrackPopupMenu(m,TPM_RETURNCMD,p.x,p.y,0,hwnd,NULL);DestroyMenu(m);if(id==ID_REMOVE)remove_app();}return 0;
    case WM_DROPFILES:handle_drop((HDROP)wp);return 0;
    case WM_KEYDOWN:if(wp==VK_F11)set_pinned(!pinned);if(wp==VK_ESCAPE&&pinned)set_pinned(FALSE);return 0;
    case WM_TIMER:
        if(wp==LAUNCH_TIMER){if(!launch_queue_step(&launch_queue,dispatch_launch,NULL)){KillTimer(hwnd,LAUNCH_TIMER);wchar_t message[160];swprintf(message,160,L"工作区启动请求已完成：已发送 %d 项，失败 %d 项。",launch_queue.count-launch_queue.failed,launch_queue.failed);set_notice(message);}return 0;}
        if(wp==START_PIN_TIMER){KillTimer(hwnd,START_PIN_TIMER);set_pinned(TRUE);return 0;}if(IsWindowVisible(hwnd)&&!IsIconic(hwnd)){sample_stats();RECT r;GetClientRect(hwnd,&r);r.top=r.bottom-px(60);InvalidateRect(hwnd,&r,FALSE);}return 0;
    case WM_RESTORE_NOVA:case WM_HOTKEY:restore_window();return 0;
    case WM_TRAY:if(lp==WM_LBUTTONUP)restore_window();if(lp==WM_RBUTTONUP)tray_menu();return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:launch_queue_cancel(&launch_queue);KillTimer(hwnd,LAUNCH_TIMER);save_config();KillTimer(hwnd,STATS_TIMER);KillTimer(hwnd,START_PIN_TIMER);UnregisterHotKey(hwnd,ID_HOTKEY);if(!test_mode)Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show){
    (void)previous;(void)command;SetProcessDPIAware();CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    HDC screen=GetDC(NULL);dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"NOVA_DESKTOP_SINGLE_INSTANCE");
    if(GetLastError()==ERROR_ALREADY_EXISTS){HWND existing=FindWindowW(APP_CLASS,NULL);if(existing)PostMessageW(existing,WM_RESTORE_NOVA,0,0);CloseHandle(mutex);CoUninitialize();return 0;}
    wchar_t dir[MAX_PATH];if(FAILED(SHGetFolderPathW(NULL,CSIDL_APPDATA,NULL,SHGFP_TYPE_CURRENT,dir))||wcslen(dir)>MAX_PATH-40)return 1;
    wcscat(dir,L"\\NOVA Desktop");CreateDirectoryW(dir,NULL);swprintf(config_path,MAX_PATH,L"%ls\\config.ini",dir);load_config();
    INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    background=CreateSolidBrush(BG);panel_brush=CreateSolidBrush(PANEL);taskbar_message=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=window_proc;wc.lpszClassName=APP_CLASS;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);wc.hbrBackground=background;
    if(!RegisterClassExW(&wc))return 2;
    HWND hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,APP_CLASS,APP_NAME,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,px(1100),px(760),NULL,NULL,instance,NULL);
    if(!hwnd)return 3;
    ShowWindow(hwnd,show==SW_HIDE?SW_HIDE:SW_MAXIMIZE);UpdateWindow(hwnd);
    MSG msg={0};while(GetMessageW(&msg,NULL,0,0)>0){
        if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){if(hold_drag.dragging||hold_drag.armed){hold_drag_cancel(&hold_drag);continue;}if(launch_queue.running){launch_queue_cancel(&launch_queue);KillTimer(hwnd,LAUNCH_TIMER);set_notice(L"已取消剩余启动，已打开的应用保持运行。");continue;}}
        if(msg.message==WM_KEYDOWN&&(msg.wParam==VK_F11||(msg.wParam=='K'&&(GetKeyState(VK_CONTROL)&0x8000)))){if(msg.wParam==VK_F11)set_pinned(!pinned);else SetFocus(search_edit);continue;}
        if(msg.message==WM_KEYDOWN&&((msg.hwnd==name_edit&&(msg.wParam==VK_RETURN||msg.wParam==VK_ESCAPE))||(msg.hwnd==app_list&&(msg.wParam==VK_RETURN||msg.wParam==VK_DELETE))||(pinned&&msg.wParam==VK_ESCAPE))){DispatchMessageW(&msg);continue;}
        if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(images)ImageList_Destroy(images);
    DeleteObject(body_font);DeleteObject(small_font);DeleteObject(title_font);DeleteObject(brand_font);DeleteObject(background);DeleteObject(panel_brush);CloseHandle(mutex);CoUninitialize();return (int)msg.wParam;
}
