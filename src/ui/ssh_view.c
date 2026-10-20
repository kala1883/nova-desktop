#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <wincred.h>
#include <oleacc.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "ssh_view.h"
#include "../platform/storage.h"
#include "../i18n.h"

#define VIEW_CLASS L"NovaSshView"
#define DONE (WM_APP+80)
#define PROGRESS (WM_APP+81)
enum { S_LIST=5100,S_NAME,S_HOST,S_PORT,S_USER,S_DIR,S_KEY,S_SECRET,S_NEW,S_SAVE,S_REMOVE,S_CONNECT,S_KEY_PICK };
enum { V_CONNECT=5200,V_DISCONNECT,V_UPLOAD,V_DOWNLOAD,V_CANCEL,V_EXECUTE,V_INPUT,V_OUTPUT,V_LIST,V_STATUS };
enum { J_CONNECT=1,J_LIST,J_MKDIR,J_RENAME,J_DELETE,J_UPLOAD,J_DOWNLOAD,J_COPY,J_MOVE,J_EXEC };
typedef struct EmptyDialog { DLGTEMPLATE dialog; WORD menu,window_class,title; } EmptyDialog;
static int dpi_scale(int value){HDC dc=GetDC(NULL);int dpi=dc?GetDeviceCaps(dc,LOGPIXELSX):96;if(dc)ReleaseDC(NULL,dc);return MulDiv(value,dpi,96);}
static HWND ssh_hot_button;
static LRESULT CALLBACK ssh_button_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;if(msg==WM_MOUSEMOVE&&ssh_hot_button!=h){if(ssh_hot_button)InvalidateRect(ssh_hot_button,NULL,TRUE);ssh_hot_button=h;TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,NULL,TRUE);}
    if(msg==WM_MOUSELEAVE||msg==WM_NCDESTROY){if(ssh_hot_button==h)ssh_hot_button=NULL;InvalidateRect(h,NULL,TRUE);}
    if(msg==WM_ENABLE||msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,NULL,TRUE);
    return DefSubclassProc(h,msg,wp,lp);
}
static LRESULT draw_ssh_button(DRAWITEMSTRUCT *draw){
    COLORREF color=(draw->itemState&ODS_SELECTED)?RGB(53,77,122):ssh_hot_button==draw->hwndItem?RGB(43,57,78):RGB(25,34,49);HBRUSH brush=CreateSolidBrush(color);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
    SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,(draw->itemState&ODS_DISABLED)?RGB(105,117,136):RGB(236,241,249));HFONT font=(HFONT)SendMessageW(draw->hwndItem,WM_GETFONT,0,0),old=SelectObject(draw->hDC,font);wchar_t text[128];GetWindowTextW(draw->hwndItem,text,128);RECT r=draw->rcItem;DrawTextW(draw->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(draw->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(draw->hDC,&r);}SelectObject(draw->hDC,old);return TRUE;
}
static HWND control(HWND parent,const wchar_t *cls,const wchar_t *name,DWORD style,int id,int x,int y,int w,int h,HFONT font){
    if(!lstrcmpW(cls,L"BUTTON"))style=(style&~BS_TYPEMASK)|BS_OWNERDRAW;
    HWND window=CreateWindowExW(0,cls,name,WS_CHILD|WS_VISIBLE|style,dpi_scale(x),dpi_scale(y),dpi_scale(w),dpi_scale(h),parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(window,WM_SETFONT,(WPARAM)font,TRUE);SetWindowTheme(window,L"DarkMode_Explorer",NULL);if(!lstrcmpW(cls,L"BUTTON"))SetWindowSubclass(window,ssh_button_proc,1,0);return window;
}
static void center_dialog(HWND h,int width,int height){
    RECT area;SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);width=min(dpi_scale(width),area.right-area.left);height=min(dpi_scale(height),area.bottom-area.top);
    SetWindowPos(h,NULL,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,SWP_NOZORDER);
    BOOL dark=TRUE;DwmSetWindowAttribute(h,20,&dark,sizeof(dark));
}
typedef struct ServerDialog { SshServers servers; SshLogin *out; int selected; BOOL fixed; HFONT font; HBRUSH brush; } ServerDialog;
static void server_list(HWND h,ServerDialog *d){
    SendDlgItemMessageW(h,S_LIST,LB_RESETCONTENT,0,0);for(int i=0;i<d->servers.count;i++)SendDlgItemMessageW(h,S_LIST,LB_ADDSTRING,0,(LPARAM)d->servers.items[i].name);
    SendDlgItemMessageW(h,S_LIST,LB_SETCURSEL,d->selected,0);
    EnableWindow(GetDlgItem(h,S_NEW),!d->fixed&&d->servers.count<SSH_SERVER_LIMIT);EnableWindow(GetDlgItem(h,S_REMOVE),!d->fixed&&d->selected>=0);
    EnableWindow(GetDlgItem(h,S_CONNECT),d->selected>=0);
}
static void server_fields(HWND h,ServerDialog *d){
    SshServer empty={0};SshServer *s=d->selected>=0?&d->servers.items[d->selected]:&empty;wchar_t port[16];swprintf(port,16,L"%d",s->port?s->port:22);
    SetDlgItemTextW(h,S_NAME,s->name);SetDlgItemTextW(h,S_HOST,s->host);SetDlgItemTextW(h,S_PORT,port);SetDlgItemTextW(h,S_USER,s->user);SetDlgItemTextW(h,S_DIR,s->directory[0]?s->directory:L"/");
    SetDlgItemTextW(h,S_SECRET,L"");SetDlgItemTextW(h,S_KEY,L"");server_list(h,d);
}
static BOOL save_server(HWND h,ServerDialog *d){
    if(d->selected<0)return TRUE;
    SshServers *next=malloc(sizeof(*next));if(!next)return FALSE;*next=d->servers;SshServer *s=&next->items[d->selected];wchar_t port[24],*end;
    GetDlgItemTextW(h,S_NAME,s->name,SSH_NAME_CAP);GetDlgItemTextW(h,S_HOST,s->host,256);GetDlgItemTextW(h,S_USER,s->user,128);GetDlgItemTextW(h,S_DIR,s->directory,SSH_PATH_CAP);GetDlgItemTextW(h,S_PORT,port,24);
    long n=wcstol(port,&end,10);s->port=port[0]&&!*end&&n>=1&&n<=65535?(int)n:0;
    BOOL ok=store_save_ssh_servers(next);if(ok)d->servers=*next;else MessageBoxW(h,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);free(next);if(ok)server_list(h,d);return ok;
}
static INT_PTR CALLBACK server_proc(HWND h,UINT message,WPARAM wp,LPARAM lp){
    ServerDialog *d=(ServerDialog*)GetWindowLongPtrW(h,DWLP_USER);
    if(message==WM_INITDIALOG){d=(ServerDialog*)lp;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);center_dialog(h,800,594);SetWindowTextW(h,nova_text(L"服务器管理 · SSH / SFTP",L"Servers · SSH / SFTP"));
        d->brush=CreateSolidBrush(RGB(22,29,42));d->font=CreateFontW(-dpi_scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        control(h,L"LISTBOX",nova_text(L"已保存服务器",L"Saved servers"),WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,S_LIST,16,18,192,414,d->font);
        control(h,L"BUTTON",nova_text(L"新建",L"New"),WS_TABSTOP,S_NEW,16,446,90,32,d->font);control(h,L"BUTTON",nova_text(L"移除",L"Remove"),WS_TABSTOP,S_REMOVE,116,446,92,32,d->font);
        const int ids[]={S_NAME,S_HOST,S_PORT,S_USER,S_DIR,S_KEY,S_SECRET};const wchar_t *labels[]={nova_text(L"名称",L"Name"),nova_text(L"主机",L"Host"),nova_text(L"端口",L"Port"),nova_text(L"用户名",L"Username"),nova_text(L"初始目录",L"Initial directory"),nova_text(L"私钥文件（不选 .pub；留空用密码）",L"Private key (not .pub; empty: password)"),nova_text(L"密码 / 私钥口令（不保存）",L"Password / key passphrase (not saved)")};
        const int caps[]={SSH_NAME_CAP,256,6,128,SSH_PATH_CAP,MAX_PATH,512};
        for(int i=0;i<7;i++){int y=16+i*61;control(h,L"STATIC",labels[i],0,5400+i,230,y,530,22,d->font);HWND edit=control(h,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|(ids[i]==S_SECRET?ES_PASSWORD:0),ids[i],230,y+24,ids[i]==S_KEY?440:530,30,d->font);SendMessageW(edit,EM_SETLIMITTEXT,caps[i]-1,0);}
        control(h,L"BUTTON",nova_text(L"浏览…",L"Browse…"),WS_TABSTOP,S_KEY_PICK,680,16+5*61+24,80,30,d->font);
        control(h,L"STATIC",nova_text(L"文件操作通过 SFTP，命令在服务器上执行。首次连接需核验主机指纹。",L"Files use SFTP; commands run on the server. Verify the host fingerprint on first connection."),0,5410,230,451,530,42,d->font);
        control(h,L"BUTTON",nova_text(L"保存",L"Save"),WS_TABSTOP,S_SAVE,464,505,90,34,d->font);control(h,L"BUTTON",nova_text(L"连接",L"Connect"),WS_TABSTOP|BS_DEFPUSHBUTTON,S_CONNECT,564,505,90,34,d->font);control(h,L"BUTTON",nova_text(L"关闭",L"Close"),WS_TABSTOP,IDCANCEL,664,505,96,34,d->font);
        if(!d->servers.count&&!d->fixed){d->servers.count=1;d->selected=0;SshServer *s=&d->servers.items[0];s->id=store_new_id();s->port=22;lstrcpyW(s->name,nova_text(L"服务器 1",L"Server 1"));lstrcpyW(s->directory,L"/");}
        EnableWindow(GetDlgItem(h,S_LIST),!d->fixed);server_fields(h,d);return TRUE;
    }
    if(!d)return FALSE;
    switch(message){
    case WM_DRAWITEM:return draw_ssh_button((DRAWITEMSTRUCT*)lp);
    case WM_COMMAND:
        if(LOWORD(wp)==S_LIST&&HIWORD(wp)==LBN_SELCHANGE){int index=(int)SendDlgItemMessageW(h,S_LIST,LB_GETCURSEL,0,0);if(index!=d->selected){if(save_server(h,d)){d->selected=index;server_fields(h,d);}else server_list(h,d);}return TRUE;}
        switch(LOWORD(wp)){
        case S_NEW:if(d->servers.count<SSH_SERVER_LIMIT&&save_server(h,d)){d->selected=d->servers.count++;SshServer *s=&d->servers.items[d->selected];ZeroMemory(s,sizeof(*s));s->id=store_new_id();s->port=22;swprintf(s->name,SSH_NAME_CAP,nova_text(L"服务器 %d",L"Server %d"),d->servers.count);lstrcpyW(s->directory,L"/");server_fields(h,d);}return TRUE;
        case S_SAVE:save_server(h,d);return TRUE;
        case S_REMOVE:if(d->selected>=0&&MessageBoxW(h,nova_text(L"移除此连接配置？服务器上的文件不会被删除。",L"Remove this connection profile? No server files will be deleted."),L"NOVA Desktop",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES){
            SshServers *next=malloc(sizeof(*next));if(!next)return TRUE;*next=d->servers;memmove(&next->items[d->selected],&next->items[d->selected+1],(size_t)(next->count-d->selected-1)*sizeof(SshServer));next->count--;
            if(store_save_ssh_servers(next)){d->servers=*next;d->selected=next->count?min(d->selected,next->count-1):-1;server_fields(h,d);}else MessageBoxW(h,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);free(next);}return TRUE;
        case S_KEY_PICK:{wchar_t path[MAX_PATH]=L"";OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=h;dialog.lpstrFile=path;dialog.nMaxFile=MAX_PATH;dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;dialog.lpstrTitle=nova_text(L"选择 SSH 私钥",L"Select SSH private key");if(GetOpenFileNameW(&dialog))SetDlgItemTextW(h,S_KEY,path);return TRUE;}
        case IDOK:case S_CONNECT:
            GetDlgItemTextW(h,S_SECRET,d->out->secret,512);GetDlgItemTextW(h,S_KEY,d->out->key_file,MAX_PATH);
            if(!d->out->secret[0]&&!d->out->key_file[0]){MessageBoxW(h,nova_text(L"请填写服务器账号的登录密码，或选择 SSH 私钥。只填写主机和用户名无法完成登录。",L"Enter the server account password or select an SSH private key. A host and username alone cannot complete login."),nova_text(L"需要 SSH 登录凭据",L"SSH login credentials required"),MB_OK|MB_ICONWARNING);SetFocus(GetDlgItem(h,S_SECRET));return TRUE;}
            if(d->selected>=0&&save_server(h,d)){d->out->server=d->servers.items[d->selected];EndDialog(h,IDOK);}return TRUE;
        case IDCANCEL:EndDialog(h,IDCANCEL);return TRUE;
        }break;
    case WM_CTLCOLORDLG:return (INT_PTR)d->brush;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,RGB(236,241,249));SetBkColor((HDC)wp,RGB(22,29,42));return (INT_PTR)d->brush;
    case WM_CLOSE:EndDialog(h,IDCANCEL);return TRUE;
    }
    return FALSE;
}
BOOL ssh_choose_server(HWND owner,int64_t fixed_id,SshLogin *login){
    ServerDialog *d=calloc(1,sizeof(*d));if(!d)return FALSE;d->out=login;d->selected=-1;d->fixed=fixed_id!=0;
    if(!store_load_ssh_servers(&d->servers)){MessageBoxW(owner,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);free(d);return FALSE;}
    for(int i=0;i<d->servers.count;i++)if(!fixed_id||d->servers.items[i].id==fixed_id){d->selected=i;break;}
    if(fixed_id&&d->selected<0){MessageBoxW(owner,nova_text(L"此服务器配置已被移除。请从服务器管理中新建连接。",L"This server profile was removed. Create a connection in Servers."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);free(d);return FALSE;}
    EmptyDialog empty={0};empty.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;empty.dialog.cx=450;empty.dialog.cy=320;
    INT_PTR result=DialogBoxIndirectParamW(GetModuleHandleW(NULL),&empty.dialog,GetAncestor(owner,GA_ROOT),server_proc,(LPARAM)d);
    if(d->font)DeleteObject(d->font);
    if(d->brush)DeleteObject(d->brush);
    SecureZeroMemory(d,sizeof(*d));free(d);return result==IDOK;
}
BOOL ssh_pick_server(HWND owner,HWND anchor,SshLogin *login){
    SshServers *servers=calloc(1,sizeof(*servers));if(!servers)return FALSE;
    if(!store_load_ssh_servers(servers)){MessageBoxW(owner,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);free(servers);return FALSE;}
    if(!servers->count){free(servers);return ssh_choose_server(owner,0,login);}
    HMENU menu=CreatePopupMenu();if(!menu){free(servers);return FALSE;}
    for(int i=0;i<servers->count;i++){
        /* Menu syntax treats '&' as a mnemonic; server names remain literal. */
        wchar_t label[SSH_NAME_CAP*2];size_t n=0;
        for(const wchar_t *p=servers->items[i].name;*p;p++){if(*p==L'&')label[n++]=L'&';label[n++]=*p;}label[n]=0;
        AppendMenuW(menu,MF_STRING,(UINT_PTR)(i+1),label);
    }
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,SSH_SERVER_LIMIT+1,nova_text(L"管理服务器…",L"Manage servers…"));
    RECT area;GetWindowRect(anchor,&area);UINT id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,area.left,area.bottom,0,owner,NULL);DestroyMenu(menu);
    int64_t server_id=id>=1&&id<=(UINT)servers->count?servers->items[id-1].id:0;free(servers);
    if(!id)return FALSE;
    return ssh_choose_server(owner,server_id,login);
}
typedef struct TextDialog { const wchar_t *title; wchar_t *text; int cap;HBRUSH brush; } TextDialog;
static INT_PTR CALLBACK text_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    TextDialog *d=(TextDialog*)GetWindowLongPtrW(h,DWLP_USER);
    if(msg==WM_INITDIALOG){d=(TextDialog*)lp;SetWindowLongPtrW(h,DWLP_USER,(LONG_PTR)d);center_dialog(h,490,160);SetWindowTextW(h,d->title);HFONT font=GetStockObject(DEFAULT_GUI_FONT);control(h,L"STATIC",nova_text(L"名称",L"Name"),0,99,16,2,440,18,font);HWND edit=control(h,L"EDIT",d->text,WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,100,16,22,440,30,font);SendMessageW(edit,EM_SETLIMITTEXT,d->cap-1,0);control(h,L"BUTTON",nova_text(L"确定",L"OK"),WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,260,65,92,30,font);control(h,L"BUTTON",nova_text(L"取消",L"Cancel"),WS_TABSTOP,IDCANCEL,364,65,92,30,font);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);return FALSE;}
    if(msg==WM_COMMAND&&LOWORD(wp)==IDOK){GetDlgItemTextW(h,100,d->text,d->cap);EndDialog(h,IDOK);return TRUE;}
    if(msg==WM_DRAWITEM)return draw_ssh_button((DRAWITEMSTRUCT*)lp);
    if(msg==WM_CTLCOLORDLG)return (INT_PTR)d->brush;
    if(msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORSTATIC){SetTextColor((HDC)wp,RGB(236,241,249));SetBkColor((HDC)wp,RGB(22,29,42));return (INT_PTR)d->brush;}
    if(msg==WM_CLOSE||(msg==WM_COMMAND&&LOWORD(wp)==IDCANCEL)){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
static BOOL ask_text(HWND owner,const wchar_t *title,wchar_t *text,int cap){TextDialog data={title,text,cap,NULL};data.brush=CreateSolidBrush(RGB(22,29,42));EmptyDialog empty={0};empty.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;empty.dialog.cx=280;empty.dialog.cy=90;INT_PTR result=DialogBoxIndirectParamW(GetModuleHandleW(NULL),&empty.dialog,GetAncestor(owner,GA_ROOT),text_proc,(LPARAM)&data);DeleteObject(data.brush);return result==IDOK;}

typedef struct RemoteView {
    HWND window,list,status,input,output,buttons[7];HFONT font;HBRUSH brush;
    IAccPropServices *accessibility;
    int64_t server_id;wchar_t path[SSH_PATH_CAP],request_path[SSH_PATH_CAP],other[SSH_PATH_CAP],local[MAX_PATH],script[SSH_PATH_CAP];
    SshLogin login;SshEntry *entries;int count,job;BOOL busy,connected,runtime;volatile LONG stopping;
    HANDLE thread,event,cancel;SshResult result;
} RemoteView;
static int worker_count;
static struct { int64_t server_id;wchar_t path[SSH_PATH_CAP];BOOL cut,valid; } clipboard;
static RemoteView *view_state(HWND view){return (RemoteView*)GetWindowLongPtrW(view,GWLP_USERDATA);}
static void show_status(RemoteView *v,const wchar_t *text){SetWindowTextW(v->status,text);}
static void controls_enabled(RemoteView *v){
    for(int i=0;i<7;i++)EnableWindow(v->buttons[i],i==4?v->busy:!v->busy&&(i==0||v->connected));
    EnableWindow(v->list,!v->busy&&v->connected);EnableWindow(v->input,!v->busy&&v->connected);
}
static void report_progress(void *context,int percent){RemoteView *v=context;PostMessageW(v->window,PROGRESS,(WPARAM)max(0,min(100,percent)),0);}
static DWORD WINAPI remote_worker(void *context){
    RemoteView *v=context;SshClient *client=NULL;
    for(;;){WaitForSingleObject(v->event,INFINITE);if(InterlockedCompareExchange(&v->stopping,0,0))break;
        SshResult *r=&v->result;ZeroMemory(r,sizeof(*r));
        if(v->job==J_CONNECT){ssh_client_close(client);client=ssh_client_connect(&v->login,v->cancel,r);if(client){ssh_client_set_progress(client,report_progress,v);ssh_client_list(client,v->request_path,r);}}
        else if(!client){r->code=1;lstrcpyW(r->error,nova_text(L"SSH 已断开；未执行操作。",L"SSH is disconnected; no operation was executed."));}
        else switch(v->job){
        case J_LIST:ssh_client_list(client,v->request_path,r);break;
        case J_MKDIR:ssh_client_mkdir(client,v->other,r);break;
        case J_RENAME:case J_MOVE:ssh_client_rename(client,v->other,v->request_path,r);break;
        case J_DELETE:ssh_client_delete(client,v->other,r);break;
        case J_UPLOAD:case J_DOWNLOAD:ssh_client_transfer(client,v->job==J_UPLOAD,v->local,v->other,r);break;
        case J_COPY:ssh_client_copy(client,v->other,v->request_path,r);break;
        case J_EXEC:ssh_client_exec(client,v->request_path,v->script,r);break;
        }
        if(r->code==3||(r->code&&!ssh_client_healthy(client))){ssh_client_close(client);client=NULL;}
        if(InterlockedCompareExchange(&v->stopping,0,0))break;
        if(!PostMessageW(v->window,DONE,client!=NULL,0))break;
    }
    ssh_client_close(client);return 0;
}
static BOOL submit(RemoteView *v,int job){
    if(v->busy)return FALSE;
    if(!v->connected&&job!=J_CONNECT){show_status(v,nova_text(L"请先连接此服务器；未在本机执行任何操作。",L"Connect to this server first; nothing was executed locally."));return FALSE;}
    if(!v->thread){
        if(worker_count>=4){show_status(v,nova_text(L"最多同时使用 4 个远程标签，请先断开一个连接。",L"At most 4 remote tabs can be active. Disconnect one first."));return FALSE;}
        v->thread=CreateThread(NULL,0,remote_worker,v,0,NULL);if(!v->thread){show_status(v,nova_text(L"无法启动 SSH 工作线程。",L"Unable to start SSH worker."));return FALSE;}worker_count++;
    }
    ResetEvent(v->cancel);v->job=job;v->busy=TRUE;controls_enabled(v);show_status(v,nova_text(L"正在处理…（远程写操作和命令不会自动重试）",L"Working… (remote writes and commands are never retried automatically)"));SetEvent(v->event);return TRUE;
}
static void stop_worker(RemoteView *v){
    if(v->thread){InterlockedExchange(&v->stopping,TRUE);SetEvent(v->cancel);SetEvent(v->event);WaitForSingleObject(v->thread,INFINITE);CloseHandle(v->thread);v->thread=NULL;worker_count--;InterlockedExchange(&v->stopping,FALSE);}
    v->busy=v->connected=FALSE;ssh_result_dispose(&v->result);SecureZeroMemory(v->login.secret,sizeof(v->login.secret));SecureZeroMemory(v->login.key_file,sizeof(v->login.key_file));
    MSG message;while(PeekMessageW(&message,v->window,DONE,DONE,PM_REMOVE)){}controls_enabled(v);
}
static void trust_target(const SshServer *server,wchar_t *target){swprintf(target,384,L"NOVA Desktop SSH host:%ls:%d",server->host,server->port);}
BOOL ssh_view_connect(HWND window,const SshLogin *login){
    RemoteView *v=view_state(window);if(!v||v->busy||login->server.id!=v->server_id)return FALSE;
    v->login=*login;wchar_t target[384];trust_target(&login->server,target);PCREDENTIALW credential=NULL;
    if(CredReadW(target,CRED_TYPE_GENERIC,0,&credential)){if(credential->CredentialBlobSize==64){memcpy(v->login.fingerprint,credential->CredentialBlob,64);v->login.fingerprint[64]=0;}CredFree(credential);}
    lstrcpyW(v->request_path,v->path);return submit(v,J_CONNECT);
}
static void refresh_remote(RemoteView *v){if(v->busy)return;lstrcpyW(v->request_path,v->path);submit(v,J_LIST);}
static int selected(RemoteView *v){int row=ListView_GetNextItem(v->list,-1,LVNI_SELECTED);return row>=0&&row<v->count?row:-1;}
static void populate(RemoteView *v,SshResult *r){
    ListView_DeleteAllItems(v->list);free(v->entries);v->entries=r->entries;r->entries=NULL;v->count=r->count;
    for(int i=0;i<v->count;i++){SshEntry *e=&v->entries[i];LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=e->name;ListView_InsertItem(v->list,&item);
        wchar_t size[40],permissions[24];if(e->directory)lstrcpyW(size,nova_text(L"目录",L"Folder"));else swprintf(size,40,L"%llu",e->size);swprintf(permissions,24,L"%04lo",e->permissions&07777);ListView_SetItemText(v->list,i,1,size);ListView_SetItemText(v->list,i,2,permissions);if(e->link)ListView_SetItemText(v->list,i,1,(LPWSTR)nova_text(L"符号链接",L"Symlink"));}
    lstrcpyW(v->path,r->path);SendMessageW(GetParent(v->window),SSH_VIEW_NAVIGATED,(WPARAM)v->window,0);
}
static void finish(RemoteView *v,BOOL connected){
    v->busy=FALSE;v->connected=connected;SshResult *r=&v->result;
    if(r->code==2){
        wchar_t hash[80],message[768];MultiByteToWideChar(CP_ACP,0,r->fingerprint,-1,hash,80);swprintf(message,768,nova_text(L"首次连接到 %ls:%d。\n\nSHA256（十六进制）：\n%ls\n\n请通过可信渠道核对指纹。信任并连接？",L"First connection to %ls:%d.\n\nSHA256 (hexadecimal):\n%ls\n\nVerify this fingerprint through a trusted channel. Trust and connect?"),v->login.server.host,v->login.server.port,hash);
        if(MessageBoxW(v->window,message,nova_text(L"核验 SSH 主机",L"Verify SSH host"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){
            wchar_t target[384];trust_target(&v->login.server,target);CREDENTIALW credential={0};credential.Type=CRED_TYPE_GENERIC;credential.TargetName=target;credential.CredentialBlobSize=64;credential.CredentialBlob=(LPBYTE)r->fingerprint;credential.Persist=CRED_PERSIST_LOCAL_MACHINE;
            if(CredWriteW(&credential,0)){strcpy(v->login.fingerprint,r->fingerprint);ssh_result_dispose(r);submit(v,J_CONNECT);return;}
            show_status(v,nova_text(L"无法保存主机指纹；未连接。",L"Unable to save the host fingerprint; not connected."));
        }else show_status(v,nova_text(L"未信任主机，连接已取消。",L"Host was not trusted; connection cancelled."));
    }else if(r->code){
        wchar_t *details=calloc(SSH_PATH_CAP+4096,sizeof(wchar_t));
        if(details){wchar_t methods[256]=L"";MultiByteToWideChar(CP_UTF8,0,r->auth_methods,-1,methods,256);
            swprintf(details,SSH_PATH_CAP+4096,nova_text(L"服务器：%ls:%d\r\n目录：%ls\r\n失败阶段：%ls\r\nWindows 错误：%d\r\nSSH 错误：%d\r\nSFTP 错误：%lu\r\n服务器支持的认证方式：%ls\r\n\r\n%ls",L"Server: %ls:%d\r\nDirectory: %ls\r\nFailed stage: %ls\r\nWindows error: %d\r\nSSH error: %d\r\nSFTP error: %lu\r\nServer authentication methods: %ls\r\n\r\n%ls"),v->login.server.host,v->login.server.port,v->request_path,ssh_phase_name(r->phase),r->native_error,r->ssh_error,r->sftp_error,methods[0]?methods:nova_text(L"尚未取得",L"Not obtained"),r->error[0]?r->error:nova_text(L"操作失败；请检查连接和权限。",L"Operation failed; check the connection and permissions."));
            SetWindowTextW(v->output,details);
            if(v->job==J_CONNECT&&!r->cancelled)MessageBoxW(GetAncestor(v->window,GA_ROOT),details,nova_text(L"SSH 连接失败 · 完整错误详情",L"SSH connection failed · full error details"),MB_OK|MB_ICONERROR);
            free(details);
        }
        show_status(v,nova_text(L"操作失败；完整错误详情已保留在命令输出区，可选择并复制。",L"Operation failed; full error details are retained in the output area and can be selected and copied."));
    }
    else if(v->job==J_EXEC){
        wchar_t *output=calloc(SSH_OUTPUT_CAP+4096,sizeof(wchar_t));if(output){int n=r->output?MultiByteToWideChar(CP_UTF8,0,r->output,(int)r->output_length,output+4096,SSH_OUTPUT_CAP-1):0;
            for(int i=0;i<n;i++)if(!output[4096+i])output[4096+i]=L' ';
            wchar_t header[SSH_PATH_CAP+768];swprintf(header,SSH_PATH_CAP+768,nova_text(L"服务器：%ls:%d\r\n目录：%ls\r\n退出码：%d%ls\r\n\r\n",L"Server: %ls:%d\r\nDirectory: %ls\r\nExit code: %d%ls\r\n\r\n"),v->login.server.host,v->login.server.port,v->request_path,r->exit_code,r->output_truncated?nova_text(L"（仅保留末尾 64 KiB）",L" (last 64 KiB only)"):L"");
            size_t prefix=wcslen(header);memmove(output+prefix,output+4096,((size_t)max(n,0)+1)*sizeof(wchar_t));wmemcpy(output,header,prefix);SetWindowTextW(v->output,output);free(output);}
        show_status(v,nova_text(L"远程命令已结束。每次执行使用当前目录和新的非交互 Shell。",L"Remote command finished. Each run uses the current directory and a new non-interactive shell."));
    }else {if(r->entries)populate(v,r);show_status(v,nova_text(L"SSH 已连接。文件操作遵循服务器权限；上传和下载不会覆盖已有目标。",L"SSH connected. Server permissions apply; uploads and downloads do not overwrite existing destinations."));}
    SecureZeroMemory(v->login.secret,sizeof(v->login.secret));SecureZeroMemory(v->login.key_file,sizeof(v->login.key_file));int job=v->job;BOOL success=!r->code;ssh_result_dispose(r);controls_enabled(v);
    if(success&&job>=J_MKDIR&&job<=J_MOVE)refresh_remote(v);
}
BOOL ssh_view_location(HWND window,wchar_t *location,size_t capacity){RemoteView *v=view_state(window);return v&&ssh_location_format(location,capacity,v->server_id,v->path);}
void ssh_view_title(HWND window,wchar_t *title,int capacity){RemoteView *v=view_state(window);if(!v||capacity<128)return;const wchar_t *name=wcsrchr(v->path,L'/');swprintf(title,capacity,L"%.50ls · %.60ls",v->login.server.name[0]?v->login.server.name:L"SSH",name&&name[1]?name+1:L"/");}
BOOL ssh_view_navigate(HWND window,const wchar_t *location){
    RemoteView *v=view_state(window);if(!v||v->busy)return FALSE;int64_t id;const wchar_t *path=location;
    if(ssh_location_parse(location,&id,&path)&&id!=v->server_id){show_status(v,nova_text(L"请在新标签打开其他服务器。",L"Open another server in a new tab."));return FALSE;}
    if(path[0]!=L'/'||wcslen(path)>=SSH_PATH_CAP-40){show_status(v,nova_text(L"请输入有效的远程绝对目录。",L"Enter a valid absolute remote directory."));return FALSE;}
    lstrcpyW(v->request_path,path);return submit(v,J_LIST);
}
BOOL ssh_view_has_directory(HWND window,const wchar_t *name){RemoteView *v=view_state(window);if(!v)return FALSE;for(int i=0;i<v->count;i++)if((v->entries[i].directory||v->entries[i].link)&&!wcscmp(v->entries[i].name,name))return TRUE;return FALSE;}
BOOL ssh_view_run(HWND window,const wchar_t *command){RemoteView *v=view_state(window);if(!v||v->busy||!command[0]||wcslen(command)>=SSH_PATH_CAP)return FALSE;lstrcpyW(v->script,command);lstrcpyW(v->request_path,v->path);if(!submit(v,J_EXEC))return FALSE;SetWindowTextW(v->input,command);SetWindowTextW(v->output,nova_text(L"正在服务器上执行… 可取消等待；远端进程可能继续运行。",L"Running on the server… You may cancel waiting; the remote process may continue."));return TRUE;}
static BOOL choose_folder(HWND owner,wchar_t *path){BROWSEINFOW info={0};info.hwndOwner=owner;info.lpszTitle=nova_text(L"选择本地文件夹（不会覆盖同名目标）",L"Choose a local folder (existing destinations are not overwritten)");info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE id=SHBrowseForFolderW(&info);if(!id)return FALSE;BOOL ok=SHGetPathFromIDListW(id,path);CoTaskMemFree(id);return ok;}
static void transfer_dialog(RemoteView *v,BOOL upload){
    if(v->busy||!v->connected)return;
    if(upload){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,nova_text(L"上传文件…",L"Upload file…"));AppendMenuW(menu,MF_STRING,2,nova_text(L"上传文件夹…",L"Upload folder…"));POINT point;GetCursorPos(&point);int id=(int)TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,v->window,NULL);DestroyMenu(menu);if(!id)return;
        if(id==2){if(!choose_folder(v->window,v->local))return;}else{v->local[0]=0;OPENFILENAMEW dialog={0};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=v->window;dialog.lpstrFile=v->local;dialog.nMaxFile=MAX_PATH;dialog.lpstrTitle=nova_text(L"上传到当前远程目录",L"Upload into current remote directory");dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&dialog))return;}
        const wchar_t *name=wcsrchr(v->local,L'\\');if(!name||!ssh_path_join(v->other,SSH_PATH_CAP,v->path,name+1)){show_status(v,nova_text(L"路径无效或过长。",L"Invalid or oversized path."));return;}submit(v,J_UPLOAD);
    }else{int index=selected(v);if(index<0)return;if(!ssh_path_join(v->other,SSH_PATH_CAP,v->path,v->entries[index].name)||!choose_folder(v->window,v->local))return;
        const wchar_t *name=v->entries[index].name;size_t n=wcslen(v->local);if(wcspbrk(name,L"\\/:*?\"<>|")||n+wcslen(name)+2>=MAX_PATH){show_status(v,nova_text(L"远程名称无法作为 Windows 文件名或路径过长。",L"The remote name is not a valid Windows filename, or the path is too long."));return;}
        wcscat(v->local,L"\\");wcscat(v->local,name);submit(v,J_DOWNLOAD);
    }
}
void ssh_view_action(HWND window,int action){
    RemoteView *v=view_state(window);if(!v||v->busy)return;
    if(action==SSH_COMMAND){SetFocus(v->input);return;}
    if(action==SSH_REFRESH){refresh_remote(v);return;}
    if(action==SSH_UP){wchar_t parent[SSH_PATH_CAP];lstrcpyW(parent,v->path);wchar_t *slash=wcsrchr(parent,L'/');if(slash==parent)parent[1]=0;else if(slash)*slash=0;ssh_view_navigate(window,parent);return;}
    if(!v->connected){show_status(v,nova_text(L"请先连接服务器。",L"Connect to the server first."));return;}
    if(action==SSH_NEWFOLDER){wchar_t name[512]=L"";if(ask_text(window,nova_text(L"新建远程文件夹",L"New remote folder"),name,512)&&ssh_path_join(v->other,SSH_PATH_CAP,v->path,name))submit(v,J_MKDIR);return;}
    if(action==SSH_PASTE){if(!clipboard.valid||clipboard.server_id!=v->server_id){show_status(v,nova_text(L"请先复制同一服务器的项目；本地传输请使用上传/下载。",L"Copy an item from the same server first; use Upload/Download for local transfers."));return;}
        const wchar_t *name=wcsrchr(clipboard.path,L'/');if(!name||!ssh_path_join(v->request_path,SSH_PATH_CAP,v->path,name+1))return;lstrcpyW(v->other,clipboard.path);if(submit(v,clipboard.cut?J_MOVE:J_COPY)&&clipboard.cut)clipboard.valid=FALSE;return;}
    int index=selected(v);if(index<0){show_status(v,nova_text(L"请先选择一个远程项目。",L"Select a remote item first."));return;}
    if(!ssh_path_join(v->other,SSH_PATH_CAP,v->path,v->entries[index].name))return;
    if(action==SSH_COPY||action==SSH_CUT){clipboard.server_id=v->server_id;lstrcpyW(clipboard.path,v->other);clipboard.cut=action==SSH_CUT;clipboard.valid=TRUE;show_status(v,nova_text(L"已记录远程文件引用；粘贴时才执行操作。",L"Remote file reference recorded; the operation starts only when pasted."));return;}
    if(action==SSH_RENAME){wchar_t name[512];lstrcpyW(name,v->entries[index].name);if(ask_text(window,nova_text(L"重命名远程项目",L"Rename remote item"),name,512)&&ssh_path_join(v->request_path,SSH_PATH_CAP,v->path,name))submit(v,J_RENAME);return;}
    if(action==SSH_DELETE){wchar_t message[2600];swprintf(message,2600,nova_text(L"永久删除服务器上的此项目及其目录内容？\n\n%ls\n\n不进入 Windows 回收站；操作可能部分完成。",L"Permanently delete this server item and its directory contents?\n\n%ls\n\nNo Windows Recycle Bin; the operation may partially complete."),v->other);if(MessageBoxW(window,message,nova_text(L"永久删除远程项目",L"Permanently delete remote item"),MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES)submit(v,J_DELETE);}
}
static void layout_view(RemoteView *v){
    RECT r;GetClientRect(v->window,&r);int gap=dpi_scale(5),width=r.right,height=r.bottom;int columns=max(1,min(6,(width-gap)/dpi_scale(66))),button_width=max(1,(width-gap)/columns-gap),rows=(6+columns-1)/columns;
    for(int i=0;i<6;i++)MoveWindow(v->buttons[i],gap+(i%columns)*(button_width+gap),gap+(i/columns)*dpi_scale(33),button_width,dpi_scale(28),TRUE);
    int bottom=min(dpi_scale(172),height/2),top=gap+rows*dpi_scale(33),list_height=max(1,height-top-bottom-dpi_scale(40));MoveWindow(v->list,gap,top,max(1,width-2*gap),list_height,TRUE);
    ListView_SetColumnWidth(v->list,0,max(dpi_scale(80),width-dpi_scale(190)));ListView_SetColumnWidth(v->list,1,dpi_scale(110));ListView_SetColumnWidth(v->list,2,dpi_scale(60));
    int y=top+list_height+gap;MoveWindow(v->input,gap,y,max(1,width-dpi_scale(72)),dpi_scale(28),TRUE);MoveWindow(v->buttons[6],max(gap,width-dpi_scale(62)),y,dpi_scale(56),dpi_scale(28),TRUE);
    y+=dpi_scale(33);MoveWindow(v->output,gap,y,max(1,width-2*gap),max(1,height-y-dpi_scale(36)),TRUE);MoveWindow(v->status,gap,height-dpi_scale(32),max(1,width-2*gap),dpi_scale(30),TRUE);
}
static LRESULT CALLBACK ssh_list_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;if(msg==WM_NOTIFY&&((NMHDR*)lp)->code==NM_CUSTOMDRAW)return SendMessageW(GetParent(h),WM_NOTIFY,wp,lp);return DefSubclassProc(h,msg,wp,lp);
}
static LRESULT CALLBACK remote_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    RemoteView *v=view_state(h);if(msg==WM_NCCREATE){v=((CREATESTRUCTW*)lp)->lpCreateParams;v->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)v);}if(!v)return DefWindowProcW(h,msg,wp,lp);
    switch(msg){
    case WM_DRAWITEM:return draw_ssh_button((DRAWITEMSTRUCT*)lp);
    case WM_CREATE:{
        v->brush=CreateSolidBrush(RGB(22,29,42));const int ids[]={V_CONNECT,V_DISCONNECT,V_UPLOAD,V_DOWNLOAD,V_CANCEL,SSH_REFRESH,V_EXECUTE};const wchar_t *names[]={nova_text(L"连接",L"Connect"),nova_text(L"断开",L"Disconnect"),nova_text(L"上传",L"Upload"),nova_text(L"下载",L"Download"),nova_text(L"取消",L"Cancel"),nova_text(L"刷新",L"Refresh"),nova_text(L"执行",L"Run")};
        for(int i=0;i<7;i++)v->buttons[i]=control(h,L"BUTTON",names[i],WS_TABSTOP,ids[i],0,0,1,1,v->font);
        v->list=control(h,WC_LISTVIEWW,nova_text(L"远程文件列表",L"Remote file list"),WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,V_LIST,0,0,1,1,v->font);ListView_SetExtendedListViewStyle(v->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(v->list,RGB(22,29,42));ListView_SetTextBkColor(v->list,RGB(22,29,42));ListView_SetTextColor(v->list,RGB(236,241,249));
        SetWindowSubclass(v->list,ssh_list_proc,1,0);
        const wchar_t *columns[]={nova_text(L"名称",L"Name"),nova_text(L"字节 / 类型",L"Bytes / type"),nova_text(L"权限",L"Mode")};for(int i=0;i<3;i++){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.cx=100;col.pszText=(LPWSTR)columns[i];ListView_InsertColumn(v->list,i,&col);}
        v->input=control(h,L"EDIT",L"pwd",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,V_INPUT,0,0,1,1,v->font);SendMessageW(v->input,EM_SETLIMITTEXT,SSH_PATH_CAP-1,0);SendMessageW(v->input,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"在当前服务器目录执行命令",L"Run a command in the current server directory"));
        v->output=control(h,L"EDIT",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,V_OUTPUT,0,0,1,1,v->font);SendMessageW(v->output,EM_SETLIMITTEXT,SSH_OUTPUT_CAP+4096,0);SetWindowTextW(v->output,nova_text(L"命令输出显示在此处。执行目标始终是此标签的服务器和目录。",L"Command output appears here. The target is always this tab's server and directory."));
        if(SUCCEEDED(CoCreateInstance(&CLSID_AccPropServices,NULL,CLSCTX_INPROC_SERVER,&IID_IAccPropServices,(void**)&v->accessibility))){IAccPropServices_SetHwndPropStr(v->accessibility,v->input,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,nova_text(L"当前远程目录中的命令",L"Command in the current remote directory"));IAccPropServices_SetHwndPropStr(v->accessibility,v->output,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,nova_text(L"远程命令输出与执行目标",L"Remote command output and execution target"));}
        v->status=control(h,L"STATIC",nova_text(L"尚未连接。恢复标签不会自动连接或执行命令。",L"Disconnected. Restoring tabs never connects or runs commands automatically."),0,V_STATUS,0,0,1,1,v->font);controls_enabled(v);layout_view(v);return 0;}
    case WM_SIZE:layout_view(v);return 0;
    case DONE:finish(v,wp!=0);return 0;
    case PROGRESS:if(v->busy){wchar_t text[160];swprintf(text,160,nova_text(L"正在传输：当前文件 %u%%（同名目标不会覆盖）",L"Transferring: current file %u%% (existing destinations are not overwritten)"),(unsigned)wp);show_status(v,text);}return 0;
    case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED)switch(LOWORD(wp)){
        case V_CONNECT:{SshLogin *login=calloc(1,sizeof(*login));if(login){if(ssh_choose_server(h,v->server_id,login))ssh_view_connect(h,login);SecureZeroMemory(login,sizeof(*login));free(login);}return 0;}
        case V_DISCONNECT:stop_worker(v);show_status(v,nova_text(L"已断开。远端运行中的命令可能继续执行。",L"Disconnected. Commands already running remotely may continue."));return 0;
        case V_UPLOAD:transfer_dialog(v,TRUE);return 0;case V_DOWNLOAD:transfer_dialog(v,FALSE);return 0;
        case V_CANCEL:SetEvent(v->cancel);show_status(v,nova_text(L"正在取消等待；请随后刷新确认远端状态。",L"Cancelling waiting; refresh afterwards to check remote state."));return 0;
        case SSH_REFRESH:refresh_remote(v);return 0;
        case V_EXECUTE:{wchar_t text[SSH_PATH_CAP];GetWindowTextW(v->input,text,SSH_PATH_CAP);ssh_view_run(h,text);return 0;}
        }break;
    case WM_NOTIFY:
        if(((NMHDR*)lp)->hwndFrom==ListView_GetHeader(v->list)&&((NMHDR*)lp)->code==NM_CUSTOMDRAW){NMCUSTOMDRAW *draw=(NMCUSTOMDRAW*)lp;if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->dwDrawStage==CDDS_ITEMPREPAINT){FillRect(draw->hdc,&draw->rc,v->brush);RECT area=draw->rc;area.left+=dpi_scale(6);SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,RGB(236,241,249));HFONT old=SelectObject(draw->hdc,v->font);const wchar_t *label=draw->dwItemSpec==0?nova_text(L"名称",L"Name"):draw->dwItemSpec==1?nova_text(L"字节 / 类型",L"Bytes / type"):nova_text(L"权限",L"Mode");DrawTextW(draw->hdc,label,-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(draw->hdc,old);return CDRF_SKIPDEFAULT;}}
        if(((NMHDR*)lp)->hwndFrom==v->list){
        if(((NMHDR*)lp)->code==NM_DBLCLK||((NMHDR*)lp)->code==LVN_ITEMACTIVATE){int index=selected(v);if(index>=0&&(v->entries[index].directory||v->entries[index].link)){wchar_t path[SSH_PATH_CAP];if(ssh_path_join(path,SSH_PATH_CAP,v->path,v->entries[index].name))ssh_view_navigate(h,path);}else if(index>=0)transfer_dialog(v,FALSE);return 0;}
        if(((NMHDR*)lp)->code==NM_RCLICK){HMENU menu=CreatePopupMenu();const int ids[]={SSH_COPY,SSH_CUT,SSH_PASTE,SSH_RENAME,SSH_DELETE,SSH_NEWFOLDER};const wchar_t *names[]={nova_text(L"复制",L"Copy"),nova_text(L"剪切",L"Cut"),nova_text(L"粘贴",L"Paste"),nova_text(L"重命名…",L"Rename…"),nova_text(L"永久删除…",L"Permanently delete…"),nova_text(L"新建文件夹…",L"New folder…")};for(int i=0;i<6;i++)AppendMenuW(menu,MF_STRING,ids[i],names[i]);POINT p;GetCursorPos(&p);int action=(int)TrackPopupMenu(menu,TPM_RETURNCMD,p.x,p.y,0,h,NULL);DestroyMenu(menu);if(action)ssh_view_action(h,action);return 0;}
        }break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,RGB(236,241,249));SetBkColor((HDC)wp,RGB(22,29,42));return (LRESULT)v->brush;
    case WM_ERASEBKGND:{RECT r;GetClientRect(h,&r);FillRect((HDC)wp,&r,v->brush);return 1;}
    case WM_DESTROY:stop_worker(v);free(v->entries);if(v->accessibility){MSAAPROPID name=PROPID_ACC_NAME;IAccPropServices_ClearHwndProps(v->accessibility,v->input,OBJID_CLIENT,CHILDID_SELF,&name,1);IAccPropServices_ClearHwndProps(v->accessibility,v->output,OBJID_CLIENT,CHILDID_SELF,&name,1);IAccPropServices_Release(v->accessibility);}CloseHandle(v->event);CloseHandle(v->cancel);DeleteObject(v->brush);if(v->runtime)ssh_runtime_release();SetWindowLongPtrW(h,GWLP_USERDATA,0);SecureZeroMemory(v,sizeof(*v));free(v);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
