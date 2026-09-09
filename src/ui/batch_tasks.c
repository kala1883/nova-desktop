#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
static HWND shortcut_button,task_owner;
static BatchTaskQueue task_queue;
static BOOL start_next_task(void);
static HWND task_list,name_edit,mode_combo,new_task_button,delete_task_button,name_label,mode_label;
static BatchTaskList collection;
static int selected_task;
#define task (collection.tasks[selected_task])
static HWND command_edit,save_button,command_label;

enum { BATCH_WAITING, BATCH_RUNNING, BATCH_SUCCEEDED, BATCH_FAILED, BATCH_SKIPPED };
enum { EVENT_STARTED=1, EVENT_RESULT, EVENT_SKIPPED, EVENT_FINISHED };

typedef struct {
    HWND owner;
    HANDLE cancel;
    BatchTask snapshot;
    void *completion;

} BatchRunContext;

typedef struct {
    int type,index,success,failed,skipped,total;
    DWORD exit_code,error;
} BatchRunEvent;

static HWND window,list,add_button,remove_button,run_button,close_button,hover_button;
static HFONT body_font,title_font;
static HBRUSH background,panel;

static int loaded,running,cancel_requested,statuses[BATCH_DIRECTORY_LIMIT];
static DWORD exit_codes[BATCH_DIRECTORY_LIMIT],errors[BATCH_DIRECTORY_LIMIT];
static HANDLE worker,cancel_event;
static int dpi=96;
static wchar_t summary_text[256];
static const COLORREF BG=RGB(14,19,29),PANEL=RGB(22,29,42),TEXT=RGB(236,241,249),MUTED=RGB(167,181,202),ACCENT=RGB(53,77,122);

