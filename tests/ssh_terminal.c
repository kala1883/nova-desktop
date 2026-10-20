/* Real HWND/worker coverage against ssh_fixture.py; no user configuration,
   registry writes, saved credentials or non-loopback connections. */
#include "../src/ui/ssh_view.c"
#include "../src/ui/terminal_view.c"
#include <assert.h>
#include "ssh_saved_login.h"
BOOL nova_english=FALSE;
static HWND preview_view;
static LRESULT CALLBACK fixture_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_SIZE&&preview_view)MoveWindow(preview_view,0,0,LOWORD(lp),HIWORD(lp),TRUE);
    if(message==WM_DESTROY){preview_view=NULL;PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
static void pump(HWND window,unsigned milliseconds){
    ULONGLONG until=GetTickCount64()+milliseconds;
    do{MSG message;while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){if(message.message!=WM_QUIT&&!ssh_view_message(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}Sleep(2);}while(GetTickCount64()<until);
}
static BOOL wait_idle(RemoteView *v){ULONGLONG until=GetTickCount64()+7000;while(v->busy&&GetTickCount64()<until)pump(v->window,10);return !v->busy;}
static BOOL wait_text(RemoteView *v,const char *expected){
    ULONGLONG until=GetTickCount64()+7000;TerminalView *t=terminal_state(v->terminal);
    while(GetTickCount64()<until){pump(v->window,10);char text[65536];size_t length=vterm_screen_get_text(t->screen,text,sizeof(text)-1,(VTermRect){0,t->rows,0,t->columns});text[length]=0;if(strstr(text,expected))return TRUE;}return FALSE;
}
int wmain(int argc,wchar_t **argv){
    assert(argc>=5&&SUCCEEDED(OleInitialize(NULL)));BOOL preview=argc==6&&!wcsncmp(argv[5],L"--preview",9);nova_english=preview&&wcsstr(argv[5],L"-en")!=NULL;
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    test_saved_login_dialog(argv[2]);
    WNDCLASSW wc={0};wc.lpfnWndProc=fixture_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"NovaTerminalFixture";wc.hCursor=LoadCursorW(NULL,IDC_ARROW);RegisterClassW(&wc);
    HWND parent=CreateWindowExW(0,wc.lpszClassName,L"NOVA SSH terminal · isolated loopback preview",WS_OVERLAPPEDWINDOW,60,60,1000,720,NULL,NULL,wc.hInstance,NULL);assert(parent);
    HFONT font=CreateFontW(-dpi_scale(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");assert(font);
    HWND window=ssh_view_create(parent,font,L"nova-ssh:1:/");assert(window);preview_view=window;RemoteView *v=view_state(window);
    RECT area;GetClientRect(parent,&area);MoveWindow(window,0,0,area.right,area.bottom,TRUE);ShowWindow(window,SW_SHOW);
    assert(!v->thread&&!v->connected);ssh_view_action(window,SSH_COMMAND);assert(v->terminal_mode&&!v->thread&&!v->connected);set_view_mode(v,FALSE);
    SshLogin login={0};login.server.id=1;login.server.port=_wtoi(argv[1]);lstrcpyW(login.server.host,L"127.0.0.1");lstrcpyW(login.server.user,L"fixture");lstrcpyW(login.server.name,L"Fixture");lstrcpyW(login.secret,L"fixture-password");
    SshResult result={0};HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);assert(cancel);assert(!ssh_client_connect(&login,cancel,&result)&&result.code==2);strcpy(login.fingerprint,result.fingerprint);ssh_result_dispose(&result);CloseHandle(cancel);
    v->login=login;lstrcpyW(v->request_path,v->path);assert(submit(v,J_CONNECT));assert(wait_idle(v)&&v->connected&&!v->terminal_running&&!v->terminal_opened);
    assert(ssh_view_navigate(window,L"/missing-fixture-directory")&&wait_idle(v)&&v->connected&&!v->terminal_mode);assert(wait_text(v,nova_english?"Failed stage:":"失败阶段："));
    assert(ssh_view_run(window,L"cd /persistent")&&v->terminal_mode&&v->busy&&v->job==J_TERMINAL);assert(wait_idle(v)&&v->terminal_running&&v->terminal_opened&&!v->pending_command[0]);assert(wait_text(v,"fixture terminal"));pump(window,150);
    assert(IsWindowVisible(v->terminal)==IsWindowVisible(parent)&&!(GetWindowLongPtrW(v->list,GWL_STYLE)&WS_VISIBLE));HANDLE worker=v->thread;
    set_view_mode(v,FALSE);assert(wait_idle(v)&&v->connected&&v->terminal_running&&v->thread==worker);assert(GetWindowLongPtrW(v->list,GWL_STYLE)&WS_VISIBLE);
    terminal_view_reset(v->terminal);assert(ssh_view_run(window,L"pwd")&&v->terminal_mode&&v->thread==worker&&!v->busy&&wait_text(v,"/persistent"));
    MoveWindow(window,0,0,460,480,TRUE);pump(window,100);int columns,rows;terminal_view_size(v->terminal,&columns,&rows);char size[80];snprintf(size,sizeof(size),"%d %d",columns,rows);assert(ssh_view_run(window,L"size")&&wait_text(v,size));
    /* Keyboard dispatch must reach the terminal before file-manager shortcuts. */
    BYTE keys[256]={0},old_keys[256];GetKeyboardState(old_keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);MSG message={v->terminal,WM_KEYDOWN,'C',0,0,{0,0}};assert(ssh_view_message(window,&message));SetKeyboardState(old_keys);assert(wait_text(v,"^C"));
    terminal_view_reset(v->terminal);assert(ssh_view_run(window,L"utf8")&&wait_text(v,"\xe4\xb8\xad\xe6\x96\x87"));
    EnterCriticalSection(&v->terminal_lock);v->input_length=sizeof(v->terminal_input);assert(!terminal_send(v,"must-not-partially-enqueue",26));v->input_length=0;LeaveCriticalSection(&v->terminal_lock);
    if(preview){MoveWindow(parent,60,60,wcsstr(argv[5],L"narrow")?400:1000,720,TRUE);if(wcsstr(argv[5],L"files"))set_view_mode(v,FALSE);ShowWindow(parent,SW_SHOW);UpdateWindow(parent);terminal_view_reset(v->terminal);terminal_view_feed(v->terminal,"\x1b[36mfixture@loopback\x1b[0m:~$ pwd\r\n/persistent\r\n\x1b[36mfixture@loopback\x1b[0m:~$ ",strlen("\x1b[36mfixture@loopback\x1b[0m:~$ pwd\r\n/persistent\r\n\x1b[36mfixture@loopback\x1b[0m:~$ "));
        ULONGLONG until=GetTickCount64()+60000;while(IsWindow(parent)&&GetTickCount64()<until)pump(window,20);
    }else{assert(ssh_view_run(window,L"exit"));ULONGLONG until=GetTickCount64()+7000;while(v->connected&&GetTickCount64()<until)pump(window,10);assert(!v->connected&&!v->terminal_running&&IsWindowEnabled(v->buttons[0]));
        stop_worker(v);v->login=login;v->terminal_opened=FALSE;lstrcpyW(v->request_path,v->path);assert(submit(v,J_CONNECT)&&wait_idle(v)&&v->terminal_running);assert(ssh_view_run(window,L"floodterm"));Sleep(300);
        EnterCriticalSection(&v->terminal_lock);assert(v->output_length>0&&v->output_length<=sizeof(v->terminal_output));LeaveCriticalSection(&v->terminal_lock);
    }
    ULONGLONG start=GetTickCount64();if(IsWindow(parent))DestroyWindow(parent);assert(GetTickCount64()-start<3000&&worker_count==0);DeleteObject(font);SecureZeroMemory(&login,sizeof(login));OleUninitialize();
    puts("PASS SSH terminal HWND/worker: disconnected mode, auto-open after connect, view switching retains shell, SFTP coexistence, resize, keyboard routing, UTF-8, queue limit, EOF and joined cleanup");return 0;
}
