#include "../src/ui/terminal_view.c"
#include <assert.h>
#include <stdio.h>
BOOL nova_english=FALSE;
static char sent[65536];static size_t sent_length;static int sends,resize_columns,resize_rows;
static BOOL capture(void *context,const char *data,size_t length){(void)context;assert(sent_length+length<sizeof(sent));memcpy(sent+sent_length,data,length);sent_length+=length;sent[sent_length]=0;sends++;return TRUE;}
static void resized(void *context,int columns,int rows){(void)context;resize_columns=columns;resize_rows=rows;}
static void feed(HWND window,const char *text){terminal_view_feed(window,text,strlen(text));}
static VTermScreenCell cell(TerminalView *t,int row,int col){VTermScreenCell c;assert(vterm_screen_get_cell(t->screen,(VTermPos){row,col},&c));return c;}
int wmain(void){
    HWND parent=CreateWindowExW(0,L"STATIC",L"Isolated terminal test",WS_OVERLAPPEDWINDOW,0,0,900,600,NULL,NULL,GetModuleHandleW(NULL),NULL);assert(parent);
    HWND window=terminal_view_create(parent,capture,resized,NULL);assert(window);ShowWindow(window,SW_SHOW);MoveWindow(window,0,0,804,404,TRUE);
    TerminalView *t=terminal_state(window);assert(t&&t->columns==resize_columns&&t->rows==resize_rows);assert(resize_columns>50&&resize_rows>10);
    feed(window,"hello\r\n\x1b[31mred\x1b[0m");assert(cell(t,0,0).chars[0]=='h'&&cell(t,1,0).chars[0]=='r');VTermScreenCell red=cell(t,1,0);vterm_screen_convert_color_to_rgb(t->screen,&red.fg);assert(red.fg.rgb.red>red.fg.rgb.green);
    feed(window,"\x1b[2J\x1b[H\xe4\xb8");feed(window,"\xad\xe6\x96\x87");assert(cell(t,0,0).chars[0]==0x4e2d&&cell(t,0,0).width==2&&cell(t,0,2).chars[0]==0x6587);
    feed(window,"\x1b[?1049h\x1b[Htop\x1b[2;1H\x1b[38;5;196mCPU");assert(t->alternate&&cell(t,0,0).chars[0]=='t'&&cell(t,1,0).chars[0]=='C');feed(window,"\x1b[?1049l");assert(!t->alternate&&cell(t,0,0).chars[0]==0x4e2d);
    sent_length=0;feed(window,"\x1b[6n");assert(sent_length>0&&sent[0]==27&&sent[sent_length-1]=='R');sent_length=0;
    feed(window,"\x1b]52;c;Y2xpcGJvYXJk\x07\x1b]2;untrusted title\x07");wchar_t title[80];GetWindowTextW(window,title,80);assert(!wcscmp(title,L"SSH 交互终端")&&!sent_length);
    feed(window,"\x1b[?1h");assert(terminal_key(t,VK_UP));assert(!memcmp(sent,"\x1bOA",3));sent_length=0;assert(terminal_key(t,VK_TAB));assert(sent_length==1&&sent[0]=='\t');sent_length=0;assert(terminal_key(t,VK_F5));assert(strstr(sent,"15~"));
    sent_length=0;sends=0;feed(window,"\x1b[?2004h");terminal_paste_text(t,L"a\r\n中文\nb",7);assert(sends==1&&strstr(sent,"\x1b[200~a\r")&&strstr(sent,"\xe4\xb8\xad\xe6\x96\x87\rb\x1b[201~"));
    terminal_view_reset(window);for(int i=0;i<1100;i++)feed(window,"line\r\n");assert(t->count==HISTORY_LINES);SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA*3),0);assert(t->offset>0);MoveWindow(window,0,0,440,240,TRUE);assert(t->offset==0&&t->columns==resize_columns);
    terminal_view_enable_input(window,FALSE);sent_length=0;SendMessageW(window,WM_CHAR,'x',0);assert(!sent_length&&IsWindowEnabled(window));terminal_view_reset(window);assert(!t->count);DestroyWindow(parent);
    DWORD objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);for(int i=0;i<12;i++){HWND host=CreateWindowExW(0,L"STATIC",L"Lifecycle",WS_OVERLAPPEDWINDOW,0,0,600,400,NULL,NULL,GetModuleHandleW(NULL),NULL);assert(host&&terminal_view_create(host,capture,resized,NULL));DestroyWindow(host);}assert(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==objects);
    puts("PASS terminal: split UTF-8/CJK, ANSI colors, cursor addressing, alternate screen, device reply, application keys, atomic bracketed paste, bounded scrollback, resize and disabled input");return 0;
}
