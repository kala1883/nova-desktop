int nova_english=0;
#include "../src/ui/file_manager.c"
#include <assert.h>

static void pump(unsigned milliseconds){
    ULONGLONG until=GetTickCount64()+milliseconds;
    do{MSG msg;while(GetTickCount64()<until&&PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(!file_manager_message(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}Sleep(10);}while(GetTickCount64()<until);
}
static BOOL wait_location(Tab *t,const wchar_t *path){
    for(int i=0;i<100;i++){pump(50);if(!t->pending&&!lstrcmpiW(t->location,path))return TRUE;}return FALSE;
}
static BOOL CALLBACK find_tree(HWND h,LPARAM data){wchar_t cls[64];GetClassNameW(h,cls,64);if(!lstrcmpW(cls,L"SysTreeView32"))*(BOOL*)data=TRUE;return TRUE;}
static int live_views(void){int n=0;for(int i=0;i<4;i++)for(int j=0;j<fm.panes[i].count;j++)if(fm.panes[i].items[j]->browser)n++;return n;}
int wmain(void){
    setvbuf(stdout,NULL,_IONBF,0);
    assert(SUCCEEDED(OleInitialize(NULL)));
    IDataObject *old_clipboard=NULL;OleGetClipboard(&old_clipboard);
    wchar_t temp[MAX_PATH],root[MAX_PATH],a[MAX_PATH],b[MAX_PATH],file[MAX_PATH],copy[MAX_PATH];
    GetTempPathW(MAX_PATH,temp);swprintf(root,MAX_PATH,L"%lsNovaFiles-%lu",temp,GetCurrentProcessId());assert(CreateDirectoryW(root,NULL));
    swprintf(a,MAX_PATH,L"%ls\\中文源目录",root);swprintf(b,MAX_PATH,L"%ls\\目标目录",root);assert(CreateDirectoryW(a,NULL));assert(CreateDirectoryW(b,NULL));
    swprintf(file,MAX_PATH,L"%ls\\测试.txt",a);swprintf(copy,MAX_PATH,L"%ls\\测试.txt",b);
    HANDLE f=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(f!=INVALID_HANDLE_VALUE);DWORD bytes;assert(WriteFile(f,"nova",4,&bytes,NULL));CloseHandle(f);
    wchar_t initial[MAX_PATH];swprintf(initial,MAX_PATH,L"%ls\\file-manager.ini",root);
    for(int i=0;i<4;i++){wchar_t section[32];swprintf(section,32,L"Pane%d",i);assert(WritePrivateProfileStringW(section,L"Tab0",root,initial));}
    HWND host=CreateWindowExW(0,L"STATIC",L"NOVA embedded file test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1200,820,NULL,NULL,GetModuleHandleW(NULL),NULL);assert(host);
    puts("Creating four native views...");assert(file_manager_open(host,root));pump(600);puts("Views created.");
    assert(GetParent(fm.window)==host);assert(GetAncestor(fm.window,GA_ROOT)==host);assert((GetWindowLongPtrW(fm.window,GWL_STYLE)&WS_CHILD)!=0);
    assert(IsWindow(fm.tooltip)&&fm.tooltips_added==42&&SendMessageW(fm.tooltip,TTM_GETTOOLCOUNT,0,0)==42);
    wchar_t icon_face[64]=L"";HDC icon_dc=GetDC(fm.window);HFONT old_icon_font=SelectObject(icon_dc,fm.icon_font);GetTextFaceW(icon_dc,64,icon_face);SelectObject(icon_dc,old_icon_font);ReleaseDC(fm.window,icon_dc);assert(!lstrcmpW(icon_face,L"Segoe MDL2 Assets"));
    assert(button_glyph(C_BACK)[0]==0xE72B&&button_glyph(C_FORWARD)[0]==0xE72A&&button_glyph(C_REFRESH)[0]==0xE72C&&button_glyph(C_UP)[0]==0xE74A);
    SendMessageW(fm.toolbar[0],WM_MOUSEMOVE,0,0);assert(fm.hot_button==fm.toolbar[0]);SendMessageW(fm.toolbar[0],WM_MOUSELEAVE,0,0);assert(!fm.hot_button);
    for(int i=0;i<10;i++){RECT button_rect;wchar_t accessible_name[40];GetWindowRect(fm.toolbar[i],&button_rect);GetWindowTextW(fm.toolbar[i],accessible_name,40);assert(button_rect.right-button_rect.left==scale(34)&&accessible_name[0]);}
    puts("PASS compact icon toolbar retains accessible names and 42 tooltips");
    HWND embedded=fm.window;file_manager_hide();assert(!IsWindowVisible(embedded));assert(file_manager_open(host,root)==embedded);assert(IsWindowVisible(embedded));
    for(int i=0;i<4;i++)assert(fm.panes[i].count==1&&current(&fm.panes[i])->browser);
    Pane *p=&fm.panes[0];Tab *t=current(p);
    fm.test_mode=TRUE;SetWindowTextW(p->address,L"echo nova");open_address(p);assert(fm.command_runs==1&&!lstrcmpW(fm.last_command,L"echo nova")&&!lstrcmpiW(fm.last_command_directory,root));
    SetWindowTextW(p->address,L"> C:\\Tools\\demo.exe --check");open_address(p);assert(fm.command_runs==2&&!lstrcmpW(fm.last_command,L"C:\\Tools\\demo.exe --check"));fm.test_mode=FALSE;
    SetWindowTextW(p->address,L"中文源目录");open_address(p);assert(wait_location(t,a));assert(lstrcmpiW(current(&fm.panes[1])->location,a));
    puts("PASS each pane switches relative directories and dispatches commands with its own working directory");
    assert(SUCCEEDED(navigate(t,a)));assert(wait_location(t,a));
    pump(700);
    assert(SUCCEEDED(navigate(t,b)));assert(wait_location(t,b));
    command(p,C_BACK);assert(wait_location(t,a));command(p,C_FORWARD);assert(wait_location(t,b));
    assert(add_tab(p,a));assert(wait_location(current(p),a));assert(p->count==2);select_tab(p,0);assert(current(p)==t);assert(!lstrcmpiW(t->location,b));
    select_tab(p,1);close_tab(p);assert(p->count==1&&current(p)==t);
    assert(SUCCEEDED(navigate(t,a)));assert(wait_location(t,a));
    PIDLIST_ABSOLUTE id=NULL;assert(SUCCEEDED(SHParseDisplayName(file,NULL,&id,0,NULL)));
    IShellView *view=NULL;assert(SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view)));
    assert(SUCCEEDED(IShellView_SelectItem(view,ILFindLastID(id),SVSI_SELECT|SVSI_FOCUSED|SVSI_DESELECTOTHERS)));IShellView_Release(view);CoTaskMemFree(id);
    pump(500);command(p,C_COPY);
    Pane *dest=&fm.panes[1];assert(SUCCEEDED(navigate(current(dest),b)));assert(wait_location(current(dest),b));pump(500);command(dest,C_PASTE);
    for(int i=0;i<100&&GetFileAttributesW(copy)==INVALID_FILE_ATTRIBUTES;i++)pump(50);
    assert(GetFileAttributesW(copy)!=INVALID_FILE_ATTRIBUTES);assert(GetFileAttributesW(file)!=INVALID_FILE_ATTRIBUTES);
    f=CreateFileW(copy,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(f!=INVALID_HANDLE_VALUE);char data[5]={0};assert(ReadFile(f,data,4,&bytes,NULL)&&bytes==4&&!strcmp(data,"nova"));CloseHandle(f);
    puts("PASS native shell creation, Unicode navigation, per-tab history, tab isolation, cross-pane copy/paste and content integrity");
    activate(dest);pump(200);assert(fm.active==1);
    assert(DeleteFileW(copy));refresh(current(dest));pump(300);command(p,C_CUT);command(dest,C_PASTE);
    for(int i=0;i<100&&(GetFileAttributesW(file)!=INVALID_FILE_ATTRIBUTES||GetFileAttributesW(copy)==INVALID_FILE_ATTRIBUTES);i++)pump(50);
    assert(GetFileAttributesW(file)==INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(copy)!=INVALID_FILE_ATTRIBUTES);
    command(p,C_NEWFOLDER);pump(700);
    wchar_t pattern[MAX_PATH],created[MAX_PATH]=L"";swprintf(pattern,MAX_PATH,L"%ls\\*",a);WIN32_FIND_DATAW entry;HANDLE find=FindFirstFileW(pattern,&entry);
    if(find!=INVALID_HANDLE_VALUE){do{if((entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&entry.cFileName[0]!=L'.')swprintf(created,MAX_PATH,L"%ls\\%ls",a,entry.cFileName);}while(FindNextFileW(find,&entry));FindClose(find);}
    assert(created[0]);puts("PASS active-pane stability, native cut/move and new-folder command");
    for(int layout=0;layout<12;layout++){
        RECT boxes[4];int n=pane_rects(layout,1000,800,boxes),area=0;
        for(int i=0;i<n;i++){
            assert(boxes[i].left>=0&&boxes[i].top>=0&&boxes[i].right<=1000&&boxes[i].bottom<=800);
            area+=(boxes[i].right-boxes[i].left)*(boxes[i].bottom-boxes[i].top);
            for(int j=0;j<i;j++){RECT overlap;assert(!IntersectRect(&overlap,&boxes[i],&boxes[j]));}
        }
        assert(area==800000);fm.layout=layout;arrange();
    }
    fm.layout=4;fm.navigation_tree=TRUE;fm.favorite_count=1;lstrcpyW(fm.favorites[0],a);save_session();file_manager_close();pump(100);
    assert(file_manager_open(host,root));pump(700);assert(fm.layout==4&&fm.navigation_tree&&fm.favorite_count==1&&!lstrcmpW(fm.favorites[0],a));assert(wait_location(current(&fm.panes[0]),a));
    BOOL has_tree=FALSE;EnumChildWindows(fm.window,find_tree,(LPARAM)&has_tree);assert(has_tree);
    file_manager_close();pump(100);
    puts("PASS 12 non-overlapping layouts, session/favorite persistence and reopen lifecycle");
    assert(store_begin());assert(store_set_int(L"files",L"Manager",L"Layout",3));assert(store_set_int(L"files",L"Manager",L"NavigationTree",0));
    for(int i=0;i<4;i++){
        wchar_t section[32],key[32];swprintf(section,32,L"Pane%d",i);assert(store_set_int(L"files",section,L"Count",TAB_LIMIT));assert(store_set_int(L"files",section,L"Selected",0));
        for(int j=0;j<TAB_LIMIT;j++){swprintf(key,32,L"Tab%d",j);assert(store_set(L"files",section,key,root));}
    }
    assert(store_end(TRUE));assert(file_manager_open(host,root));pump(400);
    assert(live_views()==1);for(int i=0;i<4;i++)assert(fm.panes[i].count==TAB_LIMIT);
    fm.layout=0;arrange();pump(400);assert(live_views()==4);
    p=&fm.panes[0];t=current(p);assert(wait_location(t,root));assert(SUCCEEDED(navigate(t,a)));assert(wait_location(t,a));
    select_tab(p,1);pump(200);assert(live_views()==5);evict_views(GetTickCount64()+VIEW_IDLE_MS+1);assert(!t->browser&&live_views()==4);
    select_tab(p,0);assert(wait_location(t,a));command(p,C_BACK);assert(wait_location(t,root));command(p,C_FORWARD);assert(wait_location(t,a));
    file_manager_hide();evict_views(GetTickCount64()+VIEW_IDLE_MS+1);assert(live_views()==0);
    assert(file_manager_open(host,root));pump(300);assert(live_views()==4&&wait_location(t,a));
    /* State writes are coalesced, but closing flushes without waiting for a timer. */
    fm.layout=3;save_session();assert(session_int(L"Manager",L"Layout",-1)==0);file_manager_close();assert(session_int(L"Manager",L"Layout",-1)==3);
    puts("PASS 48 restored tabs create only 1/4 visible views; idle eviction to zero; history restored; debounced save flushed on close");
    OleSetClipboard(old_clipboard);if(old_clipboard){OleFlushClipboard();IDataObject_Release(old_clipboard);}
    DeleteFileW(copy);DeleteFileW(file);RemoveDirectoryW(created);RemoveDirectoryW(a);RemoveDirectoryW(b);
    store_close();wchar_t settings[MAX_PATH];swprintf(settings,MAX_PATH,L"%ls\\file-manager.ini",root);DeleteFileW(settings);swprintf(settings,MAX_PATH,L"%ls\\nova.sqlite",root);DeleteFileW(settings);assert(RemoveDirectoryW(root));DestroyWindow(host);OleUninitialize();return 0;
}
