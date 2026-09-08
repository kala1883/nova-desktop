#define wWinMain nova_entry
#include "../src/main.c"
#undef wWinMain
#include <assert.h>

int wmain(int argc,wchar_t **argv) {
    wchar_t tempdir[MAX_PATH],dir[MAX_PATH],path[MAX_PATH],command[MAX_PATH+40];
    GetTempPathW(MAX_PATH,tempdir);swprintf(dir,MAX_PATH,L"%lsnova-test-%lu",tempdir,GetCurrentProcessId());
    assert(CreateDirectoryW(dir,NULL));swprintf(config_path,MAX_PATH,L"%ls\\config.ini",dir);
    WritePrivateProfileStringW(L"Fixture",L"Preserve",L"legacy",config_path);
    defaults();lstrcpyW(spaces[0].name,L"中文工作区 演示");active_space=2;prefer_pin=TRUE;
    assert(save_config());ZeroMemory(spaces,sizeof(spaces));active_space=0;prefer_pin=FALSE;load_config();
    assert(lstrcmpW(spaces[0].name,L"目录")==0&&active_space==2&&prefer_pin);
    assert(spaces[0].app_count==0);puts("PASS fixed directory workspace, UTF-16 config and pin preference");
    space_count=5;ZeroMemory(&spaces[4],sizeof(spaces[4]));spaces[4].items_loaded=1;lstrcpyW(spaces[4].name,L"??? 5");
    assert(repair_workspace_name(4)&&lstrcmpW(spaces[4].name,L"工作区 5")==0);assert(save_config());
    ZeroMemory(spaces,sizeof(spaces));load_config();assert(space_count==5&&lstrcmpW(spaces[4].name,L"工作区 5")==0);
    puts("PASS legacy numbered placeholder repaired in SQLite data");
    store_set_int(L"app",L"Nova",L"Active",-5);load_config();assert(active_space==0);
    puts("PASS malformed active workspace bounded");
    assert(store_set_int(L"app",L"Nova",L"Language",1));nova_english=FALSE;load_config();
    assert(nova_english&&!lstrcmpW(nova_text(L"中文",L"English"),L"English"));
    nova_english=FALSE;assert(store_set_int(L"app",L"Nova",L"Language",0));
    puts("PASS persisted Simplified Chinese / English language preference");
    active_space=3;assert(ensure_items(3));spaces[3].app_count=0;
    swprintf(path,MAX_PATH,L"%ls\\启动文件.cmd",dir);
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(f!=INVALID_HANDLE_VALUE);CloseHandle(f);
    assert(insert_path(path)==1);assert(insert_path(path)==0);assert(insert_path(dir)==1);
    assert(insert_path(L"Z:\\NOVA_DOES_NOT_EXIST_1735.exe")==-1);
    assert(spaces[3].app_count==2&&spaces[0].app_count==0);
    spaces[3].app_count=MAX_APPS;assert(insert_path(config_path)==-2);spaces[3].app_count=2;
    assert(contains(L"PowerShell",L"shell")&&contains(L"中文文件",L"文件")&&!contains(L"abc",L"xyz"));
    puts("PASS file/folder import, duplicate, limit, workspace isolation, search");
    test_mode=TRUE;test_launch_count=0;launch_workspace();test_mode=FALSE;
    assert(test_launch_count==spaces[3].app_count&&!bulk_launch_running);
    puts("PASS bulk launch opens every snapshot item exactly once");
    assert(startup_command(command,MAX_PATH+40));assert(command[0]==L'"');assert(wcsstr(command,L"\" --startup"));
    puts("PASS quoted startup command; user registry untouched");
    /* Real WM_DROPFILES payload through production handler, using isolated config. */
    size_t bytes=sizeof(DROPFILES)+(wcslen(path)+2)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GHND,bytes);DROPFILES *drop=GlobalLock(memory);drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;
    wcscpy((wchar_t*)((BYTE*)drop+sizeof(DROPFILES)),path);GlobalUnlock(memory);
    spaces[3].app_count=0;handle_drop((HDROP)memory);assert(spaces[3].app_count==1);
    load_config();assert(spaces[3].app_count==1);puts("PASS HDROP import and persisted launch path");
    /* Registry redirection verifies enable/disable without changing real startup. */
    wchar_t regpath[100];swprintf(regpath,100,L"Software\\NovaDesktopTest-%lu",GetCurrentProcessId());
    HKEY isolated;assert(RegCreateKeyExW(HKEY_CURRENT_USER,regpath,0,NULL,0,KEY_ALL_ACCESS,NULL,&isolated,NULL)==ERROR_SUCCESS);
    assert(RegOverridePredefKey(HKEY_CURRENT_USER,isolated)==ERROR_SUCCESS);
    assert(!read_startup());startup_enabled=FALSE;toggle_startup();assert(read_startup());toggle_startup();assert(!read_startup());
    assert(RegOverridePredefKey(HKEY_CURRENT_USER,NULL)==ERROR_SUCCESS);RegCloseKey(isolated);RegDeleteTreeW(HKEY_CURRENT_USER,regpath);
    puts("PASS startup enable/disable in isolated registry (real startup unchanged)");
    if(argc>1&&(lstrcmpW(argv[1],L"--desktop")==0||lstrcmpW(argv[1],L"--interactions")==0)){
        test_mode=TRUE;prefer_pin=FALSE;SetProcessDPIAware();OleInitialize(NULL);
        INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
        background=CreateSolidBrush(BG);panel_brush=CreateSolidBrush(PANEL);
        WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(NULL);wc.lpfnWndProc=window_proc;wc.lpszClassName=L"NovaDesktopIntegrationTest";
        assert(LoadImageW(wc.hInstance,MAKEINTRESOURCEW(IDI_NOVA),IMAGE_ICON,32,32,LR_SHARED));
        assert(RegisterClassExW(&wc));
        HDC test_dc=GetDC(NULL);dpi=GetDeviceCaps(test_dc,LOGPIXELSX);ReleaseDC(NULL,test_dc);
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"NOVA integration test",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,100,100,px(1100),px(760),NULL,NULL,wc.hInstance,NULL);
        assert(window);ShowWindow(window,SW_SHOWNOACTIVATE);RECT before,after;GetWindowRect(window,&before);
        if(lstrcmpW(argv[1],L"--desktop")==0){
        assert(set_pinned(TRUE)&&pinned&&!GetParent(window));
        unsigned painted_before=paint_generation;assert(set_desktop_mode(TRUE));
        assert(desktop_mode&&!GetParent(window)&&IsWindowVisible(window)&&paint_generation>painted_before);
        assert(!pinned&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD));assert(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST));assert(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW);
        assert(set_desktop_mode(FALSE));assert(!desktop_mode&&!GetParent(window));
        GetWindowRect(window,&after);assert(EqualRect(&before,&after));
        }
        else assert(set_pinned(FALSE));
        assert(!GetDlgItem(window,ID_STARTUP)&&!GetDlgItem(window,ID_RENAME)&&!GetDlgItem(window,ID_DELETE));
        assert(!GetDlgItem(window,ID_FILES));
        assert(GetDlgItem(window,ID_SETTINGS)&&GetDlgItem(window,ID_NEW)&&GetDlgItem(window,ID_DESKTOP)&&GetDlgItem(window,ID_LAUNCH_ALL));
        RECT first_icon;assert(ListView_GetItemRect(app_list,0,&first_icon,LVIR_ICON));hold_drag_message(&hold_drag,WM_LBUTTONDOWN,0,MAKELPARAM((first_icon.left+first_icon.right)/2,(first_icon.top+first_icon.bottom)/2));assert(hold_drag.armed&&hold_drag.left_down&&hold_drag.source==0&&GetCapture()==app_list);
        assert(hold_drag_message(&hold_drag,WM_TIMER,HOLD_DRAG_TIMER_ID,0)&&hold_drag.dragging);assert(ListView_GetItemState(app_list,0,LVIS_DROPHILITED)&LVIS_DROPHILITED);hold_drag_cancel(&hold_drag);
        /* Drive this test window's release path; never interact with external apps. */
        int old_count=spaces[0].app_count,source_count=spaces[3].app_count;
        hold_drag.source=0;hold_drag.armed=1;hold_drag.dragging=1;
        RECT target_rect;GetWindowRect(space_list,&target_rect);POINT point={target_rect.left+10,target_rect.top+10};ScreenToClient(app_list,&point);
        SendMessageW(app_list,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        assert(spaces[3].app_count==source_count&&spaces[0].app_count==old_count);
        assert(!hold_drag.armed&&!hold_drag.dragging);
        SetWindowTextW(search_edit,L"filter");assert(!hold_drag.enabled);SetWindowTextW(search_edit,L"");assert(hold_drag.enabled);
        active_space=1;assert(ensure_items(1));test_launch_count=0;int expected_launches=spaces[1].app_count;SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_SPACES,LBN_DBLCLK),(LPARAM)space_list);
        assert(test_launch_count==expected_launches&&!bulk_launch_running);
        puts("PASS protected directory drop target, filter guard and one-shot bulk launch wiring (no apps launched)");
        wchar_t fm_settings[MAX_PATH];swprintf(fm_settings,MAX_PATH,L"%ls\\file-manager.ini",dir);
        for(int i=0;i<4;i++){wchar_t section[32];swprintf(section,32,L"Pane%d",i);WritePrivateProfileStringW(section,L"Tab0",dir,fm_settings);}
        open_file_manager();assert(files_page&&IsWindow(files_view));
        assert(GetParent(files_view)==window&&GetAncestor(files_view,GA_ROOT)==window);
        assert(IsWindowVisible(files_view)&&!IsWindowVisible(app_list)&&IsWindowVisible(space_list));
        HWND original_view=files_view;active_space=0;show_workspace_page();assert(files_page&&IsWindowVisible(files_view)&&!IsWindowVisible(app_list));
        int before_delete=space_count;edit_name();delete_space();assert(!editing_name&&space_count==before_delete&&!lstrcmpW(spaces[0].name,L"目录"));
        SendMessageW(space_list,LB_SETCURSEL,1,0);
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_SPACES,LBN_SELCHANGE),(LPARAM)space_list);
        assert(!files_page&&IsWindowVisible(app_list)&&!IsWindowVisible(files_view));
        SendMessageW(space_list,LB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_SPACES,LBN_SELCHANGE),(LPARAM)space_list);
        assert(files_view==original_view&&files_page&&IsWindowVisible(files_view)&&!lstrcmpW(spaces[0].name,L"目录"));
        puts("PASS directory workspace is fixed to file management; ordinary workspaces remain editable");
        file_manager_close();DestroyWindow(window);DeleteFileW(fm_settings);OleUninitialize();puts("PASS window control integration");
    }
    store_close();DeleteFileW(path);DeleteFileW(config_path);
    wchar_t db_file[MAX_PATH];swprintf(db_file,MAX_PATH,L"%ls\\nova.sqlite",dir);DeleteFileW(db_file);swprintf(db_file,MAX_PATH,L"%ls\\nova.backup.sqlite",dir);DeleteFileW(db_file);RemoveDirectoryW(dir);
    return 0;
}
