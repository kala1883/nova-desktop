#include "../src/core/workspace.h"
#include "../src/core/launch_queue.h"
#include "../src/core/batch_task.h"
#include "../src/core/file_command.h"
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
    BatchTask task={0};assert(batch_task_add_directory(&task,L"C:\\repo-one\\")==1);assert(!wcscmp(task.directories[0],L"C:\\repo-one"));
    assert(batch_task_add_directory(&task,L"c:\\REPO-ONE")==0);assert(batch_task_add_directory(&task,L"D:\\repo-two")==1);
    assert(batch_task_remove_directory(&task,0)==1&&!wcscmp(task.directories[0],L"D:\\repo-two"));assert(!batch_task_remove_directory(&task,2));
    task=(BatchTask){0};for(int i=0;i<BATCH_DIRECTORY_LIMIT;i++){wchar_t path[40];swprintf(path,40,L"C:\\repo-%d",i);assert(batch_task_add_directory(&task,path)==1);}
    assert(batch_task_add_directory(&task,L"C:\\one-too-many")==-1);assert(batch_task_is_valid(&task));
    puts("PASS batch task normalization, duplicate rejection, removal, validation and capacity");
    wchar_t target[64];assert(batch_task_target(12345,target,64));assert(batch_task_target_id(target)==12345);
    assert(!batch_task_target_id(L"nova-batch:1x")&&!batch_task_target_id(L"nova-batch:-1")&&!batch_task_target_id(L"nova-batch:9223372036854775808")&&!batch_task_target_id(L"C:\\file.cmd"));
    static BatchTaskQueue batch_queue;BatchTask run={0},taken={0};run.id=1;run.mode=BATCH_PARALLEL;
    wcscpy(run.command,L"fake command");assert(batch_task_add_directory(&run,L"C:\\one"));
    assert(batch_task_queue_add(&batch_queue,&run)==1);assert(batch_task_queue_add(&batch_queue,&run)==0);
    wcscpy(run.command,L"changed");assert(batch_task_queue_take(&batch_queue,&taken));assert(!wcscmp(taken.command,L"fake command")&&taken.mode==BATCH_PARALLEL);
    assert(batch_task_queue_add(&batch_queue,&run)==0);
    for(int i=2;i<=BATCH_TASK_LIMIT;i++){run.id=i;assert(batch_task_queue_add(&batch_queue,&run)==1);}
    run.id=BATCH_TASK_LIMIT+1;assert(batch_task_queue_add(&batch_queue,&run)==-1);batch_queue.active_id=0;assert(batch_task_queue_take(&batch_queue,&taken)&&taken.id==2);
    batch_task_queue_cancel(&batch_queue);assert(!batch_queue.count&&batch_queue.active_id==2);
    puts("PASS stable internal task references and bounded FIFO snapshot queue with deduplication and cancellation");
    FileCommandList commands;file_command_defaults(&commands);assert(file_command_list_is_valid(&commands));
    assert(commands.count==1&&commands.default_index==0&&!wcscmp(commands.items[0].command,L"cd ."));
    assert(file_command_add(&commands,L"Git status",L"git status")==1);assert(file_command_add(&commands,L"git STATUS",L"dir")==0);
    commands.default_index=1;assert(file_command_remove(&commands,0)&&commands.count==1&&commands.default_index==0&&!wcscmp(commands.items[0].name,L"Git status"));
    assert(!file_command_remove(&commands,0));commands.items[0].command[0]=0;assert(!file_command_list_is_valid(&commands));
    puts("PASS command preset defaults, unique names, default adjustment, validation and minimum collection");
    return 0;
}
