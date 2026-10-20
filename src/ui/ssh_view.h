#ifndef NOVA_SSH_VIEW_H
#define NOVA_SSH_VIEW_H
#include <windows.h>
#include "../platform/ssh_client.h"
#define SSH_VIEW_NAVIGATED (WM_APP+79)
enum { SSH_COPY=1,SSH_CUT,SSH_PASTE,SSH_RENAME,SSH_DELETE,SSH_NEWFOLDER,SSH_REFRESH,SSH_UP,SSH_COMMAND };
BOOL ssh_choose_server(HWND owner,int64_t fixed_id,SshLogin *login);
BOOL ssh_pick_server(HWND owner,HWND anchor,SshLogin *login);
HWND ssh_view_create(HWND parent,HFONT font,const wchar_t *location);
BOOL ssh_view_connect(HWND view,const SshLogin *login);
BOOL ssh_view_navigate(HWND view,const wchar_t *location);
BOOL ssh_view_has_directory(HWND view,const wchar_t *name);
void ssh_view_action(HWND view,int action);
BOOL ssh_view_run(HWND view,const wchar_t *command);
BOOL ssh_view_location(HWND view,wchar_t *location,size_t capacity);
void ssh_view_title(HWND view,wchar_t *title,int capacity);
BOOL ssh_view_message(HWND view,MSG *message);
void ssh_view_focus(HWND view);
#endif
