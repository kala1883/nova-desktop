#ifndef NOVA_SSH_CLIENT_H
#define NOVA_SSH_CLIENT_H
#include <windows.h>
#include "../core/server_profile.h"
#define SSH_ENTRY_LIMIT 10000
#define SSH_OUTPUT_CAP 65536
typedef struct SshLogin { SshServer server; wchar_t secret[512],key_file[MAX_PATH]; char fingerprint[65]; } SshLogin;
typedef struct SshEntry { wchar_t name[512]; unsigned long permissions; unsigned long long size; BOOL directory,link; } SshEntry;
typedef enum SshPhase {
    SSH_PHASE_NONE,SSH_PHASE_RESOLVE,SSH_PHASE_TCP,SSH_PHASE_HANDSHAKE,SSH_PHASE_HOST_KEY,
    SSH_PHASE_KEY_FILE,SSH_PHASE_KEY_PARSE,SSH_PHASE_AUTH,SSH_PHASE_SFTP,SSH_PHASE_DIRECTORY,SSH_PHASE_FILE,SSH_PHASE_COMMAND
} SshPhase;
typedef enum SshKeyKind { SSH_KEY_OTHER,SSH_KEY_OPENSSH,SSH_KEY_PEM,SSH_KEY_PUBLIC,SSH_KEY_PUTTY,SSH_KEY_UTF16 } SshKeyKind;
typedef struct SshResult {
    int code,exit_code; /* 0 success, 1 error, 2 host verification required, 3 cancelled/unknown. */
    SshPhase phase;int native_error,ssh_error;unsigned long sftp_error;BOOL cancelled;
    wchar_t error[1024],path[SSH_PATH_CAP]; char fingerprint[65],auth_methods[256];
    SshEntry *entries; int count;
    char *output; size_t output_length; BOOL output_truncated;
} SshResult;
typedef struct SshClient SshClient;
typedef void (*SshProgress)(void *context,int percent);
const wchar_t *ssh_phase_name(SshPhase phase);
SshKeyKind ssh_key_kind(const char *data,size_t length);
BOOL ssh_runtime_acquire(void);
void ssh_runtime_release(void);
SshClient *ssh_client_connect(const SshLogin *login,HANDLE cancel,SshResult *result);
void ssh_client_close(SshClient *client);
BOOL ssh_client_healthy(SshClient *client);
void ssh_client_set_progress(SshClient *client,SshProgress progress,void *context);
BOOL ssh_client_list(SshClient *client,const wchar_t *path,SshResult *result);
BOOL ssh_client_mkdir(SshClient *client,const wchar_t *path,SshResult *result);
BOOL ssh_client_rename(SshClient *client,const wchar_t *from,const wchar_t *to,SshResult *result);
BOOL ssh_client_delete(SshClient *client,const wchar_t *path,SshResult *result);
BOOL ssh_client_transfer(SshClient *client,BOOL upload,const wchar_t *local,const wchar_t *remote,SshResult *result);
BOOL ssh_client_copy(SshClient *client,const wchar_t *from,const wchar_t *to,SshResult *result);
BOOL ssh_client_exec(SshClient *client,const wchar_t *directory,const wchar_t *command,SshResult *result);
/* Called only by the connection's owning worker. read/write never wait for input. */
BOOL ssh_client_terminal_open(SshClient *client,const wchar_t *directory,int columns,int rows,SshResult *result);
BOOL ssh_client_terminal_read(SshClient *client,char *data,size_t capacity,size_t *length,BOOL *ended,SshResult *result);
BOOL ssh_client_terminal_write(SshClient *client,const char *data,size_t length,size_t *written,SshResult *result);
BOOL ssh_client_terminal_resize(SshClient *client,int columns,int rows,SshResult *result);
DWORD ssh_client_terminal_wait(SshClient *client,HANDLE request,HANDLE input,DWORD timeout);
void ssh_result_dispose(SshResult *result);
#endif
