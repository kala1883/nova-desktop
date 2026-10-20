#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <wchar.h>
#include "terminal_view.h"
#include "../../third_party/libvterm/include/vterm.h"
#include "../i18n.h"

#define TERMINAL_CLASS L"NovaSshTerminal"
#define HISTORY_LINES 1000
#define MAX_COLUMNS 500
typedef struct HistoryLine {int columns;VTermScreenCell *cells;} HistoryLine;
typedef struct TerminalView {
    HWND window;VTerm *terminal;VTermScreen *screen;VTermState *state;
    HFONT font,bold;HBRUSH background;int cw,ch,columns,rows;
    TerminalSend send;TerminalResize resize;void *context;
    HistoryLine history[HISTORY_LINES];int head,count,offset;
    BOOL cursor,alternate,selecting,selection,input_enabled;int selection_start,selection_end;
    wchar_t high_surrogate;char *paste_buffer;size_t paste_length,paste_capacity;
} TerminalView;
typedef struct TerminalCreation {TerminalView *view;BOOL adopted;} TerminalCreation;
static TerminalView *terminal_state(HWND window){return (TerminalView*)GetWindowLongPtrW(window,GWLP_USERDATA);}
static void terminal_scrollbar(TerminalView *t){SCROLLINFO info={sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,t->count+t->rows-1,(UINT)t->rows,t->count-t->offset,0};SetScrollInfo(t->window,SB_VERT,&info,TRUE);}
static void terminal_caret(TerminalView *t){
    if(GetFocus()!=t->window)return;
    VTermPos pos;vterm_state_get_cursorpos(t->state,&pos);
    SetCaretPos(4+pos.col*t->cw,4+pos.row*t->ch);
    /* HideCaret/ShowCaret are counted, so rebuild the caret at state changes. */
    DestroyCaret();CreateCaret(t->window,NULL,max(1,t->cw/5),t->ch);SetCaretPos(4+pos.col*t->cw,4+pos.row*t->ch);
    if(t->cursor&&!t->offset&&t->input_enabled)ShowCaret(t->window);
}
static void terminal_output(const char *data,size_t length,void *context){
    TerminalView *t=context;
    if(t->paste_buffer){if(length<=t->paste_capacity-t->paste_length){memcpy(t->paste_buffer+t->paste_length,data,length);t->paste_length+=length;}return;}
    if(t->input_enabled&&!t->send(t->context,data,length))MessageBeep(MB_ICONWARNING);
}
static int terminal_damage(VTermRect rect,void *context){(void)rect;(void)context;return 1;}
static int terminal_cursor(VTermPos pos,VTermPos old,int visible,void *context){(void)pos;(void)old;TerminalView *t=context;t->cursor=visible;return 1;}
static int terminal_property(VTermProp prop,VTermValue *value,void *context){
    TerminalView *t=context;if(prop==VTERM_PROP_CURSORVISIBLE)t->cursor=value->boolean;
    if(prop==VTERM_PROP_ALTSCREEN){t->alternate=value->boolean;t->offset=0;t->selection=FALSE;}
    /* Remote titles and OSC clipboard requests never affect Windows state. */
    return 1;
}
static int history_push(int columns,const VTermScreenCell *cells,void *context){
    TerminalView *t=context;int index=(t->head+t->count)%HISTORY_LINES;
    VTermScreenCell *copy=malloc((size_t)columns*sizeof(*copy));if(!copy)return 0;
    memcpy(copy,cells,(size_t)columns*sizeof(*copy));free(t->history[index].cells);t->history[index]=(HistoryLine){columns,copy};
    if(t->count==HISTORY_LINES)t->head=(t->head+1)%HISTORY_LINES;else t->count++;
    if(t->offset)t->offset=min(t->count,t->offset+1);
    t->selection=FALSE;return 1;
}
static int history_clear(void *context){TerminalView *t=context;for(int i=0;i<HISTORY_LINES;i++){free(t->history[i].cells);t->history[i]=(HistoryLine){0};}t->count=t->head=t->offset=0;t->selection=FALSE;return 1;}
static const VTermScreenCallbacks screen_callbacks={.damage=terminal_damage,.movecursor=terminal_cursor,.settermprop=terminal_property,.sb_pushline=history_push,.sb_clear=history_clear};
static BOOL terminal_cell(TerminalView *t,int row,int column,VTermScreenCell *cell){
    ZeroMemory(cell,sizeof(*cell));int line=t->count-t->offset+row;
    if(line<t->count){HistoryLine *history=&t->history[(t->head+line)%HISTORY_LINES];if(column<history->columns){*cell=history->cells[column];return TRUE;}return FALSE;}
    return vterm_screen_get_cell(t->screen,(VTermPos){line-t->count,column},cell)!=0;
}
static int utf16_cell(const VTermScreenCell *cell,wchar_t *text){
    int length=0;for(int i=0;i<VTERM_MAX_CHARS_PER_CELL&&cell->chars[i];i++){uint32_t c=cell->chars[i];if(c==UINT32_MAX)break;if(c>0x10ffff)c=0xfffd;if(c>0xffff){c-=0x10000;text[length++]=(wchar_t)(0xd800+(c>>10));text[length++]=(wchar_t)(0xdc00+(c&1023));}else text[length++]=(wchar_t)c;}return length;
}
static void terminal_copy(TerminalView *t){
    int start=t->selection?min(t->selection_start,t->selection_end):0,end=t->selection?max(t->selection_start,t->selection_end):t->rows*t->columns-1;
    size_t capacity=(size_t)t->rows*(t->columns*VTERM_MAX_CHARS_PER_CELL*2+2)+1;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,capacity*sizeof(wchar_t));if(!memory)return;
    wchar_t *text=GlobalLock(memory);if(!text){GlobalFree(memory);return;}size_t length=0;
    for(int row=start/t->columns;row<=end/t->columns;row++){
        int first=row==start/t->columns?start%t->columns:0,last=row==end/t->columns?end%t->columns:t->columns-1;size_t line_start=length;
        for(int col=first;col<=last;col++){VTermScreenCell cell;if(!terminal_cell(t,row,col,&cell)||!cell.chars[0])text[length++]=L' ';else if(cell.chars[0]!=UINT32_MAX)length+=(size_t)utf16_cell(&cell,text+length);}
        while(length>line_start&&text[length-1]==L' ')length--;
        if(row<end/t->columns){text[length++]=L'\r';text[length++]=L'\n';}
    }
    text[length]=0;GlobalUnlock(memory);
    if(OpenClipboard(t->window)){if(EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory))memory=NULL;CloseClipboard();}if(memory)GlobalFree(memory);
}
static void terminal_paste_text(TerminalView *t,const wchar_t *text,size_t length){
        /* One atomic enqueue avoids partially submitting a command when input is full. */
        if(length<=8192){char *bytes=malloc(length*4+16);if(bytes){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,(int)length,bytes+6,(int)(length*4),NULL,NULL);
            if(n>0){/* Collect bracketed paste markers and content into one enqueue. */
                t->paste_buffer=bytes;t->paste_length=0;t->paste_capacity=length*4+16;vterm_keyboard_start_paste(t->terminal);
                size_t prefix=t->paste_length;/* Six-byte prefix fits before the UTF-8 source. */
                for(int i=0;i<n;i++){char c=bytes[6+i];if(c=='\r'&&i+1<n&&bytes[6+i+1]=='\n')i++;bytes[prefix++]=c=='\n'?'\r':c;}
                t->paste_length=prefix;vterm_keyboard_end_paste(t->terminal);size_t total=t->paste_length;t->paste_buffer=NULL;
                if(!t->send(t->context,bytes,total))MessageBeep(MB_ICONWARNING);
            }free(bytes);}}
        else MessageBeep(MB_ICONWARNING);
}
static void terminal_paste(TerminalView *t){
    if(!t->input_enabled||!OpenClipboard(t->window))return;
    HANDLE memory=GetClipboardData(CF_UNICODETEXT);const wchar_t *text=memory?GlobalLock(memory):NULL;
    if(text){size_t capacity=GlobalSize(memory)/sizeof(wchar_t),length=0;while(length<min(capacity,8193)&&text[length])length++;
        if(length<capacity&&length<=8192)terminal_paste_text(t,text,length);else MessageBeep(MB_ICONWARNING);
        GlobalUnlock(memory);
    }CloseClipboard();
}
static void terminal_paint(TerminalView *t,HDC dc){
    RECT area;GetClientRect(t->window,&area);FillRect(dc,&area,t->background);
    HFONT old=SelectObject(dc,t->font);int a=min(t->selection_start,t->selection_end),b=max(t->selection_start,t->selection_end);
    for(int row=0;row<t->rows;row++)for(int col=0;col<t->columns;col++){
        VTermScreenCell cell;if(!terminal_cell(t,row,col,&cell))continue;
        vterm_screen_convert_color_to_rgb(t->screen,&cell.fg);vterm_screen_convert_color_to_rgb(t->screen,&cell.bg);
        COLORREF fg=RGB(cell.fg.rgb.red,cell.fg.rgb.green,cell.fg.rgb.blue),bg=RGB(cell.bg.rgb.red,cell.bg.rgb.green,cell.bg.rgb.blue);
        if(cell.attrs.reverse){COLORREF swap=fg;fg=bg;bg=swap;}
        int index=row*t->columns+col;if(t->selection&&index>=a&&index<=b){bg=RGB(53,77,122);fg=RGB(236,241,249);}
        RECT cell_rect={4+col*t->cw,4+row*t->ch,4+(col+max(1,cell.width))*t->cw,4+(row+1)*t->ch};
        SetTextColor(dc,cell.attrs.conceal?bg:fg);SetBkColor(dc,bg);SelectObject(dc,cell.attrs.bold?t->bold:t->font);
        wchar_t text[VTERM_MAX_CHARS_PER_CELL*2];int length=utf16_cell(&cell,text);
        ExtTextOutW(dc,cell_rect.left,cell_rect.top,ETO_OPAQUE|ETO_CLIPPED,&cell_rect,text,(UINT)length,NULL);
        if(cell.attrs.underline){RECT line=cell_rect;line.top=line.bottom-2;SetDCBrushColor(dc,fg);FillRect(dc,&line,GetStockObject(DC_BRUSH));}
        if(cell.width==2)col++;
    }SelectObject(dc,old);
}
static void terminal_measure(TerminalView *t){
    RECT area;GetClientRect(t->window,&area);int columns=max(2,min(MAX_COLUMNS,(area.right-8)/t->cw)),rows=max(2,min(200,(area.bottom-8)/t->ch));
    if(t->columns==columns&&t->rows==rows)return;
    t->columns=columns;t->rows=rows;t->selection=FALSE;t->offset=0;vterm_set_size(t->terminal,rows,columns);vterm_screen_flush_damage(t->screen);terminal_scrollbar(t);terminal_caret(t);InvalidateRect(t->window,NULL,FALSE);t->resize(t->context,columns,rows);
}
static VTermModifier terminal_modifiers(void){int modifiers=0;if(GetKeyState(VK_SHIFT)&0x8000)modifiers|=VTERM_MOD_SHIFT;if(GetKeyState(VK_CONTROL)&0x8000)modifiers|=VTERM_MOD_CTRL;if(GetKeyState(VK_MENU)&0x8000)modifiers|=VTERM_MOD_ALT;return (VTermModifier)modifiers;}
static BOOL terminal_key(TerminalView *t,WPARAM key){
    VTermModifier mod=terminal_modifiers();
    if((mod&(VTERM_MOD_CTRL|VTERM_MOD_SHIFT))==(VTERM_MOD_CTRL|VTERM_MOD_SHIFT)&&key=='C'){terminal_copy(t);return TRUE;}
    if(((mod&(VTERM_MOD_CTRL|VTERM_MOD_SHIFT))==(VTERM_MOD_CTRL|VTERM_MOD_SHIFT)&&key=='V')||(key==VK_INSERT&&mod==VTERM_MOD_SHIFT)){terminal_paste(t);return TRUE;}
    if(!t->input_enabled)return TRUE;
    if(mod==VTERM_MOD_ALT&&(key==VK_F4||key==VK_SPACE))return FALSE;
    VTermKey code=VTERM_KEY_NONE;
    switch(key){case VK_RETURN:code=VTERM_KEY_ENTER;break;case VK_TAB:code=VTERM_KEY_TAB;break;case VK_BACK:code=VTERM_KEY_BACKSPACE;break;case VK_ESCAPE:code=VTERM_KEY_ESCAPE;break;
    case VK_UP:code=VTERM_KEY_UP;break;case VK_DOWN:code=VTERM_KEY_DOWN;break;case VK_LEFT:code=VTERM_KEY_LEFT;break;case VK_RIGHT:code=VTERM_KEY_RIGHT;break;
    case VK_INSERT:code=VTERM_KEY_INS;break;case VK_DELETE:code=VTERM_KEY_DEL;break;case VK_HOME:code=VTERM_KEY_HOME;break;case VK_END:code=VTERM_KEY_END;break;case VK_PRIOR:code=VTERM_KEY_PAGEUP;break;case VK_NEXT:code=VTERM_KEY_PAGEDOWN;break;}
    if(key>=VK_F1&&key<=VK_F24)code=VTERM_KEY_FUNCTION((int)key-VK_F1+1);
    if(code!=VTERM_KEY_NONE){t->offset=0;t->selection=FALSE;vterm_keyboard_key(t->terminal,code,mod);terminal_scrollbar(t);terminal_caret(t);InvalidateRect(t->window,NULL,FALSE);return TRUE;}
    /* AltGr must be translated to Unicode by Windows, not treated as Ctrl+Alt. */
    if((mod&VTERM_MOD_CTRL)&&!(GetKeyState(VK_RMENU)&0x8000)&&((key>='A'&&key<='Z')||key==VK_SPACE||key==VK_OEM_4||key==VK_OEM_5||key==VK_OEM_6||key=='6'||key==VK_OEM_MINUS)){
        uint32_t c=key>=65&&key<=90?(uint32_t)(key+32):key==VK_SPACE?' ':key==VK_OEM_4?'[':key==VK_OEM_5?'\\':key==VK_OEM_6?']':key=='6'?'^':'_';
        char bytes[2]={27,(char)(c&0x1f)};if(!t->send(t->context,bytes+((mod&VTERM_MOD_ALT)?0:1),(mod&VTERM_MOD_ALT)?2:1))MessageBeep(MB_ICONWARNING);return TRUE;
    }return FALSE;
}
static int selection_position(TerminalView *t,LPARAM position){return max(0,min(t->rows-1,(GET_Y_LPARAM(position)-4)/t->ch))*t->columns+max(0,min(t->columns-1,(GET_X_LPARAM(position)-4)/t->cw));}
static LRESULT CALLBACK terminal_proc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    TerminalView *t=terminal_state(h);if(msg==WM_NCCREATE){TerminalCreation *creation=((CREATESTRUCTW*)lp)->lpCreateParams;t=creation->view;creation->adopted=TRUE;t->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)t);}if(!t)return DefWindowProcW(h,msg,wp,lp);
    switch(msg){
    case WM_CREATE:{HDC dc=GetDC(h);int dpi=GetDeviceCaps(dc,LOGPIXELSY);LOGFONTW font={0};font.lfHeight=-MulDiv(14,dpi,96);font.lfCharSet=DEFAULT_CHARSET;font.lfQuality=CLEARTYPE_QUALITY;font.lfPitchAndFamily=FIXED_PITCH|FF_MODERN;lstrcpyW(font.lfFaceName,L"Consolas");t->font=CreateFontIndirectW(&font);font.lfWeight=FW_BOLD;t->bold=CreateFontIndirectW(&font);
        HFONT old=SelectObject(dc,t->font);TEXTMETRICW metrics;GetTextMetricsW(dc,&metrics);t->cw=max(1,metrics.tmAveCharWidth);t->ch=max(1,metrics.tmHeight);SelectObject(dc,old);ReleaseDC(h,dc);t->background=CreateSolidBrush(RGB(14,19,29));return 0;}
    case WM_SIZE:terminal_measure(t);return 0;
    case WM_GETDLGCODE:return DLGC_WANTALLKEYS|DLGC_WANTCHARS;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:if(terminal_key(t,wp))return 0;break;
    case WM_CHAR:case WM_SYSCHAR:{if(!t->input_enabled)return 0;uint32_t c=(uint32_t)wp;if(c>=0xd800&&c<=0xdbff){t->high_surrogate=(wchar_t)c;return 0;}if(c>=0xdc00&&c<=0xdfff)c=t->high_surrogate?0x10000+(((uint32_t)t->high_surrogate-0xd800)<<10)+(c-0xdc00):0xfffd;t->high_surrogate=0;
        t->offset=0;t->selection=FALSE;VTermModifier mod=terminal_modifiers();mod=(VTermModifier)(mod&VTERM_MOD_ALT);if(GetKeyState(VK_RMENU)&0x8000)mod=VTERM_MOD_NONE;vterm_keyboard_unichar(t->terminal,c,mod);terminal_scrollbar(t);terminal_caret(t);InvalidateRect(h,NULL,FALSE);return 0;}
    case WM_SETFOCUS:terminal_caret(t);return 0;
    case WM_KILLFOCUS:DestroyCaret();return 0;
    case WM_ENABLE:terminal_caret(t);return 0;
    case WM_LBUTTONDOWN:SetFocus(h);t->selection_start=t->selection_end=selection_position(t,lp);t->selecting=TRUE;t->selection=FALSE;SetCapture(h);return 0;
    case WM_MOUSEMOVE:if(t->selecting){t->selection_end=selection_position(t,lp);t->selection=TRUE;InvalidateRect(h,NULL,FALSE);}return 0;
    case WM_LBUTTONUP:if(t->selecting){t->selecting=FALSE;ReleaseCapture();}return 0;
    case WM_CAPTURECHANGED:t->selecting=FALSE;return 0;
    case WM_CONTEXTMENU:{HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,nova_text(L"复制 (Ctrl+Shift+C)",L"Copy (Ctrl+Shift+C)"));AppendMenuW(menu,MF_STRING|(t->input_enabled?0:MF_GRAYED),2,nova_text(L"粘贴 (Ctrl+Shift+V)",L"Paste (Ctrl+Shift+V)"));POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(point.x==-1&&point.y==-1){point=(POINT){4,4};ClientToScreen(h,&point);}int id=(int)TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,h,NULL);DestroyMenu(menu);if(id==1)terminal_copy(t);if(id==2)terminal_paste(t);return 0;}
    case WM_MOUSEWHEEL:if(!t->alternate){t->selection=FALSE;t->offset=max(0,min(t->count,t->offset+GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*3));terminal_scrollbar(t);terminal_caret(t);InvalidateRect(h,NULL,FALSE);}return 0;
    case WM_VSCROLL:{SCROLLINFO info={0};info.cbSize=sizeof(info);info.fMask=SIF_TRACKPOS;GetScrollInfo(h,SB_VERT,&info);int offset=t->offset;switch(LOWORD(wp)){case SB_LINEUP:offset++;break;case SB_LINEDOWN:offset--;break;case SB_PAGEUP:offset+=t->rows;break;case SB_PAGEDOWN:offset-=t->rows;break;case SB_THUMBTRACK:offset=t->count-info.nTrackPos;break;case SB_TOP:offset=t->count;break;case SB_BOTTOM:offset=0;break;}t->offset=max(0,min(t->count,offset));terminal_scrollbar(t);terminal_caret(t);InvalidateRect(h,NULL,FALSE);return 0;}
    case WM_PAINT:{PAINTSTRUCT paint;HDC dc=BeginPaint(h,&paint);terminal_paint(t,dc);EndPaint(h,&paint);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_DESTROY:history_clear(t);vterm_free(t->terminal);DeleteObject(t->font);DeleteObject(t->bold);DeleteObject(t->background);return 0;
    case WM_NCDESTROY:SetWindowLongPtrW(h,GWLP_USERDATA,0);free(t);return DefWindowProcW(h,msg,wp,lp);
    }return DefWindowProcW(h,msg,wp,lp);
}
HWND terminal_view_create(HWND parent,TerminalSend send,TerminalResize resize,void *context){
    TerminalView *t=calloc(1,sizeof(*t));if(!t)return NULL;t->send=send;t->resize=resize;t->context=context;t->columns=80;t->rows=24;t->cursor=t->input_enabled=TRUE;
    t->terminal=vterm_new(t->rows,t->columns);if(!t->terminal){free(t);return NULL;}vterm_set_utf8(t->terminal,1);t->state=vterm_obtain_state(t->terminal);t->screen=vterm_obtain_screen(t->terminal);
    vterm_output_set_callback(t->terminal,terminal_output,t);vterm_screen_set_callbacks(t->screen,&screen_callbacks,t);vterm_screen_enable_altscreen(t->screen,1);vterm_screen_set_damage_merge(t->screen,VTERM_DAMAGE_SCREEN);
    VTermColor fg,bg;vterm_color_rgb(&fg,236,241,249);vterm_color_rgb(&bg,14,19,29);vterm_screen_set_default_colors(t->screen,&fg,&bg);vterm_screen_reset(t->screen,1);
    WNDCLASSW wc={0};wc.lpfnWndProc=terminal_proc;wc.hInstance=GetModuleHandleW(NULL);wc.hCursor=LoadCursorW(NULL,IDC_IBEAM);wc.lpszClassName=TERMINAL_CLASS;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){vterm_free(t->terminal);free(t);return NULL;}
    TerminalCreation creation={t,FALSE};HWND window=CreateWindowExW(0,TERMINAL_CLASS,nova_text(L"SSH 交互终端",L"SSH interactive terminal"),WS_CHILD|WS_TABSTOP|WS_VSCROLL|WS_BORDER,0,0,640,400,parent,NULL,wc.hInstance,&creation);
    if(!window&&!creation.adopted){vterm_free(t->terminal);free(t);}return window;
}
void terminal_view_feed(HWND window,const char *data,size_t length){TerminalView *t=terminal_state(window);if(!t)return;vterm_input_write(t->terminal,data,length);vterm_screen_flush_damage(t->screen);terminal_scrollbar(t);terminal_caret(t);InvalidateRect(window,NULL,FALSE);}
void terminal_view_reset(HWND window){TerminalView *t=terminal_state(window);if(!t)return;history_clear(t);t->high_surrogate=0;vterm_screen_reset(t->screen,1);vterm_screen_flush_damage(t->screen);terminal_scrollbar(t);InvalidateRect(window,NULL,FALSE);}
void terminal_view_enable_input(HWND window,BOOL enabled){TerminalView *t=terminal_state(window);if(t&&t->input_enabled!=enabled){t->input_enabled=enabled;terminal_caret(t);}}
void terminal_view_size(HWND window,int *columns,int *rows){TerminalView *t=terminal_state(window);*columns=t?t->columns:80;*rows=t?t->rows:24;}
BOOL terminal_view_message(HWND window,MSG *message){
    TerminalView *t=terminal_state(window);if(!t||message->hwnd!=window||message->message<WM_KEYFIRST||message->message>WM_KEYLAST)return FALSE;
    if(message->message==WM_SYSKEYDOWN&&(message->wParam==VK_F4||message->wParam==VK_SPACE))return FALSE;
    if(message->message==WM_KEYDOWN||message->message==WM_SYSKEYDOWN){if(!terminal_key(t,message->wParam))TranslateMessage(message);return TRUE;}
    DispatchMessageW(message);return TRUE;
}
