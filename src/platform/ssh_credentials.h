#ifndef NOVA_SSH_CREDENTIALS_H
#define NOVA_SSH_CREDENTIALS_H
#include "ssh_client.h"

/* Windows' documented generic-credential limit. Some MinGW headers still
   advertise the older 512-byte value; do not change the platform headers. */
#define SSH_CREDENTIAL_BYTES (5*512)
typedef struct SshCredentialSnapshot {
    BYTE bytes[SSH_CREDENTIAL_BYTES];DWORD length;BOOL exists;
} SshCredentialSnapshot;
/* NULL selects Windows Credential Manager. Tests supply an in-memory store. */
typedef struct SshCredentialStore {
    void *context;
    BOOL (*read)(void *context,const wchar_t *target,BYTE *bytes,DWORD capacity,DWORD *length,DWORD *error);
    BOOL (*write)(void *context,const wchar_t *target,const BYTE *bytes,DWORD length,DWORD *error);
    BOOL (*remove)(void *context,const wchar_t *target,DWORD *error);
} SshCredentialStore;
BOOL ssh_credentials_load(SshLogin *login,BOOL *found,DWORD *error,const SshCredentialStore *store);
BOOL ssh_credentials_save(const SshLogin *login,DWORD *error,const SshCredentialStore *store);
BOOL ssh_credentials_delete(int64_t server_id,DWORD *error,const SshCredentialStore *store);
/* Caller must clear snapshots with SecureZeroMemory after commit/rollback. */
BOOL ssh_credentials_snapshot(int64_t server_id,SshCredentialSnapshot *snapshot,DWORD *error,const SshCredentialStore *store);
BOOL ssh_credentials_restore(int64_t server_id,const SshCredentialSnapshot *snapshot,DWORD *error,const SshCredentialStore *store);
#endif
