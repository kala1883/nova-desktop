#ifndef NOVA_WORKSPACE_H
#define NOVA_WORKSPACE_H
#include <wchar.h>
#define MAX_WORKSPACES 8
#define MAX_APPS 20
#define NOVA_PATH_CAP 260
typedef struct { wchar_t name[64], target[NOVA_PATH_CAP]; } AppItem;
typedef struct { wchar_t name[40]; int app_count; AppItem apps[MAX_APPS]; } Workspace;
/* destination_index=-1 appends. Returns 1 changed, 0 unchanged, -1 invalid/full/duplicate. */
int workspace_move(Workspace *spaces,int count,int source,int index,int destination,int destination_index);
#endif
