#define wWinMain nova_entry
#include "../src/main.c"
#undef wWinMain
#include <assert.h>

int wmain(int argc,wchar_t **argv) {
    wchar_t tempdir[MAX_PATH],dir[MAX_PATH],path[MAX_PATH],command[MAX_PATH+40];
    GetTempPathW(MAX_PATH,tempdir);swprintf(dir,MAX_PATH,L"%lsnova-test-%lu",tempdir,GetCurrentProcessId());
    assert(CreateDirectoryW(dir,NULL));swprintf(config_path,MAX_PATH,L"%ls\\config.ini",dir);
    defaults();lstrcpyW(spaces[0].name,L"中文工作区 演示");active_space=2;prefer_pin=TRUE;
    assert(save_config());ZeroMemory(spaces,sizeof(spaces));active_space=0;prefer_pin=FALSE;load_config();
    assert(lstrcmpW(spaces[0].name,L"中文工作区 演示")==0&&active_space==2&&prefer_pin);
    assert(spaces[0].app_count==4);puts("PASS UTF-16 config round-trip and pin preference");
    WritePrivateProfileStringW(L"Nova",L"Active",L"-5",config_path);load_config();assert(active_space==0);
    puts("PASS malformed active workspace bounded");
    active_space=3;spaces[3].app_count=0;
    swprintf(path,MAX_PATH,L"%ls\\启动文件.cmd",dir);
    HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(f!=INVALID_HANDLE_VALUE);CloseHandle(f);
    assert(insert_path(path)==1);assert(insert_path(path)==0);assert(insert_path(dir)==1);
    assert(insert_path(L"Z:\\NOVA_DOES_NOT_EXIST_1735.exe")==-1);
    assert(spaces[3].app_count==2&&spaces[0].app_count==4);
    spaces[3].app_count=MAX_APPS;assert(insert_path(config_path)==-2);spaces[3].app_count=2;
    assert(contains(L"PowerShell",L"shell")&&contains(L"中文文件",L"文件")&&!contains(L"abc",L"xyz"));
    puts("PASS file/folder import, duplicate, limit, workspace isolation, search");
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
        test_mode=TRUE;prefer_pin=FALSE;SetProcessDPIAware();CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
        background=CreateSolidBrush(BG);panel_brush=CreateSolidBrush(PANEL);
        WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(NULL);wc.lpfnWndProc=window_proc;wc.lpszClassName=L"NovaDesktopIntegrationTest";
        assert(RegisterClassExW(&wc));
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"NOVA integration test",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,100,100,1100,760,NULL,NULL,wc.hInstance,NULL);
        assert(window);ShowWindow(window,SW_SHOWNOACTIVATE);RECT before,after;GetWindowRect(window,&before);
        if(lstrcmpW(argv[1],L"--desktop")==0){
        assert(set_pinned(TRUE));assert(!GetParent(window));
        for(int retry=0;retry<20&&!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST);retry++){MSG pending;while(PeekMessageW(&pending,NULL,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}Sleep(10);}
        assert(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST);
        assert(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD));assert(GetWindowLongPtrW(window,GWL_STYLE)&WS_THICKFRAME);
        GetWindowRect(window,&after);assert(EqualRect(&before,&after));
        }
        assert(set_pinned(FALSE));assert(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST));
        GetWindowRect(window,&after);assert(EqualRect(&before,&after));
        assert(!GetDlgItem(window,ID_STARTUP)&&!GetDlgItem(window,ID_RENAME)&&!GetDlgItem(window,ID_DELETE));
        assert(GetDlgItem(window,ID_SETTINGS)&&GetDlgItem(window,ID_NEW));
        /* Drive this test window's release path; never interact with external apps. */
        int old_count=spaces[0].app_count;
        hold_drag.source=0;hold_drag.armed=1;hold_drag.dragging=1;
        RECT target_rect;GetWindowRect(space_list,&target_rect);POINT point={target_rect.left+10,target_rect.top+10};ScreenToClient(app_list,&point);
        SendMessageW(app_list,WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        assert(spaces[3].app_count==0&&spaces[0].app_count==old_count+1);
        assert(!hold_drag.armed&&!hold_drag.dragging);
        SetWindowTextW(search_edit,L"filter");assert(!hold_drag.enabled);SetWindowTextW(search_edit,L"");assert(hold_drag.enabled);
        active_space=0;SendMessageW(window,WM_COMMAND,MAKEWPARAM(ID_SPACES,LBN_DBLCLK),(LPARAM)space_list);
        assert(launch_queue.running&&launch_queue.count==spaces[0].app_count);launch_queue_cancel(&launch_queue);KillTimer(window,LAUNCH_TIMER);
        puts("PASS drag release into workspace, filter guard, double-click queue wiring (no apps launched)");
        DestroyWindow(window);CoUninitialize();puts("PASS window control integration");
    }
    DeleteFileW(path);DeleteFileW(config_path);RemoveDirectoryW(dir);
    return 0;
}
