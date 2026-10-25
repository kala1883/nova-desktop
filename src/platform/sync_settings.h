#ifndef NOVA_SYNC_SETTINGS_H
#define NOVA_SYNC_SETTINGS_H
#include <windows.h>
#include "../core/sync_model.h"
BOOL sync_settings_load(SyncSettings *value);
BOOL sync_settings_save(const SyncSettings *value);
/* Explicit one-time legacy import; original documents are never modified. */
BOOL sync_settings_import(const wchar_t *file,SyncSettings *value);
#endif
