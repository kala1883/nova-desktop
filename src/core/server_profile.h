#ifndef NOVA_SERVER_PROFILE_H
#define NOVA_SERVER_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#define SSH_SERVER_LIMIT 16
#define SSH_PATH_CAP 2048
#define SSH_NAME_CAP 64
typedef struct SshServer {
    int64_t id;
    wchar_t name[SSH_NAME_CAP],host[256],user[128],directory[SSH_PATH_CAP];
    int port;
} SshServer;
typedef struct SshServers { SshServer items[SSH_SERVER_LIMIT]; int count; } SshServers;
bool ssh_servers_valid(const SshServers *servers);
bool ssh_location_parse(const wchar_t *location,int64_t *id,const wchar_t **path);
bool ssh_location_format(wchar_t *out,size_t capacity,int64_t id,const wchar_t *path);
bool ssh_path_join(wchar_t *out,size_t capacity,const wchar_t *base,const wchar_t *name);
/* UTF-8 directory data is encoded as a single octal line for a fixed remote bootstrap. */
bool ssh_directory_payload(const unsigned char *path,size_t length,char *out,size_t capacity);
#endif
