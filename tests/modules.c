#include "../src/core/workspace.h"
#include "../src/core/launch_queue.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int calls;
static int fake_launch(const AppItem *item,void *context){(void)context;calls++;return wcscmp(item->name,L"B")!=0;}
int main(void){
    Workspace w[2]={0};w[0].app_count=3;
    for(int i=0;i<3;i++){w[0].apps[i].name[0]=(wchar_t)(L'A'+i);w[0].apps[i].target[0]=(wchar_t)(L'A'+i);}
    assert(workspace_move(w,2,0,0,0,2)==1);assert(w[0].apps[2].name[0]==L'A');
    assert(workspace_move(w,2,0,2,0,0)==1);assert(w[0].apps[0].name[0]==L'A');
    assert(workspace_move(w,2,0,0,0,-1)==1);assert(w[0].apps[2].name[0]==L'A');
    assert(workspace_move(w,2,0,0,1,-1)==1);assert(w[0].app_count==2&&w[1].app_count==1);
    w[1].apps[1]=w[0].apps[0];w[1].app_count=2;
    Workspace before[2];memcpy(before,w,sizeof(w));assert(workspace_move(w,2,0,0,1,-1)==-1);assert(!memcmp(before,w,sizeof(w)));
    assert(workspace_move(w,2,-1,0,1,0)==-1);assert(workspace_move(w,2,0,20,1,0)==-1);
    w[1].app_count=MAX_APPS;assert(workspace_move(w,2,0,0,1,0)==-1);
    puts("PASS reorder forward/back/append, cross-workspace move, duplicate/full/invalid rollback");
    LaunchQueue q={0};assert(launch_queue_begin(&q,&w[0]));assert(!launch_queue_begin(&q,&w[0]));
    wchar_t original=q.items[0].name[0];w[0].apps[0].name[0]=L'Z';assert(q.items[0].name[0]==original);
    assert(launch_queue_step(&q,fake_launch,NULL));assert(calls==1);assert(!launch_queue_step(&q,fake_launch,NULL));assert(calls==2);
    assert(!launch_queue_step(&q,fake_launch,NULL));assert(calls==2);
    w[1].app_count=1;assert(launch_queue_begin(&q,&w[1]));launch_queue_step(&q,fake_launch,NULL);assert(q.failed==1);
    assert(launch_queue_begin(&q,&w[0]));launch_queue_cancel(&q);assert(!launch_queue_step(&q,fake_launch,NULL));assert(calls==3);
    puts("PASS queue snapshot, busy guard, one dispatch per step, failure accounting and cancellation (no apps launched)");
    return 0;
}
