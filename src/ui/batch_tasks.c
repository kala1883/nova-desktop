#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "batch_tasks.h"
#include "../core/batch_task.h"
#include "../i18n.h"
#include "../platform/batch_runner.h"
#include "../platform/storage.h"

#define BATCH_CLASS L"NovaBatchTasksWindow"
#define ID_BATCH_LIST 2101
#define ID_BATCH_ADD 2102
#define ID_BATCH_REMOVE 2103
#define ID_BATCH_RUN 2104
#define ID_BATCH_CLOSE 2105
#define ID_BATCH_SAVE 2107
#define ID_TASK_NEW 2108
#define ID_TASK_DELETE 2109
#define ID_TASKS 2110
#define ID_TASK_SHORTCUT 2113
#define ID_STEP_EDIT 2114
#define ID_STEP_APPLY 2115
#define ID_STEP_BROWSE 2116
#define ID_STEP_DIRECTORY 2117
#define ID_STEP_COMMAND 2118
#define ID_STEP_RUN 2119
#define ID_STEP_CANCEL 2120
#define WM_BATCH_RESUME (WM_APP+73)
static HWND run_step_button,cancel_step_button;
static HWND edit_button,apply_button,browse_button,directory_edit,step_command_edit,directory_label,step_command_label;
static int selected_step=-1,refreshing;
static BOOL save_command(void);
static BOOL save_current(const BatchTask *value);
static void move_step(int source,int destination);
static void cancel_step_drag(void);
static void show_step(void);
static void enable_step(void);
static HWND shortcut_button,task_owner;
static BatchTaskQueue task_queue;
static BOOL start_next_task(void);
static HWND task_list,name_edit,mode_combo,new_task_button,delete_task_button,name_label,mode_label;
static BatchTaskList collection;
static int selected_task;
#define task (collection.tasks[selected_task])
static HWND command_edit,save_button,command_label;

enum { BATCH_WAITING, BATCH_RUNNING, BATCH_SUCCEEDED, BATCH_FAILED, BATCH_SKIPPED, BATCH_CANCELLED, BATCH_CANCELLING };
enum { EVENT_STARTED=1, EVENT_RESULT, EVENT_SKIPPED, EVENT_CANCELLED, EVENT_FINISHED };

typedef struct {
    HWND owner;
    HANDLE cancel;
    BatchTask snapshot;
    int slot; /* UI-owned slot maps immutable worker results to the current row. */
    void *completion;

} BatchRunContext;

typedef struct {
    int type,index,success,failed,skipped,cancelled,total,slot;
    DWORD exit_code,error;
    wchar_t output[BATCH_OUTPUT_CAP];
} BatchRunEvent;

static HWND window,list,add_button,remove_button,run_button,close_button,hover_button;
#define STEP_HOLD_TIMER 7801
#define STEP_SCROLL_TIMER 7802
static struct { int armed,dragging,source,insertion,marker; POINT pointer; } step_drag;
static HFONT body_font,title_font;
static HBRUSH background,panel;
static HIMAGELIST row_height_images;

static int loaded,running,cancel_requested,statuses[BATCH_DIRECTORY_LIMIT];
static DWORD exit_codes[BATCH_DIRECTORY_LIMIT],errors[BATCH_DIRECTORY_LIMIT];
static wchar_t outputs[BATCH_DIRECTORY_LIMIT][BATCH_OUTPUT_CAP];
typedef struct { HANDLE worker,cancel; int row; BOOL cancelling; } BatchActiveRun;
static BatchActiveRun active_runs[BATCH_DIRECTORY_LIMIT];
static BatchResult run_totals;
static int dpi=96;
static wchar_t summary_text[256];
static const COLORREF BG=RGB(14,19,29),PANEL=RGB(22,29,42),TEXT=RGB(236,241,249),MUTED=RGB(167,181,202),ACCENT=RGB(53,77,122);

