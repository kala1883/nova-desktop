#include "hold_drag.h"
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
void hold_drag_init(HoldDrag *d,HWND list,HWND spaces,DropCallback drop,void *ctx){ZeroMemory(d,sizeof(*d));d->list=list;d->spaces=spaces;d->drop=drop;d->context=ctx;d->enabled=1;}
void hold_drag_cancel(HoldDrag *d){
    d->armed=0;d->dragging=0;d->left_down=0;KillTimer(d->list,HOLD_DRAG_TIMER_ID);
    ListView_SetItemState(d->list,-1,0,LVIS_DROPHILITED);
    if(GetCapture()==d->list)ReleaseCapture();
    SetCursor(LoadCursorW(NULL,IDC_ARROW));
}
static int item_at(HWND list,POINT p){LVHITTESTINFO hit={0};hit.pt=p;int row=ListView_HitTest(list,&hit);if(row<0)return -1;LVITEMW i={0};i.mask=LVIF_PARAM;i.iItem=row;SendMessageW(list,LVM_GETITEMW,0,(LPARAM)&i);return (int)i.lParam;}
BOOL hold_drag_message(HoldDrag *d,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_LBUTTONDOWN&&d->enabled){
        hold_drag_cancel(d);
        d->down=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};d->source=item_at(d->list,d->down);
        if(d->source>=0){d->armed=1;d->left_down=1;SetCapture(d->list);if(!SetTimer(d->list,HOLD_DRAG_TIMER_ID,350,NULL))hold_drag_cancel(d);}
    }
    if(msg==WM_TIMER&&wp==HOLD_DRAG_TIMER_ID){
        KillTimer(d->list,HOLD_DRAG_TIMER_ID);
        if(d->armed&&d->left_down){d->dragging=1;if(GetCapture()!=d->list)SetCapture(d->list);ListView_SetItemState(d->list,d->source,LVIS_DROPHILITED,LVIS_DROPHILITED);SetCursor(LoadCursorW(NULL,IDC_SIZEALL));}
        else d->armed=0;
        return TRUE;
    }
    if(msg==WM_MOUSEMOVE&&d->armed){
        if(!d->dragging)return TRUE;
        else{LVHITTESTINFO hit={0};hit.pt=(POINT){GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int row=ListView_HitTest(d->list,&hit);ListView_SetItemState(d->list,-1,0,LVIS_DROPHILITED);if(row>=0)ListView_SetItemState(d->list,row,LVIS_DROPHILITED,LVIS_DROPHILITED);SetCursor(LoadCursorW(NULL,IDC_SIZEALL));return TRUE;}
    }
    if(msg==WM_LBUTTONUP&&d->armed){
        d->left_down=0;BOOL dragging=d->dragging;int source=d->source,dest=-1,index=-1;BOOL valid=FALSE;
        POINT screen={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(d->list,&screen);
        RECT r;GetWindowRect(d->spaces,&r);
        if(PtInRect(&r,screen)){POINT p=screen;ScreenToClient(d->spaces,&p);DWORD hit=(DWORD)SendMessageW(d->spaces,LB_ITEMFROMPOINT,0,MAKELPARAM(p.x,p.y));RECT item;
            if(!HIWORD(hit)&&SendMessageW(d->spaces,LB_GETITEMRECT,LOWORD(hit),(LPARAM)&item)!=LB_ERR&&PtInRect(&item,p)){dest=LOWORD(hit);valid=TRUE;}}
        else{GetWindowRect(d->list,&r);if(PtInRect(&r,screen)){POINT p=screen;ScreenToClient(d->list,&p);index=item_at(d->list,p);valid=TRUE;}}
        hold_drag_cancel(d);if(dragging&&valid)d->drop(source,dest,index,d->context);return dragging;
    }
    if(msg==WM_LBUTTONDBLCLK){hold_drag_cancel(d);return FALSE;}
    if((msg==WM_CAPTURECHANGED&&GetCapture()!=d->list)||msg==WM_CANCELMODE||msg==WM_KILLFOCUS){hold_drag_cancel(d);}
    if(msg==WM_KEYDOWN&&wp==VK_ESCAPE&&(d->armed||d->dragging)){hold_drag_cancel(d);return TRUE;}
    return FALSE;
}
