#define WIN32_LEAN_AND_MEAN
#include "shell_icons.h"
#include <shellapi.h>
#include <commctrl.h>
HICON shell_icon_without_overlay(const wchar_t *path){
    SHFILEINFOW info={0};
    HIMAGELIST system=(HIMAGELIST)SHGetFileInfoW(path,0,&info,sizeof(info),SHGFI_SYSICONINDEX|SHGFI_LARGEICON);
    if(!system)return NULL;
    return ImageList_GetIcon(system,info.iIcon,ILD_NORMAL);
}