static int bpx(int value){return MulDiv(value,dpi,96);}
static BOOL whole_task_running(void){
    for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].worker&&active_runs[i].row<0)return TRUE;
    return FALSE;
}
static BatchActiveRun *active_step(int index){
    if(index<0)return NULL;
    for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].worker&&active_runs[i].row==index)return &active_runs[i];
    return NULL;
}
static BOOL step_running(int index){
    if(whole_task_running())return TRUE;
    return active_step(index)!=NULL;
}
static BOOL step_available(int index){
    return collection.count&&index>=0&&index<task.directory_count&&!cancel_requested&&!step_running(index);
}
static void cancel_runs(void){
    for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].cancel){active_runs[i].cancelling=TRUE;SetEvent(active_runs[i].cancel);}
}
static const wchar_t *status_text(int status){
    switch(status){
    case BATCH_RUNNING:return nova_text(L"执行中",L"Running");
    case BATCH_SUCCEEDED:return nova_text(L"成功",L"Succeeded");
    case BATCH_FAILED:return nova_text(L"失败",L"Failed");
    case BATCH_SKIPPED:return nova_text(L"已跳过",L"Skipped");
    case BATCH_CANCELLED:return nova_text(L"已取消",L"Cancelled");
    case BATCH_CANCELLING:return nova_text(L"正在取消…",L"Cancelling…");
    default:return nova_text(L"等待",L"Waiting");
    }
}
static void set_summary(const wchar_t *value){lstrcpynW(summary_text,value,256);if(window)InvalidateRect(window,NULL,FALSE);}
static void system_error_text(DWORD error,wchar_t *value,DWORD capacity){
    if(!value||!capacity)return;
    value[0]=0;
    if(!error)return;
    DWORD length=FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error,0,value,capacity,NULL);
    while(length&&(value[length-1]==L'\r'||value[length-1]==L'\n'||value[length-1]==L' '||value[length-1]==L'.'))value[--length]=0;
}
static void result_detail(int index,wchar_t *value,DWORD capacity){
    if(!value||!capacity)return;
    value[0]=0;
    if(index<0||index>=task.directory_count)return;
    if(outputs[index][0])lstrcpynW(value,outputs[index],capacity);
    else if(errors[index]&&errors[index]!=ERROR_CANCELLED)system_error_text(errors[index],value,capacity);
}
static void show_result_summary(int index){
    if(index<0||index>=task.directory_count)return;
    wchar_t detail[BATCH_OUTPUT_CAP];result_detail(index,detail,BATCH_OUTPUT_CAP);
    if(statuses[index]==BATCH_FAILED){
        wchar_t value[256];
        if(detail[0])swprintf(value,256,nova_text(L"失败详情：%.230ls",L"Failure details: %.230ls"),detail);
        else if(errors[index])swprintf(value,256,nova_text(L"执行失败（Windows 错误 %lu）。",L"Execution failed (Windows error %lu)."),errors[index]);
        else swprintf(value,256,nova_text(L"命令失败（退出码 %lu），未产生输出。",L"Command failed (exit %lu) and produced no output."),exit_codes[index]);
        set_summary(value);
    }else if(statuses[index]==BATCH_CANCELLED)set_summary(nova_text(L"当前命令已终止。",L"The active command was terminated."));
}
static void show_result_dialog(int index){
    if(index<0||index>=task.directory_count||(statuses[index]!=BATCH_FAILED&&statuses[index]!=BATCH_CANCELLED&&statuses[index]!=BATCH_SUCCEEDED))return;
    wchar_t detail[BATCH_OUTPUT_CAP];result_detail(index,detail,BATCH_OUTPUT_CAP);
    if(!detail[0])lstrcpynW(detail,nova_text(L"命令未产生输出。",L"The command produced no output."),BATCH_OUTPUT_CAP);
    wchar_t message[BATCH_OUTPUT_CAP+BATCH_DIRECTORY_CAP+160];
    if(statuses[index]==BATCH_CANCELLED)swprintf(message,sizeof(message)/sizeof(message[0]),nova_text(L"目录：%ls\n\n当前命令已终止。\n\n最后输出：\n%ls",L"Folder: %ls\n\nThe active command was terminated.\n\nLast output:\n%ls"),task.directories[index],detail);
    else if(errors[index])swprintf(message,sizeof(message)/sizeof(message[0]),nova_text(L"目录：%ls\nWindows 错误：%lu\n\n%ls",L"Folder: %ls\nWindows error: %lu\n\n%ls"),task.directories[index],errors[index],detail);
    else swprintf(message,sizeof(message)/sizeof(message[0]),nova_text(L"目录：%ls\n退出码：%lu\n\n%ls",L"Folder: %ls\nExit code: %lu\n\n%ls"),task.directories[index],exit_codes[index],detail);
    MessageBoxW(window,message,statuses[index]==BATCH_SUCCEEDED?nova_text(L"任务输出",L"Task output"):statuses[index]==BATCH_CANCELLED?nova_text(L"任务已取消",L"Task cancelled"):nova_text(L"任务失败",L"Task failed"),MB_OK|(statuses[index]==BATCH_FAILED?MB_ICONWARNING:MB_ICONINFORMATION));
}
static void refresh_row(int index){
    if(!list||index<0||index>=task.directory_count)return;
    wchar_t value[BATCH_OUTPUT_CAP+96];lstrcpynW(value,status_text(statuses[index]),sizeof(value)/sizeof(value[0]));
    if(statuses[index]==BATCH_FAILED){
        wchar_t detail[BATCH_OUTPUT_CAP];result_detail(index,detail,BATCH_OUTPUT_CAP);
        if(errors[index]&&detail[0])swprintf(value,sizeof(value)/sizeof(value[0]),nova_text(L"失败（Windows %lu）：%ls",L"Failed (Windows %lu): %ls"),errors[index],detail);
        else if(errors[index])swprintf(value,sizeof(value)/sizeof(value[0]),nova_text(L"失败（Windows %lu）",L"Failed (Windows %lu)"),errors[index]);
        else if(detail[0])swprintf(value,sizeof(value)/sizeof(value[0]),nova_text(L"失败（退出码 %lu）：%ls",L"Failed (exit %lu): %ls"),exit_codes[index],detail);
        else swprintf(value,sizeof(value)/sizeof(value[0]),nova_text(L"失败（退出码 %lu；无输出）",L"Failed (exit %lu; no output)"),exit_codes[index]);
    }
    if(statuses[index]==BATCH_SUCCEEDED&&outputs[index][0])swprintf(value,sizeof(value)/sizeof(value[0]),nova_text(L"成功：%ls",L"Succeeded: %ls"),outputs[index]);
    ListView_SetItemText(list,index,2,value);
    ListView_SetItemText(list,index,0,task.directories[index]);
    ListView_SetItemText(list,index,1,(LPWSTR)batch_task_command(&task,index));
}
static void refresh_list(void){
    if(!list)return;
    cancel_step_drag();
    refreshing=TRUE;ListView_DeleteAllItems(list);
    for(int i=0;i<task.directory_count;i++){
        LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=task.directories[i];ListView_InsertItem(list,&item);refresh_row(i);
    }
    if(selected_step>=task.directory_count)selected_step=task.directory_count-1;
    if(selected_step>=0)ListView_SetItemState(list,selected_step,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    refreshing=FALSE;show_step();
    EnableWindow(run_button,task.directory_count>0&&(!running||!cancel_requested));
}
static void localize(void){
    if(!window)return;
    SetWindowTextW(window,nova_text(L"批量任务",L"Batch tasks"));
    SetWindowTextW(shortcut_button,nova_text(L"添加到工作区…",L"Add to workspace…"));
    SetWindowTextW(command_label,nova_text(L"默认命令",L"Default command"));SetWindowTextW(save_button,nova_text(L"保存任务",L"Save task"));
    SetWindowTextW(new_task_button,nova_text(L"新建任务",L"New task"));SetWindowTextW(delete_task_button,nova_text(L"删除任务",L"Delete task"));
    SetWindowTextW(name_label,nova_text(L"名称",L"Name"));SetWindowTextW(mode_label,nova_text(L"执行模式",L"Execution"));
    int mode=(int)SendMessageW(mode_combo,CB_GETCURSEL,0,0);
    SendMessageW(mode_combo,CB_RESETCONTENT,0,0);
    SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"顺序执行（逐个目录）",L"Sequential (one folder at a time)"));
    SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"同时执行（所有目录）",L"Parallel (all folders)"));
    SendMessageW(mode_combo,CB_SETCURSEL,mode<0?task.mode:mode,0);
    SetWindowTextW(task_list,nova_text(L"已保存的任务",L"Saved tasks"));SetWindowTextW(list,nova_text(L"批量任务目录",L"Batch task folders"));
    if(!running)set_summary(nova_text(L"选择任务后可单独一键执行。",L"Select a task to run it individually."));
    SetWindowTextW(run_step_button,nova_text(L"执行选中项",L"Run selected"));
    SetWindowTextW(add_button,nova_text(L"新增子任务",L"Add subtask"));SetWindowTextW(remove_button,nova_text(L"删除",L"Delete"));
    SetWindowTextW(edit_button,nova_text(L"编辑",L"Edit"));SetWindowTextW(apply_button,nova_text(L"应用修改",L"Apply changes"));SetWindowTextW(browse_button,nova_text(L"浏览…",L"Browse…"));
    SetWindowTextW(directory_label,nova_text(L"工作目录",L"Working folder"));SetWindowTextW(step_command_label,nova_text(L"子任务命令",L"Subtask command"));
    SendMessageW(directory_edit,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"选择子任务后编辑工作目录",L"Select a subtask to edit its folder"));
    SendMessageW(step_command_edit,EM_SETCUEBANNER,TRUE,(LPARAM)nova_text(L"每个子任务可使用不同命令",L"Each subtask can use a different command"));
    SetWindowTextW(run_button,cancel_requested?nova_text(L"正在取消…",L"Cancelling…"):running?nova_text(L"取消全部",L"Cancel all"):nova_text(L"一键执行",L"Run task"));SetWindowTextW(close_button,nova_text(L"关闭",L"Close"));
    LVCOLUMNW column={0};column.mask=LVCF_TEXT;column.pszText=(LPWSTR)nova_text(L"目录",L"Folder");ListView_SetColumn(list,0,&column);column.pszText=(LPWSTR)nova_text(L"命令",L"Command");ListView_SetColumn(list,1,&column);column.pszText=(LPWSTR)nova_text(L"状态",L"Status");ListView_SetColumn(list,2,&column);
    for(int i=0;i<task.directory_count;i++)refresh_row(i);
    enable_step();
    InvalidateRect(window,NULL,TRUE);
}
static void layout(void){
    if(!window)return;
    RECT client;GetClientRect(window,&client);int margin=bpx(24),height=bpx(36);
    int left=bpx(244),width=client.right-left-margin,right=client.right-margin;
    MoveWindow(new_task_button,margin,bpx(72),bpx(192),height,TRUE);
    MoveWindow(task_list,margin,bpx(120),bpx(192),client.bottom-bpx(202),TRUE);
    MoveWindow(delete_task_button,margin,client.bottom-bpx(60),bpx(192),height,TRUE);
    MoveWindow(shortcut_button,right-bpx(302),bpx(20),bpx(170),height,TRUE);
    MoveWindow(save_button,right-bpx(120),bpx(20),bpx(120),height,TRUE);
    int half=(width-bpx(24))/2;
    MoveWindow(name_label,left,bpx(74),half,bpx(22),TRUE);
    MoveWindow(name_edit,left,bpx(100),half,bpx(32),TRUE);
    MoveWindow(mode_label,left+half+bpx(24),bpx(74),half,bpx(22),TRUE);
    MoveWindow(mode_combo,left+half+bpx(24),bpx(100),half,bpx(180),TRUE);
    MoveWindow(command_label,left,bpx(146),bpx(138),bpx(28),TRUE);
    MoveWindow(command_edit,left+bpx(142),bpx(142),width-bpx(142),bpx(32),TRUE);
    MoveWindow(run_step_button,right-bpx(524),bpx(194),bpx(128),height,TRUE);
    MoveWindow(cancel_step_button,right-bpx(388),bpx(194),bpx(128),height,TRUE);
    MoveWindow(add_button,right-bpx(252),bpx(194),bpx(108),height,TRUE);
    MoveWindow(edit_button,right-bpx(136),bpx(194),bpx(64),height,TRUE);
    MoveWindow(remove_button,right-bpx(64),bpx(194),bpx(64),height,TRUE);
    MoveWindow(list,left,bpx(240),width,client.bottom-bpx(500),TRUE);
    int editor=client.bottom-bpx(238);
    MoveWindow(directory_label,left,editor,bpx(142),bpx(26),TRUE);
    MoveWindow(directory_edit,left+bpx(146),editor,width-bpx(250),bpx(32),TRUE);
    MoveWindow(browse_button,right-bpx(96),editor,bpx(96),bpx(32),TRUE);
    MoveWindow(step_command_label,left,editor+bpx(46),bpx(142),bpx(26),TRUE);
    MoveWindow(step_command_edit,left+bpx(146),editor+bpx(46),width-bpx(146),bpx(36),TRUE);
    MoveWindow(apply_button,right-bpx(142),editor+bpx(94),bpx(142),height,TRUE);
    MoveWindow(close_button,right-bpx(90),client.bottom-bpx(60),bpx(90),height,TRUE);
    MoveWindow(run_button,right-bpx(222),client.bottom-bpx(60),bpx(120),height,TRUE);
    ListView_SetColumnWidth(list,0,(width-bpx(125))/2);ListView_SetColumnWidth(list,1,(width-bpx(125))/2);ListView_SetColumnWidth(list,2,bpx(120));
}
static void draw_text(HDC dc,const wchar_t *value,RECT area,HFONT font,COLORREF color,UINT format){
    HFONT old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value,-1,&area,format|DT_NOPREFIX);SelectObject(dc,old);
}
static void paint_window(HDC dc){
    RECT client;GetClientRect(window,&client);FillRect(dc,&client,background);
    RECT side={0,0,bpx(232),client.bottom};FillRect(dc,&side,panel);
    RECT title={bpx(24),bpx(20),bpx(220),bpx(54)};draw_text(dc,nova_text(L"任务库",L"Task library"),title,title_font,TEXT,DT_SINGLELINE|DT_VCENTER);
    RECT heading={bpx(244),bpx(20),client.right-bpx(338),bpx(54)};draw_text(dc,nova_text(L"任务详情",L"Task details"),heading,title_font,TEXT,DT_SINGLELINE|DT_VCENTER);
    RECT help={bpx(244),bpx(194),client.right-bpx(572),bpx(230)};draw_text(dc,nova_text(L"子任务",L"Subtasks"),help,title_font,TEXT,DT_SINGLELINE|DT_VCENTER);
    RECT hint={bpx(244),client.bottom-bpx(100),client.right-bpx(24),client.bottom-bpx(76)};draw_text(dc,nova_text(L"长按并上下拖动排序；双击已完成项查看输出。",L"Hold and drag rows to reorder. Double-click completed rows for output."),hint,body_font,MUTED,DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT summary={bpx(244),client.bottom-bpx(60),client.right-bpx(260),client.bottom-bpx(24)};draw_text(dc,summary_text,summary,body_font,MUTED,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
}
static LRESULT CALLBACK button_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR subclass_id,DWORD_PTR data){
    (void)subclass_id;(void)data;
    if(message==WM_MOUSEMOVE&&hover_button!=hwnd){hover_button=hwnd;TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);InvalidateRect(hwnd,NULL,TRUE);}
    if(message==WM_MOUSELEAVE){if(hover_button==hwnd)hover_button=NULL;InvalidateRect(hwnd,NULL,TRUE);}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,button_proc,1);
    return DefSubclassProc(hwnd,message,wp,lp);
}
static void cancel_step_drag(void){
    step_drag.armed=step_drag.dragging=0;step_drag.insertion=-1;
    if(!list)return;
    KillTimer(list,STEP_HOLD_TIMER);KillTimer(list,STEP_SCROLL_TIMER);
    if(GetCapture()==list)ReleaseCapture();
    InvalidateRect(list,NULL,FALSE);SetCursor(LoadCursorW(NULL,IDC_ARROW));
}
static RECT step_drag_area(void){
    RECT area,header;GetClientRect(list,&area);
    GetWindowRect(ListView_GetHeader(list),&header);MapWindowPoints(NULL,list,(POINT*)&header,2);
    area.top=header.bottom;return area;
}
static void update_step_drag(BOOL scroll){
    RECT area=step_drag_area();POINT point=step_drag.pointer;
    step_drag.insertion=-1;
    if(PtInRect(&area,point)){
        int top=ListView_GetTopIndex(list);RECT row;
        if(scroll&&ListView_GetItemRect(list,top,&row,LVIR_BOUNDS)){
            int delta=point.y<area.top+bpx(20)?-1:point.y>=area.bottom-bpx(20)?1:0;
            if(delta)ListView_Scroll(list,0,delta*(row.bottom-row.top));
        }
        for(int i=ListView_GetTopIndex(list);i<task.directory_count;i++){
            if(!ListView_GetItemRect(list,i,&row,LVIR_BOUNDS))break;
            if(point.y<(row.top+row.bottom)/2){step_drag.insertion=i;step_drag.marker=row.top;break;}
            step_drag.insertion=i+1;step_drag.marker=row.bottom;
        }
        if(step_drag.marker<area.top)step_drag.marker=area.top;
        if(step_drag.marker>area.bottom-bpx(2))step_drag.marker=area.bottom-bpx(2);
    }
    SetCursor(LoadCursorW(NULL,step_drag.insertion<0?IDC_NO:IDC_SIZENS));InvalidateRect(list,NULL,FALSE);
}
static LRESULT CALLBACK folder_list_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(message==WM_LBUTTONDOWN&&!running&&collection.count){
        cancel_step_drag();LVHITTESTINFO hit={0};hit.pt=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        int row=ListView_SubItemHitTest(hwnd,&hit);
        if(row>=0){
            /* Validate pending edits before native selection notifications can change rows. */
            if(!save_command())return 0;
            SetFocus(hwnd);ListView_SetItemState(hwnd,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
            ListView_SetItemState(hwnd,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetSelectionMark(hwnd,row);
            step_drag.source=row;step_drag.pointer=hit.pt;step_drag.armed=1;SetCapture(hwnd);
            if(!SetTimer(hwnd,STEP_HOLD_TIMER,350,NULL))cancel_step_drag();
            return 0;
        }
    }
    if(message==WM_TIMER&&wp==STEP_HOLD_TIMER){
        KillTimer(hwnd,STEP_HOLD_TIMER);
        if(step_drag.armed&&!running){step_drag.dragging=1;update_step_drag(FALSE);SetTimer(hwnd,STEP_SCROLL_TIMER,100,NULL);}
        else cancel_step_drag();
        return 0;
    }
    if(message==WM_TIMER&&wp==STEP_SCROLL_TIMER){if(step_drag.dragging)update_step_drag(TRUE);return 0;}
    if(message==WM_MOUSEMOVE&&step_drag.armed){step_drag.pointer=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(step_drag.dragging)update_step_drag(FALSE);return 0;}
    if(message==WM_LBUTTONUP&&step_drag.armed){
        int source=step_drag.source,destination=-1;
        if(step_drag.dragging){step_drag.pointer=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};update_step_drag(FALSE);destination=step_drag.insertion;if(destination>source)destination--;}
        cancel_step_drag();if(destination>=0)move_step(source,destination);return 0;
    }
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE&&step_drag.armed){cancel_step_drag();return 0;}
    if(message==WM_LBUTTONDBLCLK||message==WM_CANCELMODE||message==WM_KILLFOCUS||(message==WM_CAPTURECHANGED&&GetCapture()!=hwnd))cancel_step_drag();
    if(message==WM_NOTIFY&&((NMHDR*)lp)->code==NM_CUSTOMDRAW)return SendMessageW(window,WM_NOTIFY,wp,lp);
    if(message==WM_PAINT){
        LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
        if(!ListView_GetItemCount(hwnd)){
            RECT area,header;GetClientRect(hwnd,&area);GetWindowRect(ListView_GetHeader(hwnd),&header);area.top=header.bottom-header.top;
            HDC dc=GetDC(hwnd);FillRect(dc,&area,panel);InflateRect(&area,-bpx(16),-bpx(8));
            draw_text(dc,nova_text(L"点击“新增子任务”，设置工作目录和命令。",L"Add a subtask, then set its folder and command."),area,body_font,MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);ReleaseDC(hwnd,dc);
        }
        if(step_drag.dragging&&step_drag.insertion>=0){
            RECT area=step_drag_area();area.top=step_drag.marker;area.bottom=area.top+bpx(2);
            HDC dc=GetDC(hwnd);SetDCBrushColor(dc,RGB(145,181,255));FillRect(dc,&area,(HBRUSH)GetStockObject(DC_BRUSH));ReleaseDC(hwnd,dc);
        }
        return result;
    }
    if(message==WM_NCDESTROY){cancel_step_drag();RemoveWindowSubclass(hwnd,folder_list_proc,1);}
    return DefSubclassProc(hwnd,message,wp,lp);
}
static HWND make_button(const wchar_t *label,int id){
    HWND value=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(value,WM_SETFONT,(WPARAM)body_font,TRUE);SetWindowSubclass(value,button_proc,1,0);return value;
}
static void post_event(HWND owner,BatchRunEvent *event){if(!PostMessageW(owner,WM_NOVA_BATCH_EVENT,0,(LPARAM)event))free(event);}
static void run_progress(void *parameter,int type,int index,DWORD code,DWORD error,const wchar_t *output){
    BatchRunContext *context=parameter;
    BatchRunEvent *event=calloc(1,sizeof(*event));if(!event)return;
    event->type=type;event->index=index;event->slot=context->slot;event->total=context->snapshot.directory_count;event->exit_code=code;event->error=error;if(output)lstrcpynW(event->output,output,BATCH_OUTPUT_CAP);post_event(context->owner,event);
}
static DWORD WINAPI run_worker(void *parameter){
    BatchRunContext *context=parameter;
    BatchResult result=batch_runner_task(&context->snapshot,context->cancel,run_progress,context,NULL);
    BatchRunEvent *done=context->completion;
    if(done){done->type=EVENT_FINISHED;done->index=-1;done->slot=context->slot;done->success=result.success;done->failed=result.failed;done->skipped=result.skipped;done->cancelled=result.cancelled;done->total=context->snapshot.directory_count;post_event(context->owner,done);}
    free(context);return 0;
}
static BOOL save_current(const BatchTask *value){
    BatchTask previous=task;task=*value;
    if(!store_save_batch_tasks(&collection)){task=previous;return FALSE;}
    if(task_owner)PostMessageW(task_owner,WM_NOVA_BATCH_CHANGED,0,0);
    return TRUE;
}
static void refresh_tasks(void){
    SendMessageW(task_list,LB_RESETCONTENT,0,0);
    for(int i=0;i<collection.count;i++)SendMessageW(task_list,LB_ADDSTRING,0,(LPARAM)collection.tasks[i].name);
    SendMessageW(task_list,LB_SETCURSEL,collection.count?selected_task:-1,0);
}
static void enable_editor(void){
    BOOL available=collection.count>0&&!running;
    HWND controls[]={command_edit,name_edit,mode_combo,delete_task_button,shortcut_button};
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);i++)EnableWindow(controls[i],available);
    BOOL row_editable=collection.count&&!whole_task_running()&&!cancel_requested;
    EnableWindow(add_button,row_editable&&task.directory_count<BATCH_DIRECTORY_LIMIT);
    EnableWindow(save_button,row_editable);
    enable_step();
    EnableWindow(task_list,!running);EnableWindow(new_task_button,!running&&collection.count<BATCH_TASK_LIMIT);
    EnableWindow(run_button,collection.count&&task.directory_count&&(!running||!cancel_requested));
}
static void show_task(void){
    selected_step=-1;
    SetWindowTextW(name_edit,collection.count?task.name:L"");
    SetWindowTextW(command_edit,collection.count?task.command:L"");
    SendMessageW(mode_combo,CB_SETCURSEL,task.mode,0);
    ZeroMemory(statuses,sizeof(statuses));ZeroMemory(exit_codes,sizeof(exit_codes));ZeroMemory(errors,sizeof(errors));ZeroMemory(outputs,sizeof(outputs));
    refresh_list();enable_editor();set_summary(nova_text(L"选择任务后可单独一键执行。",L"Select a task to run it individually."));
}
static void show_step(void){
    BOOL available=collection.count&&selected_step>=0&&selected_step<task.directory_count;
    int was_refreshing=refreshing;refreshing=TRUE;
    SetWindowTextW(directory_edit,available?task.directories[selected_step]:L"");
    SetWindowTextW(step_command_edit,available?batch_task_command(&task,selected_step):L"");
    refreshing=was_refreshing;
    enable_step();
}
static void enable_step(void){
    HWND fields[]={directory_edit,step_command_edit,browse_button,apply_button,edit_button,remove_button,run_step_button};
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)EnableWindow(fields[i],step_available(selected_step));
    BatchActiveRun *active=active_step(selected_step);
    SetWindowTextW(cancel_step_button,active&&active->cancelling?nova_text(L"正在取消…",L"Cancelling…"):nova_text(L"取消选中项",L"Cancel selected"));
    EnableWindow(cancel_step_button,active&&!active->cancelling&&!cancel_requested);
}
static BOOL read_step(BatchTask *next){
    if(selected_step<0||selected_step>=next->directory_count)return TRUE;
    if(step_running(selected_step))return TRUE;
    wchar_t directory[BATCH_DIRECTORY_CAP],command[BATCH_COMMAND_CAP];
    GetWindowTextW(directory_edit,directory,BATCH_DIRECTORY_CAP);GetWindowTextW(step_command_edit,command,BATCH_COMMAND_CAP);
    /* Preserve legacy inheritance until this row is explicitly edited. */
    if(!wcscmp(directory,task.directories[selected_step])&&!wcscmp(command,batch_task_command(&task,selected_step)))return TRUE;
    if(!batch_task_edit_step(next,selected_step,directory,command)){
        set_summary(nova_text(L"目录不能为空或重复，命令不能为空。",L"Use a unique folder and a non-empty command."));return FALSE;
    }
    return TRUE;
}
static void edit_step(void){if(step_available(selected_step)){SetFocus(step_command_edit);SendMessageW(step_command_edit,EM_SETSEL,0,-1);}}
static void browse_step(void){
    if(!step_available(selected_step))return;
    long long task_id=task.id;int row=selected_step;
    BROWSEINFOW browse={0};browse.hwndOwner=window;browse.lpszTitle=nova_text(L"选择工作目录",L"Choose a working folder");browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);if(!item)return;
    wchar_t path[BATCH_DIRECTORY_CAP];BOOL ok=SHGetPathFromIDListW(item,path);CoTaskMemFree(item);
    if(ok&&window&&task.id==task_id&&selected_step==row&&step_available(row))SetWindowTextW(directory_edit,path);
}
static BOOL save_command(void){
    if(whole_task_running()||!collection.count)return TRUE;
    BatchTask next=task;if(!running)GetWindowTextW(command_edit,next.command,BATCH_COMMAND_CAP);
    if(!next.command[wcsspn(next.command,L" \t\r\n")]){set_summary(nova_text(L"请输入要执行的命令。",L"Enter a command to run."));SetFocus(command_edit);return FALSE;}
    if(!read_step(&next))return FALSE;
    if(!running){GetWindowTextW(name_edit,next.name,BATCH_NAME_CAP);next.mode=(int)SendMessageW(mode_combo,CB_GETCURSEL,0,0);}
    if(!next.name[wcsspn(next.name,L" \t\r\n")]){set_summary(nova_text(L"请输入任务名称。",L"Enter a task name."));SetFocus(name_edit);return FALSE;}
    BOOL changed[BATCH_DIRECTORY_LIMIT]={0};
    for(int i=0;i<next.directory_count;i++)changed[i]=wcscmp(next.directories[i],task.directories[i])||wcscmp(batch_task_command(&next,i),batch_task_command(&task,i));
    if(!save_current(&next)){MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    for(int i=0;i<next.directory_count;i++)if(changed[i]){statuses[i]=BATCH_WAITING;exit_codes[i]=errors[i]=0;outputs[i][0]=0;}
    task=next;refresh_tasks();for(int i=0;i<task.directory_count;i++)refresh_row(i);show_step();set_summary(nova_text(L"任务已保存。",L"Task saved."));
    if(!running&&task_queue.count)PostMessageW(window,WM_BATCH_RESUME,0,0);
    return TRUE;
}
static void move_step(int source,int destination){
    if(running||!collection.count||source<0||source>=task.directory_count||destination<0||destination>=task.directory_count||source==destination)return;
    if(!save_command())return;
    BatchTask next=task;
    int direction=destination>source?1:-1;
    for(int i=source;i!=destination;i+=direction){
        int j=i+direction;
        memcpy(next.directories[i],task.directories[j],sizeof(next.directories[i]));
        memcpy(next.commands[i],task.commands[j],sizeof(next.commands[i]));
    }
    memcpy(next.directories[destination],task.directories[source],sizeof(next.directories[destination]));
    memcpy(next.commands[destination],task.commands[source],sizeof(next.commands[destination]));
    /* Commit configuration before moving UI results; a failed write leaves order intact. */
    if(!save_current(&next)){MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return;}
    int status=statuses[source];DWORD code=exit_codes[source],error=errors[source];wchar_t output[BATCH_OUTPUT_CAP];
    memcpy(output,outputs[source],sizeof(output));
    for(int i=source;i!=destination;i+=direction){
        int j=i+direction;statuses[i]=statuses[j];exit_codes[i]=exit_codes[j];errors[i]=errors[j];memcpy(outputs[i],outputs[j],sizeof(outputs[i]));
    }
    statuses[destination]=status;exit_codes[destination]=code;errors[destination]=error;memcpy(outputs[destination],output,sizeof(output));
    selected_step=destination;refresh_list();ListView_EnsureVisible(list,destination,FALSE);
    set_summary(nova_text(L"子任务顺序已保存。",L"Subtask order saved."));
}
static BOOL begin_task(const BatchTask *snapshot,int source_row){
    if(source_row < -1||source_row>=snapshot->directory_count)return FALSE;
    if((source_row<0&&running)||(source_row>=0&&!step_available(source_row)))return FALSE;
    int slot=0;while(slot<BATCH_DIRECTORY_LIMIT&&active_runs[slot].worker)slot++;
    if(slot==BATCH_DIRECTORY_LIMIT)return FALSE;
    BatchRunContext *context=calloc(1,sizeof(*context));
    if(!context){MessageBoxW(window,nova_text(L"内存不足，无法创建批量任务快照。",L"Not enough memory to create the batch task snapshot."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->completion=calloc(1,sizeof(BatchRunEvent));
    if(!context->completion){free(context);MessageBoxW(window,nova_text(L"内存不足，无法启动任务。",L"Not enough memory to start the task."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->owner=task_owner;context->snapshot=*snapshot;context->slot=slot;
    if(source_row>=0){
        lstrcpyW(context->snapshot.directories[0],snapshot->directories[source_row]);
        lstrcpyW(context->snapshot.commands[0],batch_task_command(snapshot,source_row));
        context->snapshot.directory_count=1;context->snapshot.mode=BATCH_SEQUENTIAL;
    }
    HANDLE cancel_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!cancel_event){free(context->completion);free(context);MessageBoxW(window,nova_text(L"无法创建批量任务取消事件。",L"Unable to create the batch task cancellation event."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->cancel=cancel_event;
    for(int i=0;i<collection.count;i++)if(collection.tasks[i].id==snapshot->id){selected_task=i;break;}
    if(source_row<0){
        if(window){refresh_tasks();show_task();}
        for(int i=0;i<task.directory_count;i++){statuses[i]=BATCH_WAITING;exit_codes[i]=errors[i]=0;outputs[i][0]=0;}
    }
    HANDLE worker=CreateThread(NULL,0,run_worker,context,0,NULL);
    if(!worker){CloseHandle(cancel_event);free(context->completion);free(context);MessageBoxW(window,nova_text(L"无法创建批量任务工作线程。",L"Unable to create the batch task worker thread."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    active_runs[slot].worker=worker;active_runs[slot].cancel=cancel_event;active_runs[slot].row=source_row;
    if(!running)ZeroMemory(&run_totals,sizeof(run_totals));
    running++;cancel_requested=FALSE;
    if(source_row>=0){statuses[source_row]=BATCH_RUNNING;exit_codes[source_row]=errors[source_row]=0;outputs[source_row][0]=0;}
    set_summary(nova_text(L"任务已开始…",L"Task started…"));refresh_list();
    SetWindowTextW(run_button,nova_text(L"取消全部",L"Cancel all"));enable_editor();
    return TRUE;
}
static BOOL start_next_task(void){
    if(running)return FALSE;
    /* A finishing worker must not discard edits to another idle row. Invalid
       edits leave the queue intact; a later successful save resumes it. */
    if(window&&!save_command())return FALSE;
    BatchTask snapshot;
    while(batch_task_queue_take(&task_queue,&snapshot)){
        if(begin_task(&snapshot,-1))return TRUE;
        task_queue.active_id=0;
    }
    return FALSE;
}
static void start_or_cancel(void){
    if(running){
        batch_task_queue_cancel(&task_queue);cancel_requested=TRUE;cancel_runs();
        for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].worker&&active_runs[i].row>=0){statuses[active_runs[i].row]=BATCH_CANCELLING;refresh_row(active_runs[i].row);}
        enable_editor();
        SetWindowTextW(run_button,nova_text(L"正在取消…",L"Cancelling…"));EnableWindow(run_button,FALSE);return;
    }
    if(collection.count&&task.directory_count&&save_command())batch_tasks_launch(task_owner,task.id);
}
static void run_selected_step(void){
    if(!step_available(selected_step))return;
    if(!save_command())return;
    task_queue.active_id=task.id;
    if(!begin_task(&task,selected_step)&&!running)task_queue.active_id=0;
}
static void cancel_selected_step(void){
    BatchActiveRun *active=active_step(selected_step);
    if(!active||active->cancelling||cancel_requested)return;
    active->cancelling=TRUE;SetEvent(active->cancel);
    statuses[selected_step]=BATCH_CANCELLING;refresh_row(selected_step);enable_step();
    set_summary(nova_text(L"正在取消选中的子任务，其他子任务继续执行。",L"Cancelling the selected subtask; other subtasks continue."));
}
static void add_shortcut(void){
    if(running||!collection.count||!save_command())return;
    SendMessageW(task_owner,WM_NOVA_BATCH_SHORTCUT,0,(LPARAM)&task);
}
static void add_directory(void){
    if(whole_task_running()||cancel_requested||!collection.count||!save_command())return;
    long long task_id=task.id;
    BROWSEINFOW browse={0};browse.hwndOwner=window;browse.lpszTitle=nova_text(L"选择命令的执行目录",L"Choose a working folder for the command");browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);if(!item)return;
    wchar_t directory[BATCH_DIRECTORY_CAP];BOOL resolved=SHGetPathFromIDListW(item,directory);CoTaskMemFree(item);if(!resolved)return;
    if(!window||task.id!=task_id||whole_task_running()||cancel_requested)return;
    int result=batch_task_add_directory(&task,directory);
    if(result==1){
        lstrcpyW(task.commands[task.directory_count-1],task.command);
        statuses[task.directory_count-1]=BATCH_WAITING;
        if(!save_current(&task)){batch_task_remove_directory(&task,task.directory_count-1);MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);}
        else{selected_step=task.directory_count-1;set_summary(nova_text(L"子任务已添加，可在下方编辑独立命令。",L"Subtask added. Edit its command below."));}
        refresh_list();enable_editor();edit_step();
    }
    else if(result==0)MessageBoxW(window,nova_text(L"这个目录已经在任务列表中。",L"This folder is already in the task."),nova_text(L"未添加目录",L"Folder not added"),MB_OK|MB_ICONINFORMATION);
    else MessageBoxW(window,nova_text(L"无法添加：任务最多包含 24 个目录，且路径不能超过 259 个字符。",L"Unable to add: a task supports up to 24 folders and paths up to 259 characters."),nova_text(L"未添加目录",L"Folder not added"),MB_OK|MB_ICONWARNING);
}
static void remove_directory(void){
    if(!step_available(selected_step)||!save_command())return;
    int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected<0)return;
    BatchTask previous=task;
    if(batch_task_remove_directory(&task,selected)){
        if(!save_current(&task)){task=previous;MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);}
        else{for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].worker&&active_runs[i].row>selected)active_runs[i].row--;
            for(int i=selected;i<task.directory_count;i++){statuses[i]=statuses[i+1];exit_codes[i]=exit_codes[i+1];errors[i]=errors[i+1];lstrcpynW(outputs[i],outputs[i+1],BATCH_OUTPUT_CAP);}statuses[task.directory_count]=BATCH_WAITING;exit_codes[task.directory_count]=errors[task.directory_count]=0;outputs[task.directory_count][0]=0;set_summary(nova_text(L"目录已移除；磁盘内容未更改。",L"Folder removed; files on disk were not changed."));}
        selected_step=-1;refresh_list();enable_editor();
    }
}
static void new_task(void){
    if(running||collection.count>=BATCH_TASK_LIMIT||!save_command())return;
    int previous=selected_task;selected_task=collection.count++;
    ZeroMemory(&task,sizeof(task));swprintf(task.name,BATCH_NAME_CAP,nova_text(L"任务 %d",L"Task %d"),collection.count);
    lstrcpyW(task.command,L"git pull origin main");
    if(!store_save_batch_tasks(&collection)){collection.count--;selected_task=previous;MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return;}
    refresh_tasks();show_task();SetFocus(name_edit);SendMessageW(name_edit,EM_SETSEL,0,-1);
}
static void delete_task(void){
    if(running||!collection.count)return;
    if(MessageBoxW(window,nova_text(L"删除选中的任务配置？目录和文件会保留。",L"Delete this task configuration? Folders and files will remain."),nova_text(L"删除任务",L"Delete task"),MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    BatchTaskList *previous=malloc(sizeof(*previous));if(!previous)return;*previous=collection;
    memmove(&collection.tasks[selected_task],&collection.tasks[selected_task+1],(size_t)(collection.count-selected_task-1)*sizeof(BatchTask));collection.count--;
    if(!store_save_batch_tasks(&collection)){collection=*previous;MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);}
    else{if(selected_task>=collection.count)selected_task=collection.count?collection.count-1:0;if(!collection.count)ZeroMemory(&task,sizeof(task));}
    free(previous);refresh_tasks();show_task();if(task_owner)PostMessageW(task_owner,WM_NOVA_BATCH_CHANGED,0,0);
}
static void close_manager(void){
    if(!save_command())return;
    DestroyWindow(window);if(!running&&task_queue.count)start_next_task();
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_BATCH_RESUME:if(task_queue.count)start_next_task();return 0;
    case WM_CREATE:{
        window=hwnd;HDC dc=GetDC(hwnd);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(hwnd,dc);
        body_font=CreateFontW(-bpx(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");title_font=CreateFontW(-bpx(24),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        background=CreateSolidBrush(BG);panel=CreateSolidBrush(PANEL);
        shortcut_button=make_button(nova_text(L"添加到工作区…",L"Add to workspace…"),ID_TASK_SHORTCUT);
        task_list=CreateWindowExW(0,L"LISTBOX",nova_text(L"已保存的任务",L"Saved tasks"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,0,10,10,hwnd,(HMENU)ID_TASKS,GetModuleHandleW(NULL),NULL);
        name_label=CreateWindowExW(0,L"STATIC",nova_text(L"名称",L"Name"),WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        name_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",task.name,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,hwnd,(HMENU)2111,GetModuleHandleW(NULL),NULL);
        mode_label=CreateWindowExW(0,L"STATIC",nova_text(L"执行模式",L"Execution"),WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        mode_combo=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,10,100,hwnd,(HMENU)2112,GetModuleHandleW(NULL),NULL);
        HWND fields[]={task_list,name_label,name_edit,mode_label,mode_combo};
        for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)SendMessageW(fields[i],WM_SETFONT,(WPARAM)body_font,TRUE);
        SendMessageW(name_edit,EM_SETLIMITTEXT,BATCH_NAME_CAP-1,0);
        new_task_button=make_button(nova_text(L"新建任务",L"New task"),ID_TASK_NEW);delete_task_button=make_button(nova_text(L"删除任务",L"Delete task"),ID_TASK_DELETE);
        command_label=CreateWindowExW(0,L"STATIC",nova_text(L"命令",L"Command"),WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        command_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",task.command,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,hwnd,(HMENU)2106,GetModuleHandleW(NULL),NULL);
        SendMessageW(command_label,WM_SETFONT,(WPARAM)body_font,TRUE);SendMessageW(command_edit,WM_SETFONT,(WPARAM)body_font,TRUE);
        SendMessageW(command_edit,EM_SETLIMITTEXT,BATCH_COMMAND_CAP-1,0);
        save_button=make_button(nova_text(L"保存命令",L"Save command"),ID_BATCH_SAVE);
        EnableWindow(command_edit,!running);EnableWindow(save_button,!running);
        list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,nova_text(L"批量任务目录",L"Batch task folders"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,0,0,10,10,hwnd,(HMENU)(INT_PTR)ID_BATCH_LIST,GetModuleHandleW(NULL),NULL);
        SendMessageW(list,WM_SETFONT,(WPARAM)body_font,TRUE);ListView_SetBkColor(list,PANEL);ListView_SetTextBkColor(list,PANEL);ListView_SetTextColor(list,TEXT);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SetWindowTheme(list,L"DarkMode_Explorer",NULL);
        /* A private empty small-image list gives native rows comfortable height. */
        row_height_images=ImageList_Create(1,bpx(34),ILC_COLOR32,1,0);if(row_height_images)ListView_SetImageList(list,row_height_images,LVSIL_SMALL);
        SetWindowSubclass(list,folder_list_proc,1,0);
        LVCOLUMNW column={0};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=bpx(440);column.pszText=(LPWSTR)nova_text(L"目录",L"Folder");ListView_InsertColumn(list,0,&column);column.cx=bpx(112);column.pszText=(LPWSTR)nova_text(L"状态",L"Status");ListView_InsertColumn(list,1,&column);
        add_button=make_button(nova_text(L"添加目录",L"Add folder"),ID_BATCH_ADD);remove_button=make_button(nova_text(L"移除",L"Remove"),ID_BATCH_REMOVE);run_button=make_button(nova_text(L"一键执行",L"Run task"),ID_BATCH_RUN);close_button=make_button(nova_text(L"关闭",L"Close"),ID_BATCH_CLOSE);
        run_step_button=make_button(L"",ID_STEP_RUN);cancel_step_button=make_button(L"",ID_STEP_CANCEL);
        edit_button=make_button(L"",ID_STEP_EDIT);apply_button=make_button(L"",ID_STEP_APPLY);browse_button=make_button(L"",ID_STEP_BROWSE);
        directory_label=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        directory_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,hwnd,(HMENU)ID_STEP_DIRECTORY,GetModuleHandleW(NULL),NULL);
        step_command_label=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,10,10,hwnd,NULL,GetModuleHandleW(NULL),NULL);
        step_command_edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,10,10,hwnd,(HMENU)ID_STEP_COMMAND,GetModuleHandleW(NULL),NULL);
        HWND step_fields[]={directory_label,directory_edit,step_command_label,step_command_edit};
        for(unsigned i=0;i<sizeof(step_fields)/sizeof(step_fields[0]);i++)SendMessageW(step_fields[i],WM_SETFONT,(WPARAM)body_font,TRUE);
        SendMessageW(directory_edit,EM_SETLIMITTEXT,BATCH_DIRECTORY_CAP-1,0);SendMessageW(step_command_edit,EM_SETLIMITTEXT,BATCH_COMMAND_CAP-1,0);
        column.pszText=(LPWSTR)nova_text(L"状态",L"Status");ListView_InsertColumn(list,2,&column);
        selected_step=-1;
        if(!summary_text[0])set_summary(task.directory_count?nova_text(L"准备就绪。",L"Ready."):nova_text(L"设置命令并添加执行目录。",L"Set a command and add working folders."));
        refresh_tasks();refresh_list();EnableWindow(add_button,!running);localize();enable_editor();layout();BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));return 0;
    }
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=bpx(1000);info->ptMinTrackSize.y=bpx(720);return 0;}
    case WM_SIZE:layout();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd,&paint);paint_window(dc);EndPaint(hwnd,&paint);return 0;}
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,BG);return (LRESULT)background;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,PANEL);return (LRESULT)panel;
    case WM_MEASUREITEM:if(((MEASUREITEMSTRUCT*)lp)->CtlID==ID_TASKS){((MEASUREITEMSTRUCT*)lp)->itemHeight=bpx(44);return TRUE;}if(((MEASUREITEMSTRUCT*)lp)->CtlID==2112){((MEASUREITEMSTRUCT*)lp)->itemHeight=bpx(28);return TRUE;}break;
    case WM_DRAWITEM:{DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT*)lp;
        if(draw->CtlID==ID_TASKS){
            HBRUSH brush=CreateSolidBrush(draw->itemState&ODS_SELECTED?ACCENT:PANEL);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);
            if(draw->itemID!=(UINT)-1){wchar_t label[BATCH_NAME_CAP];SendMessageW(task_list,LB_GETTEXT,draw->itemID,(LPARAM)label);RECT area=draw->rcItem;area.left+=bpx(12);area.right-=bpx(8);draw_text(draw->hDC,label,area,body_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);}
            if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);
            return TRUE;
        }
        if(draw->CtlID==2112){
            FillRect(draw->hDC,&draw->rcItem,panel);RECT area=draw->rcItem;area.left+=bpx(6);
            const wchar_t *label=draw->itemID==BATCH_PARALLEL?nova_text(L"同时执行（所有目录）",L"Parallel (all folders)"):nova_text(L"顺序执行（逐个目录）",L"Sequential (one folder at a time)");
            draw_text(draw->hDC,label,area,body_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);return TRUE;
        }
        if(draw->CtlID<ID_BATCH_ADD||draw->CtlID>ID_STEP_CANCEL)break;
        BOOL primary=draw->CtlID==ID_BATCH_RUN||draw->CtlID==ID_BATCH_SAVE;COLORREF fill=primary?ACCENT:PANEL;if(hover_button==draw->hwndItem||draw->itemState&ODS_SELECTED)fill=primary?RGB(76,103,159):RGB(39,51,71);if(draw->itemState&ODS_DISABLED)fill=RGB(30,38,51);
        HBRUSH brush=CreateSolidBrush(fill);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);wchar_t label[80];GetWindowTextW(draw->hwndItem,label,80);draw_text(draw->hDC,label,draw->rcItem,body_font,(draw->itemState&ODS_DISABLED)?RGB(112,128,150):TEXT,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
        if(draw->itemState&ODS_FOCUS){RECT focus=draw->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(draw->hDC,&focus);}return TRUE;}
    case WM_COMMAND:
        if(LOWORD(wp)==ID_STEP_CANCEL){cancel_selected_step();return 0;}
        if(!refreshing&&((HIWORD(wp)==EN_CHANGE&&GetFocus()==(HWND)lp)||(LOWORD(wp)==2112&&HIWORD(wp)==CBN_SELCHANGE)))set_summary(nova_text(L"有未保存的修改。",L"Unsaved changes."));
        if(LOWORD(wp)==ID_TASKS&&HIWORD(wp)==LBN_SELCHANGE){
            int next=(int)SendMessageW(task_list,LB_GETCURSEL,0,0);
            if(!running&&next>=0&&next<collection.count&&next!=selected_task){
                if(save_command()){selected_task=next;refresh_tasks();show_task();}
                else SendMessageW(task_list,LB_SETCURSEL,selected_task,0);
            }return 0;
        }
        switch(LOWORD(wp)){case ID_STEP_RUN:run_selected_step();return 0;case ID_STEP_EDIT:edit_step();return 0;case ID_STEP_APPLY:save_command();return 0;case ID_STEP_BROWSE:browse_step();return 0;case ID_TASK_SHORTCUT:add_shortcut();return 0;case ID_TASK_NEW:new_task();return 0;case ID_TASK_DELETE:delete_task();return 0;case ID_BATCH_SAVE:save_command();return 0;case ID_BATCH_ADD:add_directory();return 0;case ID_BATCH_REMOVE:remove_directory();return 0;case ID_BATCH_RUN:start_or_cancel();return 0;case ID_BATCH_CLOSE:close_manager();return 0;}break;
    case WM_NOTIFY:{NMHDR *notice=(NMHDR*)lp;
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==NM_CUSTOMDRAW){
            NMLVCUSTOMDRAW *draw=(NMLVCUSTOMDRAW*)lp;
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){draw->clrText=TEXT;draw->clrTextBk=ListView_GetItemState(list,(int)draw->nmcd.dwItemSpec,LVIS_SELECTED)?ACCENT:PANEL;draw->nmcd.uItemState&=~CDIS_SELECTED;return CDRF_NEWFONT;}
        }
        if(notice->hwndFrom==ListView_GetHeader(list)&&notice->code==NM_CUSTOMDRAW){
            NMCUSTOMDRAW *draw=(NMCUSTOMDRAW*)lp;
            if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->dwDrawStage==CDDS_ITEMPREPAINT){
                FillRect(draw->hdc,&draw->rc,panel);RECT area=draw->rc;area.left+=bpx(8);
                draw_text(draw->hdc,draw->dwItemSpec==2?nova_text(L"状态",L"Status"):draw->dwItemSpec==1?nova_text(L"命令",L"Command"):nova_text(L"工作目录",L"Working folder"),area,body_font,TEXT,DT_SINGLELINE|DT_VCENTER);return CDRF_SKIPDEFAULT;
            }
        }
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==LVN_ITEMCHANGING&&!refreshing){
            NMLISTVIEW *change=(NMLISTVIEW*)lp;
            if((change->uChanged&LVIF_STATE)&&(change->uOldState&LVIS_SELECTED)&&!(change->uNewState&LVIS_SELECTED)&&!save_command())return TRUE;
        }
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==LVN_ITEMCHANGED&&!refreshing){
            int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected!=selected_step){selected_step=selected;show_step();}show_result_summary(selected);
        }
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==NM_DBLCLK){int index=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(index>=0&&(statuses[index]==BATCH_FAILED||statuses[index]==BATCH_CANCELLED||statuses[index]==BATCH_SUCCEEDED))show_result_dialog(index);else edit_step();return 0;}
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==LVN_GETEMPTYMARKUP){NMLVEMPTYMARKUP *empty=(NMLVEMPTYMARKUP*)lp;empty->dwFlags=EMF_CENTERED;lstrcpynW(empty->szMarkup,nova_text(L"点击“新增子任务”，设置工作目录和命令。",L"Add a subtask, then set its folder and command."),sizeof(empty->szMarkup)/sizeof(empty->szMarkup[0]));return TRUE;}
        return 0;}
    case WM_CLOSE:close_manager();return 0;
    case WM_DESTROY:if(row_height_images){ListView_SetImageList(list,NULL,LVSIL_SMALL);ImageList_Destroy(row_height_images);row_height_images=NULL;}selected_step=-1;run_step_button=cancel_step_button=NULL;edit_button=apply_button=browse_button=directory_edit=step_command_edit=directory_label=step_command_label=NULL;shortcut_button=command_edit=command_label=save_button=task_list=name_edit=mode_combo=new_task_button=delete_task_button=name_label=mode_label=NULL;window=NULL;list=NULL;add_button=remove_button=run_button=close_button=NULL;hover_button=NULL;DeleteObject(body_font);DeleteObject(title_font);DeleteObject(background);DeleteObject(panel);body_font=title_font=NULL;background=panel=NULL;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

