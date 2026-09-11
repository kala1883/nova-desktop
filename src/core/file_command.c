#include "file_command.h"
#include <string.h>
#include <wchar.h>

static BOOL has_text(const wchar_t *value){
    if(!value)return FALSE;
    while(*value==L' '||*value==L'\t'||*value==L'\r'||*value==L'\n')value++;
    return *value!=0;
}

void file_command_defaults(FileCommandList *commands){
    if(!commands)return;
    ZeroMemory(commands,sizeof(*commands));
    commands->count=1;
    commands->default_index=0;
    lstrcpyW(commands->items[0].name,L"cmd.exe");
    lstrcpyW(commands->items[0].command,L"cd .");
}

BOOL file_command_list_is_valid(const FileCommandList *commands){
    if(!commands||commands->count<1||commands->count>FILE_COMMAND_LIMIT||
       commands->default_index<0||commands->default_index>=commands->count)return FALSE;
    for(int i=0;i<commands->count;i++){
        const FileCommand *item=&commands->items[i];
        if(!has_text(item->name)||!has_text(item->command)||
           wcslen(item->name)>=FILE_COMMAND_NAME_CAP||wcslen(item->command)>=FILE_COMMAND_TEXT_CAP)return FALSE;
        for(int j=0;j<i;j++)if(!lstrcmpiW(item->name,commands->items[j].name))return FALSE;
    }
    return TRUE;
}

int file_command_add(FileCommandList *commands,const wchar_t *name,const wchar_t *command){
    if(!commands||commands->count<0||commands->count>=FILE_COMMAND_LIMIT||!has_text(name)||!has_text(command)||
       wcslen(name)>=FILE_COMMAND_NAME_CAP||wcslen(command)>=FILE_COMMAND_TEXT_CAP)return -1;
    for(int i=0;i<commands->count;i++)if(!lstrcmpiW(name,commands->items[i].name))return 0;
    FileCommand *item=&commands->items[commands->count++];
    lstrcpynW(item->name,name,FILE_COMMAND_NAME_CAP);
    lstrcpynW(item->command,command,FILE_COMMAND_TEXT_CAP);
    if(commands->count==1)commands->default_index=0;
    return 1;
}

BOOL file_command_remove(FileCommandList *commands,int index){
    if(!commands||commands->count<=1||index<0||index>=commands->count)return FALSE;
    memmove(&commands->items[index],&commands->items[index+1],
            (size_t)(commands->count-index-1)*sizeof(commands->items[0]));
    commands->count--;
    if(commands->default_index==index)commands->default_index=0;
    else if(commands->default_index>index)commands->default_index--;
    ZeroMemory(&commands->items[commands->count],sizeof(commands->items[0]));
    return TRUE;
}
