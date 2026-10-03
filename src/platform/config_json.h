#ifndef NOVA_CONFIG_JSON_H
#define NOVA_CONFIG_JSON_H
#include <windows.h>
typedef struct sqlite3 sqlite3;
/* JSON is the disk format; SQLite provides an in-memory transaction engine. */
BOOL config_json_load(sqlite3 *db,const wchar_t *directory,BOOL *initialized,wchar_t *error);
BOOL config_json_load_local(sqlite3 *db,const wchar_t *directory,BOOL *migrate,wchar_t *error);
BOOL config_json_legacy(sqlite3 *db,const wchar_t *directory,BOOL *imported,wchar_t *error);
BOOL config_json_commit(sqlite3 *db,const wchar_t *directory,wchar_t *error);
BOOL config_json_backup(const wchar_t *directory,wchar_t *error);
#endif