BOOL batch_tasks_open(HWND owner){
    task_owner=owner;
    if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return TRUE;}
    if(!loaded){if(!store_load_batch_tasks(&collection)){MessageBoxW(owner,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}loaded=TRUE;}
    WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(NULL);wc.lpfnWndProc=window_proc;wc.lpszClassName=BATCH_CLASS;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&wc);
    HDC dc=GetDC(owner);if(dc){dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);}
    RECT owner_rect;GetWindowRect(owner,&owner_rect);int width=MulDiv(1120,dpi,96),height=MulDiv(800,dpi,96),x=owner_rect.left+(owner_rect.right-owner_rect.left-width)/2,y=owner_rect.top+(owner_rect.bottom-owner_rect.top-height)/2;
    MONITORINFO monitor={0};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor)){
        RECT work=monitor.rcWork;if(width>work.right-work.left)width=work.right-work.left;if(height>work.bottom-work.top)height=work.bottom-work.top;
        x=work.left+(work.right-work.left-width)/2;y=work.top+(work.bottom-work.top-height)/2;
    }
    HWND created=CreateWindowExW(WS_EX_CONTROLPARENT,BATCH_CLASS,nova_text(L"批量任务",L"Batch tasks"),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX,x,y,width,height,owner,NULL,GetModuleHandleW(NULL),NULL);
    if(!created)return FALSE;
    HICON large=(HICON)SendMessageW(owner,WM_GETICON,ICON_BIG,0),small=(HICON)SendMessageW(owner,WM_GETICON,ICON_SMALL,0);
    if(large)SendMessageW(created,WM_SETICON,ICON_BIG,(LPARAM)large);
    if(small)SendMessageW(created,WM_SETICON,ICON_SMALL,(LPARAM)small);
    ShowWindow(created,SW_SHOW);UpdateWindow(created);return TRUE;
}

