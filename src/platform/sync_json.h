#ifndef NOVA_SYNC_JSON_H
#define NOVA_SYNC_JSON_H
#include "sync_client.h"
typedef struct SyncBuffer { char *data;size_t size,capacity; } SyncBuffer;
BOOL sync_buffer_add(SyncBuffer *b,const char *value);
BOOL sync_buffer_string(SyncBuffer *b,const char *value);
char *sync_utf8(const wchar_t *text);
BOOL sync_wide(const char *text,wchar_t *out,int capacity);
BOOL sync_json_valid(sqlite3 *db,const char *text);
char *sync_json_patch(sqlite3 *db,const char *json,const char *path,const char *value,BOOL object);
BOOL sync_sha(const void *data,size_t length,char out[65]);
#endif
