#ifndef NOVA_SHELL_ICONS_H
#define NOVA_SHELL_ICONS_H
#include <windows.h>
/* Returned HICON is owned by caller. Never mutate/free the system image list. */
HICON shell_icon_without_overlay(const wchar_t *path);
#endif
