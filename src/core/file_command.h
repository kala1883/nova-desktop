#ifndef NOVA_FILE_COMMAND_H
#define NOVA_FILE_COMMAND_H

#include <windows.h>

#define FILE_COMMAND_LIMIT 16
#define FILE_COMMAND_NAME_CAP 64
#define FILE_COMMAND_TEXT_CAP 2048

typedef struct FileCommand {
    wchar_t name[FILE_COMMAND_NAME_CAP];
    wchar_t command[FILE_COMMAND_TEXT_CAP];
} FileCommand;

typedef struct FileCommandList {
    FileCommand items[FILE_COMMAND_LIMIT];
    int count;
    int default_index;
} FileCommandList;

void file_command_defaults(FileCommandList *commands);
BOOL file_command_list_is_valid(const FileCommandList *commands);
int file_command_add(FileCommandList *commands, const wchar_t *name, const wchar_t *command);
BOOL file_command_remove(FileCommandList *commands, int index);

#endif
