/* Per-device hover detail. Included by main.c after monitor_hit.
 * An owned non-activating popup overlays embedded Shell child windows without
 * becoming an independently topmost window. */
static int monitor_hover_kind=-1;
static HWND monitor_detail;
static wchar_t monitor_detail_description[768];
static const COLORREF monitor_colors[]={RGB(63,209,242),RGB(175,137,255),RGB(245,183,94),RGB(93,216,152),RGB(104,166,255)};
static int monitor_kind_at(int index){
    for(int i=0;i<MON_COUNT;i++)if(monitor.mask[monitor.order[i]]&&index--==0)return monitor.order[i];
    return -1;
}
static void monitor_detail_hide(void){
    monitor_hover_kind=-1;
    if(IsWindow(monitor_detail))ShowWindow(monitor_detail,SW_HIDE);
}
static void monitor_detail_refresh(void){
    int kind=monitor_hover_kind;
    if(kind<0||kind>=MON_COUNT||!IsWindow(monitor_detail))return;
    wchar_t next[768];lstrcpynW(next,monitor_name(kind),768);
    for(int a=0;a<monitor_attrs[kind];a++){
        wchar_t value[64],part[120];monitor_format(kind,a,value,64);
        swprintf(part,120,L"; %ls: %ls",monitor_attr(kind,a),value);
        if(wcslen(next)+wcslen(part)<768)lstrcatW(next,part);
    }
    if(lstrcmpW(next,monitor_detail_description)){
        lstrcpyW(monitor_detail_description,next);
        SetWindowTextW(monitor_detail,next);
        InvalidateRect(monitor_detail,NULL,FALSE);
    }
}
static void monitor_detail_paint(HDC dc,RECT r,int kind){
    HBRUSH fill=CreateSolidBrush(RGB(27,36,51));if(fill){FillRect(dc,&r,fill);DeleteObject(fill);}
    HBRUSH edge=CreateSolidBrush(RGB(62,77,98));if(edge){FrameRect(dc,&r,edge);DeleteObject(edge);}
    if(kind<0||kind>=MON_COUNT)return;
    int w=r.right-r.left;
    gauge_symbol(dc,px(20),px(20),kind,FALSE,monitor_colors[kind]);
    text(dc,monitor_name(kind),box(px(39),px(7),w-px(51),px(26)),body_font,TEXT,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    RECT divider=box(px(12),px(38),w-px(24),px(1));
    HBRUSH rule=CreateSolidBrush(RGB(45,59,79));if(rule){FillRect(dc,&divider,rule);DeleteObject(rule);}
    for(int a=0;a<monitor_attrs[kind];a++){
        wchar_t value[64];monitor_format(kind,a,value,64);
        int top=px(43+a*25),split=w*55/100;
        text(dc,monitor_attr(kind,a),box(px(12),top,split-px(16),px(23)),small_font,MUTED,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        text(dc,value,box(split,top,w-split-px(12),px(23)),small_font,TEXT,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    }
}
static LRESULT CALLBACK monitor_detail_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=buffer?CreateCompatibleBitmap(dc,r.right,r.bottom):NULL;
        if(bitmap){HGDIOBJ old=SelectObject(buffer,bitmap);monitor_detail_paint(buffer,r,monitor_hover_kind);
            BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);}
        else monitor_detail_paint(dc,r,monitor_hover_kind);
        if(buffer)DeleteDC(buffer);
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void monitor_detail_hover(HWND hwnd,POINT point){
    int index=monitor_hit(hwnd,point,FALSE),kind=index<0?-1:monitor_kind_at(index);
    if(kind<0||!IsWindowVisible(hwnd)||monitor_drag_from>=0){monitor_detail_hide();return;}
    if(kind==monitor_hover_kind&&IsWindowVisible(monitor_detail))return;
    if(!IsWindow(monitor_detail)){
        WNDCLASSW wc={0};wc.lpfnWndProc=monitor_detail_proc;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"NovaMonitorDetail";
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
        monitor_detail=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,wc.lpszClassName,L"",WS_POPUP,
            0,0,0,0,main_window,NULL,wc.hInstance,NULL);
        DWORD error=monitor_detail?0:GetLastError();
        nova_log_write(monitor_detail?NOVA_LOG_INFO:NOVA_LOG_WARN,"monitor","detail_window_create",L"ok=%d code=%lu owned_popup=1",monitor_detail!=NULL,error);
        if(!monitor_detail)return;
        monitor_detail_description[0]=0;
    }
    RECT client,cell,area;GetClientRect(hwnd,&client);GetClientRect(main_window,&area);
    if(!monitor_cell(index,client.right,client.bottom,&cell)){monitor_detail_hide();return;}
    POINT anchor={client.right,(cell.top+cell.bottom)/2};MapWindowPoints(hwnd,main_window,&anchor,1);
    int width=px(244),height=px(53+25*monitor_attrs[kind]),left=anchor.x+px(8),top=anchor.y-height/2;
    if(left+width>area.right-px(4)||height>area.bottom-caption_height()-px(4)){monitor_detail_hide();return;}
    if(top<caption_height()+px(4))top=caption_height()+px(4);
    if(top+height>area.bottom-px(4))top=area.bottom-px(4)-height;
    POINT position={left,top};ClientToScreen(main_window,&position);
    monitor_hover_kind=kind;monitor_detail_refresh();
    /* Place immediately above its owner, not above unrelated applications.
     * The owner remains in its existing z-order band (including desktop fence). */
    HWND previous=GetWindow(main_window,GW_HWNDPREV);
    UINT flags=SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW;
    if(previous==monitor_detail)flags|=SWP_NOZORDER;
    if(!SetWindowPos(monitor_detail,previous?previous:HWND_TOP,position.x,position.y,width,height,flags)){
        DWORD error=GetLastError();monitor_detail_hide();
        nova_log_write(NOVA_LOG_WARN,"monitor","detail_window_place_failed",L"code=%lu",error);return;
    }
    InvalidateRect(monitor_detail,NULL,FALSE);
}
static void monitor_network_surface(HDC dc,RECT cell,int mask,int attrs,int line,HFONT compact_font){
    int cw=cell.right-cell.left,ch=cell.bottom-cell.top,x=(cell.left+cell.right)/2;
    int content=cw-px(8);if(content>px(146))content=px(146);
    int left=x-content/2,header=px(15);
    int top=cell.top+(ch-header-line*attrs)/2;if(top<cell.top)top=cell.top;
    if(cw>=px(78)){
        HGDIOBJ old=SelectObject(dc,small_font);SIZE size={0};const wchar_t *name=monitor_name(MON_NET);
        GetTextExtentPoint32W(dc,name,lstrlenW(name),&size);SelectObject(dc,old);
        int group=px(16)+size.cx;if(group>content)group=content;
        int start=x-group/2;
        gauge_symbol(dc,start+px(6),top+header/2,MON_NET,TRUE,monitor_colors[MON_NET]);
        text(dc,name,box(start+px(16),top,group-px(16),header),small_font,monitor_colors[MON_NET],DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    }else gauge_symbol(dc,x,top+header/2,MON_NET,TRUE,monitor_colors[MON_NET]);
    top+=header;
    for(int a=0;a<monitor_attrs[MON_NET];a++)if(mask&(1<<a)){
        wchar_t value[64];monitor_format(MON_NET,a,value,64);
        if(cw>=px(100)){
            int label_width=content*43/100;
            text(dc,monitor_attr(MON_NET,a),box(left,top,label_width,line),compact_font?compact_font:small_font,MUTED,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            text(dc,value,box(left+label_width,top,content-label_width,line),compact_font?compact_font:small_font,TEXT,DT_RIGHT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        }else{
            wchar_t label[100];const wchar_t *prefix=a==0?L"\x2193 ":a==1?L"\x2191 ":a==2?L"\x2194 ":L"# ";
            swprintf(label,100,L"%ls%ls",prefix,wcscmp(value,nova_text(L"暂无数据",L"N/A"))?value:L"N/A");
            text(dc,label,box(cell.left+px(2),top,cw-px(4),line),compact_font?compact_font:small_font,TEXT,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        }
        top+=line;
    }
}