BOOL batch_tasks_lookup(long long id,BatchTask *value){
    if(id<=0||!value)return FALSE;
    if(!loaded){if(!store_load_batch_tasks(&collection))return FALSE;loaded=TRUE;}
    for(int i=0;i<collection.count;i++)if(collection.tasks[i].id==id){*value=collection.tasks[i];return TRUE;}
    return FALSE;
}
BOOL batch_tasks_launch(HWND owner,long long id){
    BatchTask value;
    if(!batch_tasks_lookup(id,&value))return FALSE;
    if(window&&!save_command())return FALSE;
    if(!batch_tasks_lookup(id,&value)||!value.directory_count)return FALSE;
    if(!window&&!batch_tasks_open(owner))return FALSE;
    task_owner=owner;
    int queued=batch_task_queue_add(&task_queue,&value);
    if(queued<0)return FALSE;
    if(!running)return start_next_task();
    wchar_t summary[256];swprintf(summary,256,nova_text(L"正在执行任务；另有 %d 条等待。重复启动会自动忽略。",L"Task running; %d more queued. Duplicate launches are ignored."),task_queue.count);set_summary(summary);
    return TRUE;
}
BOOL batch_tasks_busy(void){return running||task_queue.count>0;}
BOOL batch_tasks_handle_event(LPARAM parameter){
    BatchRunEvent *event=(BatchRunEvent*)parameter;if(!event)return FALSE;
    if(event->slot<0||event->slot>=BATCH_DIRECTORY_LIMIT||!active_runs[event->slot].worker){free(event);return FALSE;}
    BatchActiveRun *active=&active_runs[event->slot];
    if(event->type!=EVENT_FINISHED&&active->row>=0)event->index=active->row;
    if(event->index>=0&&event->index<task.directory_count){
        if(event->type==EVENT_STARTED){
            statuses[event->index]=active->cancelling?BATCH_CANCELLING:BATCH_RUNNING;wchar_t value[320];
            if(event->total==1)swprintf(value,320,nova_text(L"正在执行子任务 %d：%ls",L"Running subtask %d: %ls"),event->index+1,task.directories[event->index]);
            else swprintf(value,320,nova_text(L"正在执行 %d/%d：%ls",L"Running %d/%d: %ls"),event->index+1,task.directory_count,task.directories[event->index]);
            if(!active->cancelling)set_summary(value);
        }
        else if(event->type==EVENT_SKIPPED){
            statuses[event->index]=active->row>=0&&active->cancelling?BATCH_CANCELLED:BATCH_SKIPPED;
            if(statuses[event->index]==BATCH_CANCELLED)errors[event->index]=ERROR_CANCELLED;
        }
        else if(event->type==EVENT_CANCELLED){statuses[event->index]=BATCH_CANCELLED;exit_codes[event->index]=event->exit_code;errors[event->index]=event->error;lstrcpynW(outputs[event->index],event->output,BATCH_OUTPUT_CAP);}
        else if(event->type==EVENT_RESULT){statuses[event->index]=event->error||event->exit_code?BATCH_FAILED:BATCH_SUCCEEDED;exit_codes[event->index]=event->exit_code;errors[event->index]=event->error;lstrcpynW(outputs[event->index],event->output,BATCH_OUTPUT_CAP);}
        refresh_row(event->index);
    }
    if(event->type==EVENT_FINISHED){
        if(active->row>=0&&active->cancelling){event->cancelled+=event->skipped;event->skipped=0;}
        run_totals.success+=event->success;run_totals.failed+=event->failed;run_totals.cancelled+=event->cancelled;run_totals.skipped+=event->skipped;
        CloseHandle(active->worker);CloseHandle(active->cancel);ZeroMemory(active,sizeof(*active));running--;
        if(!running){task_queue.active_id=0;cancel_requested=FALSE;}
        if(window){localize();enable_editor();
            wchar_t summary[256];
            if(running)swprintf(summary,256,nova_text(L"一个子任务已结束，仍有 %d 个子任务执行中。",L"One subtask finished; %d subtasks still running."),running);
            else swprintf(summary,256,nova_text(L"完成：成功 %d，失败 %d，取消 %d，跳过 %d。双击已完成项可查看输出。",L"Finished: %d succeeded, %d failed, %d cancelled, %d skipped. Double-click completed rows for output."),run_totals.success,run_totals.failed,run_totals.cancelled,run_totals.skipped);
            set_summary(summary);}
    }
    BOOL finished=event->type==EVENT_FINISHED;
    free(event);if(finished&&!running&&task_queue.count)start_next_task();return TRUE;
}
void batch_tasks_language_changed(void){localize();}
void batch_tasks_shutdown(void){
    batch_task_queue_cancel(&task_queue);cancel_runs();if(window)DestroyWindow(window);
    /* Workers own their cancel handle until they return; join before closing it. */
    for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++)if(active_runs[i].worker){
        WaitForSingleObject(active_runs[i].worker,INFINITE);CloseHandle(active_runs[i].worker);CloseHandle(active_runs[i].cancel);
    }
    ZeroMemory(active_runs,sizeof(active_runs));running=cancel_requested=0;task_queue.active_id=0;
    MSG event;while(PeekMessageW(&event,task_owner,WM_NOVA_BATCH_EVENT,WM_NOVA_BATCH_EVENT,PM_REMOVE))free((void*)event.lParam);
}
HWND batch_tasks_window(void){return window;}