static int bpx(int value){return MulDiv(value,dpi,96);}
static const wchar_t *status_text(int status){
    switch(status){
    case BATCH_RUNNING:return nova_text(L"执行中",L"Running");
    case BATCH_SUCCEEDED:return nova_text(L"成功",L"Succeeded");
    case BATCH_FAILED:return nova_text(L"失败",L"Failed");
    case BATCH_SKIPPED:return nova_text(L"已跳过",L"Skipped");
    default:return nova_text(L"等待",L"Waiting");
    }
}
static void set_summary(const wchar_t *value){lstrcpynW(summary_text,value,256);if(window)InvalidateRect(window,NULL,FALSE);}
static void refresh_row(int index){
    if(!list||index<0||index>=task.directory_count)return;
    wchar_t value[96];lstrcpynW(value,status_text(statuses[index]),96);
    if(statuses[index]==BATCH_FAILED){
        if(errors[index])swprintf(value,96,nova_text(L"失败（Windows %lu）",L"Failed (Windows %lu)"),errors[index]);
        else swprintf(value,96,nova_text(L"失败（退出码 %lu）",L"Failed (exit %lu)"),exit_codes[index]);
    }
    ListView_SetItemText(list,index,1,value);
}
static void refresh_list(void){
    if(!list)return;
    ListView_DeleteAllItems(list);
    for(int i=0;i<task.directory_count;i++){
        LVITEMW item={0};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=task.directories[i];ListView_InsertItem(list,&item);refresh_row(i);
    }
    EnableWindow(remove_button,!running&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);EnableWindow(run_button,task.directory_count>0&&(!running||!cancel_requested));
}
static void localize(void){
    if(!window)return;
    SetWindowTextW(window,nova_text(L"批量任务",L"Batch tasks"));
    SetWindowTextW(shortcut_button,nova_text(L"添加到工作区…",L"Add to workspace…"));
    SetWindowTextW(command_label,nova_text(L"命令",L"Command"));SetWindowTextW(save_button,nova_text(L"保存任务",L"Save task"));
    SetWindowTextW(new_task_button,nova_text(L"新建任务",L"New task"));SetWindowTextW(delete_task_button,nova_text(L"删除任务",L"Delete task"));
    SetWindowTextW(name_label,nova_text(L"名称",L"Name"));SetWindowTextW(mode_label,nova_text(L"执行模式",L"Execution"));
    int mode=(int)SendMessageW(mode_combo,CB_GETCURSEL,0,0);
    SendMessageW(mode_combo,CB_RESETCONTENT,0,0);
    SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"顺序执行（逐个目录）",L"Sequential (one folder at a time)"));
    SendMessageW(mode_combo,CB_ADDSTRING,0,(LPARAM)nova_text(L"同时执行（所有目录）",L"Parallel (all folders)"));
    SendMessageW(mode_combo,CB_SETCURSEL,mode<0?task.mode:mode,0);
    SetWindowTextW(task_list,nova_text(L"已保存的任务",L"Saved tasks"));SetWindowTextW(list,nova_text(L"批量任务目录",L"Batch task folders"));
    if(!running)set_summary(nova_text(L"选择任务后可单独一键执行。",L"Select a task to run it individually."));
    SetWindowTextW(add_button,nova_text(L"添加目录",L"Add folder"));SetWindowTextW(remove_button,nova_text(L"移除",L"Remove"));
    SetWindowTextW(run_button,cancel_requested?nova_text(L"正在取消…",L"Cancelling…"):running?nova_text(L"取消待执行",L"Cancel pending"):nova_text(L"一键执行",L"Run task"));SetWindowTextW(close_button,nova_text(L"关闭",L"Close"));
    LVCOLUMNW column={0};column.mask=LVCF_TEXT;column.pszText=(LPWSTR)nova_text(L"目录",L"Folder");ListView_SetColumn(list,0,&column);column.pszText=(LPWSTR)nova_text(L"状态",L"Status");ListView_SetColumn(list,1,&column);
    for(int i=0;i<task.directory_count;i++)refresh_row(i);
    InvalidateRect(window,NULL,TRUE);
}
static void layout(void){
    if(!window)return;
    RECT client;GetClientRect(window,&client);int margin=bpx(24),gap=bpx(8),button_height=bpx(38);
    int left=bpx(236),width=client.right-left-margin;
    MoveWindow(shortcut_button,client.right-margin-bpx(168),bpx(12),bpx(168),button_height,TRUE);
    MoveWindow(task_list,margin,bpx(57),bpx(188),client.bottom-bpx(125),TRUE);
    MoveWindow(new_task_button,margin,client.bottom-bpx(54),bpx(90),button_height,TRUE);
    MoveWindow(delete_task_button,margin+bpx(98),client.bottom-bpx(54),bpx(90),button_height,TRUE);
    MoveWindow(name_label,left,bpx(57),bpx(96),bpx(32),TRUE);
    MoveWindow(name_edit,left+bpx(100),bpx(57),width-bpx(100),bpx(32),TRUE);
    MoveWindow(command_label,left,bpx(101),bpx(96),bpx(32),TRUE);
    MoveWindow(command_edit,left+bpx(100),bpx(101),width-bpx(100),bpx(32),TRUE);
    MoveWindow(mode_label,left,bpx(145),bpx(96),bpx(32),TRUE);
    MoveWindow(mode_combo,left+bpx(100),bpx(145),width-bpx(224),bpx(180),TRUE);
    MoveWindow(save_button,client.right-margin-bpx(116),bpx(143),bpx(116),button_height,TRUE);
    MoveWindow(list,left,bpx(224),width,client.bottom-bpx(324),TRUE);
    int y=client.bottom-bpx(54),small=bpx(90),run_width=bpx(112);
    MoveWindow(add_button,left,y,small,button_height,TRUE);MoveWindow(remove_button,left+small+gap,y,small,button_height,TRUE);
    MoveWindow(close_button,client.right-margin-small,y,small,button_height,TRUE);MoveWindow(run_button,client.right-margin-small-gap-run_width,y,run_width,button_height,TRUE);
    ListView_SetColumnWidth(list,1,bpx(152));ListView_SetColumnWidth(list,0,width-bpx(156));
}
static void draw_text(HDC dc,const wchar_t *value,RECT area,HFONT font,COLORREF color,UINT format){
    HFONT old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value,-1,&area,format|DT_NOPREFIX);SelectObject(dc,old);
}
static void paint_window(HDC dc){
    RECT client;GetClientRect(window,&client);FillRect(dc,&client,background);
    RECT title={bpx(24),bpx(17),client.right-bpx(24),bpx(49)};draw_text(dc,nova_text(L"批量任务",L"Batch tasks"),title,title_font,TEXT,DT_SINGLELINE|DT_VCENTER);
    RECT help={bpx(236),bpx(188),client.right-bpx(24),bpx(213)};draw_text(dc,nova_text(L"使用 cmd 命令语法；取消只跳过尚未开始的目录。",L"Use cmd syntax. Cancel skips folders that have not started."),help,body_font,MUTED,DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT summary={bpx(236),client.bottom-bpx(92),client.right-bpx(24),client.bottom-bpx(64)};draw_text(dc,summary_text,summary,body_font,MUTED,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
}
static LRESULT CALLBACK button_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR subclass_id,DWORD_PTR data){
    (void)subclass_id;(void)data;
    if(message==WM_MOUSEMOVE&&hover_button!=hwnd){hover_button=hwnd;TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);InvalidateRect(hwnd,NULL,TRUE);}
    if(message==WM_MOUSELEAVE){if(hover_button==hwnd)hover_button=NULL;InvalidateRect(hwnd,NULL,TRUE);}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,button_proc,1);
    return DefSubclassProc(hwnd,message,wp,lp);
}
static LRESULT CALLBACK folder_list_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    (void)id;(void)data;
    if(message==WM_NOTIFY&&((NMHDR*)lp)->code==NM_CUSTOMDRAW)return SendMessageW(window,WM_NOTIFY,wp,lp);
    if(message==WM_PAINT){
        LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
        if(!ListView_GetItemCount(hwnd)){
            RECT area,header;GetClientRect(hwnd,&area);GetWindowRect(ListView_GetHeader(hwnd),&header);area.top=header.bottom-header.top;
            HDC dc=GetDC(hwnd);FillRect(dc,&area,panel);InflateRect(&area,-bpx(16),-bpx(8));
            draw_text(dc,nova_text(L"添加目录后即可批量执行命令。",L"Add folders to run your command in each one."),area,body_font,MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);ReleaseDC(hwnd,dc);
        }
        return result;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,folder_list_proc,1);
    return DefSubclassProc(hwnd,message,wp,lp);
}
static HWND make_button(const wchar_t *label,int id){
    HWND value=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(value,WM_SETFONT,(WPARAM)body_font,TRUE);SetWindowSubclass(value,button_proc,1,0);return value;
}
static void post_event(HWND owner,BatchRunEvent *event){if(!PostMessageW(owner,WM_NOVA_BATCH_EVENT,0,(LPARAM)event))free(event);}
static void run_progress(void *parameter,int type,int index,DWORD code,DWORD error){
    BatchRunContext *context=parameter;
    BatchRunEvent *event=calloc(1,sizeof(*event));if(!event)return;
    event->type=type;event->index=index;event->exit_code=code;event->error=error;post_event(context->owner,event);
}
static DWORD WINAPI run_worker(void *parameter){
    BatchRunContext *context=parameter;
    BatchResult result=batch_runner_task(&context->snapshot,context->cancel,run_progress,context,NULL);
    BatchRunEvent *done=context->completion;
    if(done){done->type=EVENT_FINISHED;done->success=result.success;done->failed=result.failed;done->skipped=result.skipped;done->total=context->snapshot.directory_count;post_event(context->owner,done);}
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
    HWND controls[]={command_edit,save_button,name_edit,mode_combo,add_button,delete_task_button,shortcut_button};
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);i++)EnableWindow(controls[i],available);
    EnableWindow(task_list,!running);EnableWindow(new_task_button,!running&&collection.count<BATCH_TASK_LIMIT);
    EnableWindow(run_button,collection.count&&task.directory_count&&(!running||!cancel_requested));
}
static void show_task(void){
    SetWindowTextW(name_edit,collection.count?task.name:L"");
    SetWindowTextW(command_edit,collection.count?task.command:L"");
    SendMessageW(mode_combo,CB_SETCURSEL,task.mode,0);
    ZeroMemory(statuses,sizeof(statuses));ZeroMemory(exit_codes,sizeof(exit_codes));ZeroMemory(errors,sizeof(errors));
    refresh_list();enable_editor();set_summary(nova_text(L"选择任务后可单独一键执行。",L"Select a task to run it individually."));
}
static BOOL save_command(void){
    if(running||!collection.count)return TRUE;
    BatchTask next=task;GetWindowTextW(command_edit,next.command,BATCH_COMMAND_CAP);
    if(!next.command[wcsspn(next.command,L" \t\r\n")]){set_summary(nova_text(L"请输入要执行的命令。",L"Enter a command to run."));SetFocus(command_edit);return FALSE;}
    GetWindowTextW(name_edit,next.name,BATCH_NAME_CAP);
    next.mode=(int)SendMessageW(mode_combo,CB_GETCURSEL,0,0);
    if(!next.name[wcsspn(next.name,L" \t\r\n")]){set_summary(nova_text(L"请输入任务名称。",L"Enter a task name."));SetFocus(name_edit);return FALSE;}
    if(!save_current(&next)){MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    task=next;refresh_tasks();set_summary(nova_text(L"任务已保存。",L"Task saved."));return TRUE;
}
static BOOL begin_task(const BatchTask *snapshot){
    BatchRunContext *context=calloc(1,sizeof(*context));
    if(!context){MessageBoxW(window,nova_text(L"内存不足，无法创建批量任务快照。",L"Not enough memory to create the batch task snapshot."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->completion=calloc(1,sizeof(BatchRunEvent));
    if(!context->completion){free(context);MessageBoxW(window,nova_text(L"内存不足，无法启动任务。",L"Not enough memory to start the task."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->owner=task_owner;context->snapshot=*snapshot;
    cancel_event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!cancel_event){free(context->completion);free(context);MessageBoxW(window,nova_text(L"无法创建批量任务取消事件。",L"Unable to create the batch task cancellation event."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    context->cancel=cancel_event;
    for(int i=0;i<collection.count;i++)if(collection.tasks[i].id==snapshot->id){selected_task=i;break;}
    if(window){refresh_tasks();show_task();}
    for(int i=0;i<task.directory_count;i++){statuses[i]=BATCH_WAITING;exit_codes[i]=errors[i]=0;}
    running=TRUE;cancel_requested=FALSE;set_summary(nova_text(L"任务已开始…",L"Task started…"));refresh_list();
    SetWindowTextW(run_button,nova_text(L"取消待执行",L"Cancel pending"));EnableWindow(add_button,FALSE);EnableWindow(remove_button,FALSE);
    worker=CreateThread(NULL,0,run_worker,context,0,NULL);enable_editor();
    if(!worker){running=FALSE;CloseHandle(cancel_event);cancel_event=NULL;free(context->completion);free(context);localize();refresh_list();enable_editor();MessageBoxW(window,nova_text(L"无法创建批量任务工作线程。",L"Unable to create the batch task worker thread."),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}
    return TRUE;
}
static BOOL start_next_task(void){
    BatchTask snapshot;
    while(batch_task_queue_take(&task_queue,&snapshot)){
        if(begin_task(&snapshot))return TRUE;
        task_queue.active_id=0;
    }
    return FALSE;
}
static void start_or_cancel(void){
    if(running){
        batch_task_queue_cancel(&task_queue);cancel_requested=TRUE;SetEvent(cancel_event);
        SetWindowTextW(run_button,nova_text(L"正在取消…",L"Cancelling…"));EnableWindow(run_button,FALSE);return;
    }
    if(collection.count&&task.directory_count&&save_command())batch_tasks_launch(task_owner,task.id);
}
static void add_shortcut(void){
    if(running||!collection.count||!save_command())return;
    SendMessageW(task_owner,WM_NOVA_BATCH_SHORTCUT,0,(LPARAM)&task);
}
static void add_directory(void){
    if(running||!collection.count||!save_command())return;
    BROWSEINFOW browse={0};browse.hwndOwner=window;browse.lpszTitle=nova_text(L"选择命令的执行目录",L"Choose a working folder for the command");browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&browse);if(!item)return;
    wchar_t directory[BATCH_DIRECTORY_CAP];BOOL resolved=SHGetPathFromIDListW(item,directory);CoTaskMemFree(item);if(!resolved)return;
    int result=batch_task_add_directory(&task,directory);
    if(result==1){
        statuses[task.directory_count-1]=BATCH_WAITING;
        if(!save_current(&task)){batch_task_remove_directory(&task,task.directory_count-1);MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);}
        else set_summary(nova_text(L"目录已保存。",L"Folder saved."));
        refresh_list();
    }
    else if(result==0)MessageBoxW(window,nova_text(L"这个目录已经在任务列表中。",L"This folder is already in the task."),nova_text(L"未添加目录",L"Folder not added"),MB_OK|MB_ICONINFORMATION);
    else MessageBoxW(window,nova_text(L"无法添加：任务最多包含 24 个目录，且路径不能超过 259 个字符。",L"Unable to add: a task supports up to 24 folders and paths up to 259 characters."),nova_text(L"未添加目录",L"Folder not added"),MB_OK|MB_ICONWARNING);
}
static void remove_directory(void){
    if(running||!collection.count||!save_command())return;
    int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(selected<0)return;
    BatchTask previous=task;
    if(batch_task_remove_directory(&task,selected)){
        if(!save_current(&task)){task=previous;MessageBoxW(window,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);}
        else{for(int i=selected;i<task.directory_count;i++){statuses[i]=statuses[i+1];exit_codes[i]=exit_codes[i+1];errors[i]=errors[i+1];}statuses[task.directory_count]=BATCH_WAITING;exit_codes[task.directory_count]=errors[task.directory_count]=0;set_summary(nova_text(L"目录已移除；磁盘内容未更改。",L"Folder removed; files on disk were not changed."));}
        refresh_list();
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
static LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
    switch(message){
    case WM_CREATE:{
        window=hwnd;HDC dc=GetDC(hwnd);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(hwnd,dc);
        body_font=CreateFontW(-bpx(15),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");title_font=CreateFontW(-bpx(24),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        background=CreateSolidBrush(BG);panel=CreateSolidBrush(PANEL);
        shortcut_button=make_button(nova_text(L"添加到工作区…",L"Add to workspace…"),ID_TASK_SHORTCUT);
        task_list=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",nova_text(L"已保存的任务",L"Saved tasks"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY,0,0,10,10,hwnd,(HMENU)ID_TASKS,GetModuleHandleW(NULL),NULL);
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
        SetWindowSubclass(list,folder_list_proc,1,0);
        LVCOLUMNW column={0};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=bpx(440);column.pszText=(LPWSTR)nova_text(L"目录",L"Folder");ListView_InsertColumn(list,0,&column);column.cx=bpx(112);column.pszText=(LPWSTR)nova_text(L"状态",L"Status");ListView_InsertColumn(list,1,&column);
        add_button=make_button(nova_text(L"添加目录",L"Add folder"),ID_BATCH_ADD);remove_button=make_button(nova_text(L"移除",L"Remove"),ID_BATCH_REMOVE);run_button=make_button(nova_text(L"一键执行",L"Run task"),ID_BATCH_RUN);close_button=make_button(nova_text(L"关闭",L"Close"),ID_BATCH_CLOSE);
        if(!summary_text[0])set_summary(task.directory_count?nova_text(L"准备就绪。",L"Ready."):nova_text(L"设置命令并添加执行目录。",L"Set a command and add working folders."));
        refresh_tasks();refresh_list();EnableWindow(add_button,!running);localize();enable_editor();layout();BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));return 0;
    }
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=bpx(820);info->ptMinTrackSize.y=bpx(520);return 0;}
    case WM_SIZE:layout();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint;HDC dc=BeginPaint(hwnd,&paint);paint_window(dc);EndPaint(hwnd,&paint);return 0;}
    case WM_CTLCOLORSTATIC:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,BG);return (LRESULT)background;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:SetTextColor((HDC)wp,TEXT);SetBkColor((HDC)wp,PANEL);return (LRESULT)panel;
    case WM_MEASUREITEM:if(((MEASUREITEMSTRUCT*)lp)->CtlID==2112){((MEASUREITEMSTRUCT*)lp)->itemHeight=bpx(28);return TRUE;}break;
    case WM_DRAWITEM:{DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT*)lp;
        if(draw->CtlID==2112){
            FillRect(draw->hDC,&draw->rcItem,panel);RECT area=draw->rcItem;area.left+=bpx(6);
            const wchar_t *label=draw->itemID==BATCH_PARALLEL?nova_text(L"同时执行（所有目录）",L"Parallel (all folders)"):nova_text(L"顺序执行（逐个目录）",L"Sequential (one folder at a time)");
            draw_text(draw->hDC,label,area,body_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);return TRUE;
        }
        if(draw->CtlID<ID_BATCH_ADD||draw->CtlID>ID_TASK_SHORTCUT)break;
        BOOL primary=draw->CtlID==ID_BATCH_RUN;COLORREF fill=primary?ACCENT:PANEL;if(hover_button==draw->hwndItem||draw->itemState&ODS_SELECTED)fill=primary?RGB(76,103,159):RGB(39,51,71);if(draw->itemState&ODS_DISABLED)fill=RGB(30,38,51);
        HBRUSH brush=CreateSolidBrush(fill);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);wchar_t label[80];GetWindowTextW(draw->hwndItem,label,80);draw_text(draw->hDC,label,draw->rcItem,body_font,(draw->itemState&ODS_DISABLED)?RGB(112,128,150):TEXT,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
        if(draw->itemState&ODS_FOCUS){RECT focus=draw->rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(draw->hDC,&focus);}return TRUE;}
    case WM_COMMAND:
        if(LOWORD(wp)==ID_TASKS&&HIWORD(wp)==LBN_SELCHANGE){
            int next=(int)SendMessageW(task_list,LB_GETCURSEL,0,0);
            if(!running&&next>=0&&next<collection.count&&next!=selected_task){
                if(save_command()){selected_task=next;refresh_tasks();show_task();}
                else SendMessageW(task_list,LB_SETCURSEL,selected_task,0);
            }return 0;
        }
        switch(LOWORD(wp)){case ID_TASK_SHORTCUT:add_shortcut();return 0;case ID_TASK_NEW:new_task();return 0;case ID_TASK_DELETE:delete_task();return 0;case ID_BATCH_SAVE:save_command();return 0;case ID_BATCH_ADD:add_directory();return 0;case ID_BATCH_REMOVE:remove_directory();return 0;case ID_BATCH_RUN:start_or_cancel();return 0;case ID_BATCH_CLOSE:if(save_command())DestroyWindow(hwnd);return 0;}break;
    case WM_NOTIFY:{NMHDR *notice=(NMHDR*)lp;
        if(notice->hwndFrom==ListView_GetHeader(list)&&notice->code==NM_CUSTOMDRAW){
            NMCUSTOMDRAW *draw=(NMCUSTOMDRAW*)lp;
            if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->dwDrawStage==CDDS_ITEMPREPAINT){
                FillRect(draw->hdc,&draw->rc,panel);RECT area=draw->rc;area.left+=bpx(8);
                draw_text(draw->hdc,draw->dwItemSpec?nova_text(L"状态",L"Status"):nova_text(L"目录",L"Folder"),area,body_font,TEXT,DT_SINGLELINE|DT_VCENTER);return CDRF_SKIPDEFAULT;
            }
        }
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==LVN_ITEMCHANGED)EnableWindow(remove_button,!running&&ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);
        if(notice->idFrom==ID_BATCH_LIST&&notice->code==LVN_GETEMPTYMARKUP){NMLVEMPTYMARKUP *empty=(NMLVEMPTYMARKUP*)lp;empty->dwFlags=EMF_CENTERED;lstrcpynW(empty->szMarkup,nova_text(L"添加目录后即可批量执行命令。",L"Add folders to run your command in each one."),sizeof(empty->szMarkup)/sizeof(empty->szMarkup[0]));return TRUE;}
        return 0;}
    case WM_CLOSE:if(save_command())DestroyWindow(hwnd);return 0;
    case WM_DESTROY:shortcut_button=command_edit=command_label=save_button=task_list=name_edit=mode_combo=new_task_button=delete_task_button=name_label=mode_label=NULL;window=NULL;list=NULL;add_button=remove_button=run_button=close_button=NULL;hover_button=NULL;DeleteObject(body_font);DeleteObject(title_font);DeleteObject(background);DeleteObject(panel);body_font=title_font=NULL;background=panel=NULL;return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

BOOL batch_tasks_open(HWND owner){
    task_owner=owner;
    if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return TRUE;}
    if(!loaded){if(!store_load_batch_tasks(&collection)){MessageBoxW(owner,store_error(),L"NOVA Desktop",MB_OK|MB_ICONWARNING);return FALSE;}loaded=TRUE;}
    WNDCLASSEXW wc={0};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(NULL);wc.lpfnWndProc=window_proc;wc.lpszClassName=BATCH_CLASS;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassExW(&wc);
    HDC dc=GetDC(owner);if(dc){dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);}
    RECT owner_rect;GetWindowRect(owner,&owner_rect);int width=MulDiv(940,dpi,96),height=MulDiv(620,dpi,96),x=owner_rect.left+(owner_rect.right-owner_rect.left-width)/2,y=owner_rect.top+(owner_rect.bottom-owner_rect.top-height)/2;
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
    if(window&&!running&&!save_command())return FALSE;
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
    if(event->index>=0&&event->index<task.directory_count){
        if(event->type==EVENT_STARTED){statuses[event->index]=BATCH_RUNNING;wchar_t value[320];swprintf(value,320,nova_text(L"正在执行 %d/%d：%ls",L"Running %d/%d: %ls"),event->index+1,task.directory_count,task.directories[event->index]);set_summary(value);}
        else if(event->type==EVENT_SKIPPED)statuses[event->index]=BATCH_SKIPPED;
        else if(event->type==EVENT_RESULT){statuses[event->index]=event->error||event->exit_code?BATCH_FAILED:BATCH_SUCCEEDED;exit_codes[event->index]=event->exit_code;errors[event->index]=event->error;}
        refresh_row(event->index);
    }
    if(event->type==EVENT_FINISHED){
        running=FALSE;task_queue.active_id=0;cancel_requested=FALSE;if(worker){CloseHandle(worker);worker=NULL;}if(cancel_event){CloseHandle(cancel_event);cancel_event=NULL;}
        if(window){localize();enable_editor();EnableWindow(command_edit,TRUE);EnableWindow(save_button,TRUE);EnableWindow(add_button,TRUE);EnableWindow(remove_button,ListView_GetNextItem(list,-1,LVNI_SELECTED)>=0);EnableWindow(run_button,task.directory_count>0);
            wchar_t summary[240];swprintf(summary,240,nova_text(L"完成：成功 %d，失败 %d，跳过 %d",L"Finished: %d succeeded, %d failed, %d skipped"),event->success,event->failed,event->skipped);set_summary(summary);}
    }
    BOOL finished=event->type==EVENT_FINISHED;
    free(event);if(finished&&task_queue.count)start_next_task();return TRUE;
}
void batch_tasks_language_changed(void){localize();}
void batch_tasks_shutdown(void){batch_task_queue_cancel(&task_queue);if(running&&cancel_event)SetEvent(cancel_event);if(window)DestroyWindow(window);if(!running){if(worker)CloseHandle(worker);if(cancel_event)CloseHandle(cancel_event);worker=cancel_event=NULL;}}
HWND batch_tasks_window(void){return window;}
