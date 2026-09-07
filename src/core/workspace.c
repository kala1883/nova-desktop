#include "workspace.h"
#include <string.h>
int workspace_move(Workspace *spaces,int count,int source,int index,int destination,int at){
    if(!spaces||count<1||count>MAX_WORKSPACES||source<0||source>=count||destination<0||destination>=count)return -1;
    Workspace *from=&spaces[source],*to=&spaces[destination];
    if(from->app_count<0||from->app_count>MAX_APPS||to->app_count<0||to->app_count>MAX_APPS||index<0||index>=from->app_count)return -1;
    if(at<0)at=to->app_count;
    if(at>to->app_count)return -1;
    if(source==destination){
        if(at==from->app_count)at--;
        if(at==index)return 0;
        AppItem item=from->apps[index];
        if(index<at)memmove(&from->apps[index],&from->apps[index+1],(size_t)(at-index)*sizeof(item));
        else memmove(&from->apps[at+1],&from->apps[at],(size_t)(index-at)*sizeof(item));
        from->apps[at]=item;return 1;
    }
    if(to->app_count==MAX_APPS)return -1;
    for(int i=0;i<to->app_count;i++)if(wcscmp(to->apps[i].target,from->apps[index].target)==0)return -1;
    AppItem item=from->apps[index];memmove(&to->apps[at+1],&to->apps[at],(size_t)(to->app_count-at)*sizeof(item));to->apps[at]=item;to->app_count++;
    memmove(&from->apps[index],&from->apps[index+1],(size_t)(from->app_count-index-1)*sizeof(item));from->app_count--;return 1;
}
