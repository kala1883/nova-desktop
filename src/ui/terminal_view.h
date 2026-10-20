#ifndef NOVA_TERMINAL_VIEW_H
#define NOVA_TERMINAL_VIEW_H
#include <windows.h>
typedef BOOL (*TerminalSend)(void *context,const char *data,size_t length);
typedef void (*TerminalResize)(void *context,int columns,int rows);
HWND terminal_view_create(HWND parent,TerminalSend send,TerminalResize resize,void *context);
void terminal_view_feed(HWND window,const char *data,size_t length);
void terminal_view_reset(HWND window);
void terminal_view_enable_input(HWND window,BOOL enabled);
void terminal_view_size(HWND window,int *columns,int *rows);
BOOL terminal_view_message(HWND window,MSG *message);
#endif
