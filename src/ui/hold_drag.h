#ifndef NOVA_HOLD_DRAG_H
#define NOVA_HOLD_DRAG_H
#include <windows.h>
typedef void (*DropCallback)(int source,int destination_workspace,int destination_index,void *context);
typedef struct {HWND list,spaces;int source,armed,dragging,enabled;POINT down;DropCallback drop;void *context;} HoldDrag;
void hold_drag_init(HoldDrag *drag,HWND list,HWND spaces,DropCallback drop,void *context);
void hold_drag_cancel(HoldDrag *drag);
/* Returns TRUE if consumed; call before native list-view processing. */
BOOL hold_drag_message(HoldDrag *drag,UINT message,WPARAM wp,LPARAM lp);
#endif
