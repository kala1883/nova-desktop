#include "hold_drag.h"
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
void hold_drag_init(HoldDrag *d,HWND list,HWND spaces,DropCallback drop,void *ctx){ZeroMemory(d,sizeof(*d));d->list=list;d->spaces=spaces;d->drop=drop;d->context=ctx;d->enabled=1;d->source=-1;d->source_row=-1;d->hover_workspace=-1;}
void hold_drag_cancel(HoldDrag *d){
    int hover=d->hover_workspace;d->armed=0;d->dragging=0;d->left_down=0;d->source=-1;d->source_row=-1;d->hover_workspace=-1;KillTimer(d->list,HOLD_DRAG_TIMER_ID);
    ListView_SetItemState(d->list,-1,0,LVIS_DROPHILITED);
    if(hover>=0&&IsWindow(d->spaces))InvalidateRect(d->spaces,NULL,FALSE);
    if(GetCapture()==d->list)ReleaseCapture();
    SetCursor(LoadCursorW(NULL,IDC_ARROW));
}
static int item_at(HWND list,POINT p,int *row_out){LVHITTESTINFO hit={0};hit.pt=p;int row=ListView_HitTest(list,&hit);if(row<0)return -1;LVITEMW i={0};i.mask=LVIF_PARAM;i.iItem=row;if(!SendMessageW(list,LVM_GETITEMW,0,(LPARAM)&i))return -1;if(row_out)*row_out=row;return (int)i.lParam;}
static int workspace_at(HWND spaces,POINT screen){
    RECT bounds;GetWindowRect(spaces,&bounds);if(!PtInRect(&bounds,screen))return -1;
    POINT point=screen;ScreenToClient(spaces,&point);DWORD hit=(DWORD)SendMessageW(spaces,LB_ITEMFROMPOINT,0,MAKELPARAM(point.x,point.y));RECT item;
    if(HIWORD(hit)||SendMessageW(spaces,LB_GETITEMRECT,LOWORD(hit),(LPARAM)&item)==LB_ERR||!PtInRect(&item,point))return -1;
    return LOWORD(hit);
}
static void update_workspace_hover(HoldDrag *d,int hover){
    if(hover==d->hover_workspace)return;
    d->hover_workspace=hover;InvalidateRect(d->spaces,NULL,FALSE);
}
static void begin_drag(HoldDrag *d){
    if(!d->armed||!d->left_down||d->dragging)return;
    KillTimer(d->list,HOLD_DRAG_TIMER_ID);d->dragging=1;if(GetCapture()!=d->list)SetCapture(d->list);
    ListView_SetItemState(d->list,d->source_row,LVIS_DROPHILITED,LVIS_DROPHILITED);SetCursor(LoadCursorW(NULL,IDC_SIZEALL));
}
BOOL hold_drag_message(HoldDrag *d,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_LBUTTONDOWN&&d->enabled){
        hold_drag_cancel(d);
        d->down=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};d->source=item_at(d->list,d->down,&d->source_row);
        if(d->source>=0){
            SetFocus(d->list);ListView_SetItemState(d->list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(d->list,d->source_row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetSelectionMark(d->list,d->source_row);
            d->armed=1;d->left_down=1;SetCapture(d->list);if(!SetTimer(d->list,HOLD_DRAG_TIMER_ID,350,NULL))hold_drag_cancel(d);
            return TRUE;
        }
    }
    if(msg==WM_TIMER&&wp==HOLD_DRAG_TIMER_ID){
        KillTimer(d->list,HOLD_DRAG_TIMER_ID);
        if(d->armed&&d->left_down)begin_drag(d);
        else d->armed=0;
        return TRUE;
    }
    if(msg==WM_MOUSEMOVE&&d->armed){
        POINT point={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(!d->dragging){int threshold_x=GetSystemMetrics(SM_CXDRAG),threshold_y=GetSystemMetrics(SM_CYDRAG);if(threshold_x<1)threshold_x=1;if(threshold_y<1)threshold_y=1;if(abs(point.x-d->down.x)<threshold_x&&abs(point.y-d->down.y)<threshold_y)return TRUE;begin_drag(d);}
        {
            POINT screen=point;ClientToScreen(d->list,&screen);int hover=workspace_at(d->spaces,screen);
            update_workspace_hover(d,hover);ListView_SetItemState(d->list,-1,0,LVIS_DROPHILITED);
            RECT list_bounds;GetWindowRect(d->list,&list_bounds);BOOL over_list=PtInRect(&list_bounds,screen);
            if(hover<0&&over_list){LVHITTESTINFO hit={0};hit.pt=point;int row=ListView_HitTest(d->list,&hit);if(row>=0)ListView_SetItemState(d->list,row,LVIS_DROPHILITED,LVIS_DROPHILITED);}
            SetCursor(LoadCursorW(NULL,(hover>0||over_list)?IDC_SIZEALL:IDC_NO));return TRUE;
        }
    }
    if(msg==WM_LBUTTONUP&&d->armed){
        d->left_down=0;BOOL dragging=d->dragging;int source=d->source,dest=-1,index=-1;BOOL valid=FALSE;
        POINT screen={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(d->list,&screen);
        RECT r;dest=workspace_at(d->spaces,screen);
        if(dest>=0)valid=TRUE;
        else{GetWindowRect(d->list,&r);if(PtInRect(&r,screen)){POINT p=screen;ScreenToClient(d->list,&p);index=item_at(d->list,p,NULL);valid=TRUE;}}
        hold_drag_cancel(d);if(dragging&&valid)d->drop(source,dest,index,d->context);return dragging;
    }
    if(msg==WM_LBUTTONDBLCLK){hold_drag_cancel(d);return FALSE;}
    if((msg==WM_CAPTURECHANGED&&GetCapture()!=d->list)||msg==WM_CANCELMODE||msg==WM_KILLFOCUS){hold_drag_cancel(d);}
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE&&(d->armed||d->dragging)){hold_drag_cancel(d);return TRUE;}
    return FALSE;
}