HWND ssh_view_create(HWND parent,HFONT font,const wchar_t *location){
    int64_t id;const wchar_t *path;if(!ssh_location_parse(location,&id,&path)||wcslen(path)>=SSH_PATH_CAP-40)return NULL;
    RemoteView *v=calloc(1,sizeof(*v));if(!v)return NULL;v->server_id=id;v->font=font;lstrcpyW(v->path,path);v->runtime=ssh_runtime_acquire();v->event=CreateEventW(NULL,FALSE,FALSE,NULL);v->cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    SshServers *servers=calloc(1,sizeof(*servers));if(servers){if(store_load_ssh_servers(servers))for(int i=0;i<servers->count;i++)if(servers->items[i].id==id){v->login.server=servers->items[i];break;}free(servers);}
    if(!v->runtime||!v->event||!v->cancel){if(v->runtime)ssh_runtime_release();if(v->event)CloseHandle(v->event);if(v->cancel)CloseHandle(v->cancel);free(v);return NULL;}
    WNDCLASSW wc={0};wc.lpfnWndProc=remote_proc;wc.hInstance=GetModuleHandleW(NULL);wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.lpszClassName=VIEW_CLASS;RegisterClassW(&wc);
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,VIEW_CLASS,nova_text(L"SSH 远程文件视图",L"SSH remote file view"),WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,100,100,parent,NULL,wc.hInstance,v);
    if(!window){CloseHandle(v->event);CloseHandle(v->cancel);ssh_runtime_release();free(v);}return window;
}
void ssh_view_focus(HWND view){RemoteView *v=view_state(view);if(v)SetFocus(v->list);}
BOOL ssh_view_message(HWND window,MSG *message){
    RemoteView *v=view_state(window);if(!v||message->message!=WM_KEYDOWN)return FALSE;
    if(message->hwnd==v->input&&message->wParam==VK_RETURN){SendMessageW(window,WM_COMMAND,V_EXECUTE,0);return TRUE;}
    if(message->hwnd!=v->list)return FALSE;
    BOOL ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;int action=0;
    if(ctrl&&message->wParam=='C')action=SSH_COPY;else if(ctrl&&message->wParam=='X')action=SSH_CUT;else if(ctrl&&message->wParam=='V')action=SSH_PASTE;else if(message->wParam==VK_F2)action=SSH_RENAME;else if(message->wParam==VK_DELETE)action=SSH_DELETE;else if(ctrl&&(GetKeyState(VK_SHIFT)&0x8000)&&message->wParam=='N')action=SSH_NEWFOLDER;
    if(action){ssh_view_action(window,action);return TRUE;}return FALSE;
}
