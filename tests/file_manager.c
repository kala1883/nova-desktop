int nova_english=0;
#include "../src/ui/file_manager.c"
#include <assert.h>

static void pump(unsigned milliseconds){
    ULONGLONG until=GetTickCount64()+milliseconds;
    do{MSG msg;while(GetTickCount64()<until&&PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(!file_manager_message(&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}Sleep(10);}while(GetTickCount64()<until);
}
static BOOL same_path(const wchar_t *a,const wchar_t *b){
    wchar_t long_a[MAX_PATH],long_b[MAX_PATH];
    if(!GetLongPathNameW(a,long_a,MAX_PATH)||!GetLongPathNameW(b,long_b,MAX_PATH))return !lstrcmpiW(a,b);
    return !lstrcmpiW(long_a,long_b);
}
static BOOL wait_location(Tab *t,const wchar_t *path){
    for(int i=0;i<100;i++){pump(50);if(!t->pending&&same_path(t->location,path))return TRUE;}return FALSE;
}
static BOOL CALLBACK find_tree(HWND h,LPARAM data){wchar_t cls[64];GetClassNameW(h,cls,64);if(!lstrcmpW(cls,L"SysTreeView32"))*(BOOL*)data=TRUE;return TRUE;}
static int live_views(void){int n=0;for(int i=0;i<4;i++)for(int j=0;j<fm.panes[i].count;j++)if(fm.panes[i].items[j]->browser)n++;return n;}
static void assert_view_fills_host(Tab *t){
    IShellView *view=NULL;HWND window=NULL;RECT host_rect,view_rect;
    assert(SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&view)));
    assert(SUCCEEDED(IShellView_GetWindow(view,&window)));IShellView_Release(view);
    assert(GetClientRect(t->host,&host_rect)&&GetWindowRect(window,&view_rect));
    MapWindowPoints(NULL,t->host,(POINT*)&view_rect,2);
    assert(host_rect.right>100&&host_rect.bottom>100);
    assert(EqualRect(&host_rect,&view_rect));
}
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
    assert(IsWindow(fm.tooltip)&&fm.tooltips_added==66&&SendMessageW(fm.tooltip,TTM_GETTOOLCOUNT,0,0)==66);
    wchar_t icon_face[64]=L"";HDC icon_dc=GetDC(fm.window);HFONT old_icon_font=SelectObject(icon_dc,fm.icon_font);GetTextFaceW(icon_dc,64,icon_face);SelectObject(icon_dc,old_icon_font);ReleaseDC(fm.window,icon_dc);assert(!lstrcmpW(icon_face,L"Segoe MDL2 Assets"));
    assert(button_glyph(C_BACK)[0]==0xE72B&&button_glyph(C_FORWARD)[0]==0xE72A&&button_glyph(C_REFRESH)[0]==0xE72C&&button_glyph(C_UP)[0]==0xE74A&&button_glyph(C_COMMAND)[0]==0xE756);
    SendMessageW(fm.toolbar[0],WM_MOUSEMOVE,0,0);assert(fm.hot_button==fm.toolbar[0]);SendMessageW(fm.toolbar[0],WM_MOUSELEAVE,0,0);assert(!fm.hot_button);
    for(int i=0;i<MANAGER_TOOLBAR_COUNT;i++){RECT button_rect;wchar_t accessible_name[40];GetWindowRect(fm.toolbar[i],&button_rect);GetWindowTextW(fm.toolbar[i],accessible_name,40);assert(button_rect.right-button_rect.left==scale(34)&&accessible_name[0]);}
    for(int i=0;i<4;i++){
        Pane *pane=&fm.panes[i];HWND actions[]={pane->cut,pane->paste,pane->delete_file,pane->new_folder};RECT back_rect;GetWindowRect(pane->back,&back_rect);
        for(int j=0;j<4;j++){RECT action_rect;wchar_t accessible_name[40];assert(GetParent(actions[j])==pane->window);GetWindowRect(actions[j],&action_rect);GetWindowTextW(actions[j],accessible_name,40);assert(action_rect.top==back_rect.top&&action_rect.right-action_rect.left==scale(32)&&accessible_name[0]);}
        RECT command_rect,new_folder_rect;wchar_t command_name[80];GetWindowRect(pane->command_button,&command_rect);GetWindowRect(pane->new_folder,&new_folder_rect);GetWindowTextW(pane->command_button,command_name,80);
        assert(IsWindowVisible(pane->command_button)&&command_rect.left>new_folder_rect.right&&command_rect.right-command_rect.left==scale(48)&&command_name[0]);
    }
    puts("PASS global and per-pane icon toolbars retain accessible names and 66 tooltips");
    HWND embedded=fm.window;file_manager_hide();assert(!IsWindowVisible(embedded));assert(file_manager_open(host,root)==embedded);assert(IsWindowVisible(embedded));
    for(int i=0;i<4;i++)assert(fm.panes[i].count==1&&current(&fm.panes[i])->browser);
    for(int i=0;i<4;i++)assert((GetWindowLongPtrW(fm.panes[i].tabs,GWL_STYLE)&TCS_OWNERDRAWFIXED)!=0);
    Pane *narrow=&fm.panes[0];MoveWindow(narrow->window,0,0,scale(128),scale(300),TRUE);pane_layout(narrow);RECT narrow_client;GetClientRect(narrow->window,&narrow_client);assert(IsWindowVisible(narrow->navigation_menu)&&!IsWindowVisible(narrow->back));
    HWND compact[]={narrow->navigation_menu,narrow->cut,narrow->paste,narrow->delete_file,narrow->new_folder};LONG previous_right=0;for(int i=0;i<5;i++){RECT item;GetWindowRect(compact[i],&item);MapWindowPoints(NULL,narrow->window,(POINT*)&item,2);assert(item.left>=previous_right&&item.right<=narrow_client.right);previous_right=item.right;}
    fm.layout=0;arrange();RECT manager_rect;GetClientRect(fm.window,&manager_rect);int content_height=manager_rect.bottom-scale(46)-scale(27);
    POINT divider={split_pixel(0,0,manager_rect.right),scale(46)+content_height/2};int divider_index=-1;
    assert(hit_splitter(divider,&divider_index)==1&&divider_index==0);
    int original_split=fm.splits[0][0];SendMessageW(fm.window,WM_LBUTTONDOWN,0,MAKELPARAM(divider.x,divider.y));assert(fm.splitter_dragging&&GetCapture()==fm.window);
    SendMessageW(fm.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(divider.x+scale(80),divider.y));SendMessageW(fm.window,WM_LBUTTONUP,0,MAKELPARAM(divider.x+scale(80),divider.y));
    assert(!fm.splitter_dragging&&fm.splits[0][0]>original_split);int resized_split=fm.splits[0][0];RECT resized[4];pane_rects(0,manager_rect.right,content_height,resized);assert(resized[0].right>manager_rect.right/2);
    pump(500);assert(session_int(L"Manager",L"Split0_0",-1)==resized_split);
    divider.x=split_pixel(0,0,manager_rect.right);SendMessageW(fm.window,WM_LBUTTONDOWN,0,MAKELPARAM(divider.x,divider.y));assert(fm.splitter_dragging);
    SendMessageW(fm.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(divider.x-scale(40),divider.y));SendMessageW(fm.window,WM_CANCELMODE,0,0);assert(!fm.splitter_dragging&&fm.splits[0][0]==resized_split);
    puts("PASS pane dividers resize, cancel safely, debounce to SQLite, and directory tabs use a clear owner-drawn selection state");
    Pane *p=&fm.panes[0];Tab *t=current(p);
    assert(wait_location(t,root));assert_view_fills_host(t);
    /* Reproduce the stale 100x100 browser rectangle without resizing the pane. */
    RECT stale={0,0,100,100};assert(SUCCEEDED(IExplorerBrowser_SetRect(t->browser,NULL,stale)));
    command(p,C_REFRESH);pump(200);assert_view_fills_host(t);
    IShellView *created_view=NULL;
    assert(SUCCEEDED(IExplorerBrowser_GetCurrentView(t->browser,&IID_IShellView,(void**)&created_view)));
    event_created(&t->events,created_view);IShellView_Release(created_view);
    assert(SUCCEEDED(IExplorerBrowser_SetRect(t->browser,NULL,stale)));
    pump(200);assert_view_fills_host(t);
    puts("PASS refresh and deferred view-created layout recover a stale 100x100 Shell view");
    fm.test_mode=TRUE;command(p,C_COMMAND);assert(fm.command_runs==1&&!lstrcmpW(fm.last_command,L"cd .")&&same_path(fm.last_command_directory,root));
    assert(file_command_add(&fm.commands,L"检查状态",L"git status")==1);assert(set_default_command(1));command(p,C_COMMAND);assert(fm.command_runs==2&&!lstrcmpW(fm.last_command,L"git status"));
    open_command_manager();assert(IsWindow(fm.command_window)&&SendMessageW(fm.command_list,LB_GETCOUNT,0,0)==2);DestroyWindow(fm.command_window);
    SetWindowTextW(p->address,L"echo nova");open_address(p);assert(fm.command_runs==3&&!lstrcmpW(fm.last_command,L"echo nova")&&same_path(fm.last_command_directory,root));
    SetWindowTextW(p->address,L"> C:\\Tools\\demo.exe --check");open_address(p);assert(fm.command_runs==4&&!lstrcmpW(fm.last_command,L"C:\\Tools\\demo.exe --check"));fm.test_mode=FALSE;
    SetWindowTextW(p->address,L"中文源目录");open_address(p);assert(wait_location(t,a));assert(lstrcmpiW(current(&fm.panes[1])->location,a));
    puts("PASS command split button, preset editor/default persistence, and address bar dispatch use the active pane working directory");
    assert(SUCCEEDED(navigate(t,a)));assert(wait_location(t,a));
    pump(700);
    assert(SUCCEEDED(navigate(t,b)));assert(wait_location(t,b));
    command(p,C_BACK);assert(wait_location(t,a));command(p,C_FORWARD);assert(wait_location(t,b));
    assert(add_tab(p,a));assert(wait_location(current(p),a));assert(p->count==2);Tab *added=current(p);select_tab(p,0);assert(current(p)==t);assert(same_path(t->location,b));
    RECT first_tab,last_tab;assert(TabCtrl_GetItemRect(p->tabs,0,&first_tab)&&TabCtrl_GetItemRect(p->tabs,1,&last_tab));POINT press={(first_tab.left+first_tab.right)/2,(first_tab.top+first_tab.bottom)/2},drop={last_tab.right-1,(last_tab.top+last_tab.bottom)/2};
    SendMessageW(p->tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(press.x,press.y));assert(p->tab_drag_armed&&p->tab_drag_source==0&&GetCapture()==p->tabs);SendMessageW(p->tabs,WM_TIMER,TAB_DRAG_TIMER_ID,0);assert(p->tab_dragging);
    SendMessageW(p->tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(drop.x,drop.y));SendMessageW(p->tabs,WM_LBUTTONUP,0,MAKELPARAM(drop.x,drop.y));assert(!p->tab_drag_armed&&!p->tab_dragging&&p->items[0]==added&&p->items[1]==t&&current(p)==t);
    pump(500);wchar_t persisted[LOCATION_SIZE];session_get(L"Pane0",L"Tab1",L"",persisted,LOCATION_SIZE);assert(same_path(persisted,t->location));
    SendMessageW(p->tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(drop.x,drop.y));SendMessageW(p->tabs,WM_TIMER,TAB_DRAG_TIMER_ID,0);assert(p->tab_dragging);MSG escape={p->tabs,WM_KEYDOWN,VK_ESCAPE,0,0,{0,0}};assert(file_manager_message(&escape));assert(!p->tab_drag_armed&&!p->tab_dragging&&p->items[1]==t);
    select_tab(p,0);close_tab(p);assert(p->count==1&&current(p)==t);
    puts("PASS press-and-hold tab drag reorders within one pane, persists, and Escape cancels safely");
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
    assert(file_manager_open(host,root));pump(700);assert(fm.layout==4&&fm.navigation_tree&&fm.favorite_count==1&&!lstrcmpW(fm.favorites[0],a));assert(fm.splits[0][0]==resized_split);assert(wait_location(current(&fm.panes[0]),a));
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
    select_tab(p,0);assert(wait_location(t,a));assert_view_fills_host(t);command(p,C_BACK);assert(wait_location(t,root));command(p,C_FORWARD);assert(wait_location(t,a));assert_view_fills_host(t);
    file_manager_hide();evict_views(GetTickCount64()+VIEW_IDLE_MS+1);assert(live_views()==0);
    assert(file_manager_open(host,root));pump(300);assert(live_views()==4&&wait_location(t,a));
    /* State writes are coalesced, but closing flushes without waiting for a timer. */
    fm.layout=3;save_session();assert(session_int(L"Manager",L"Layout",-1)==0);file_manager_close();assert(session_int(L"Manager",L"Layout",-1)==3);
    puts("PASS 48 restored tabs create only 1/4 visible views; idle eviction to zero; history restored; debounced save flushed on close");
    OleSetClipboard(old_clipboard);if(old_clipboard){OleFlushClipboard();IDataObject_Release(old_clipboard);}
    DeleteFileW(copy);DeleteFileW(file);RemoveDirectoryW(created);RemoveDirectoryW(a);RemoveDirectoryW(b);
    store_close();wchar_t settings[MAX_PATH];swprintf(settings,MAX_PATH,L"%ls\\file-manager.ini",root);DeleteFileW(settings);swprintf(settings,MAX_PATH,L"%ls\\nova.sqlite",root);DeleteFileW(settings);assert(RemoveDirectoryW(root));DestroyWindow(host);OleUninitialize();return 0;
}
