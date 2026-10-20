#ifndef NOVA_FAKE_SSH_CREDENTIALS_H
#define NOVA_FAKE_SSH_CREDENTIALS_H
#include "../src/platform/ssh_credentials.h"
#include <string.h>
#include <wchar.h>
typedef struct TestCredentialVault {
    struct {BOOL used;wchar_t target[80];SshCredentialSnapshot data;} entries[16];
    DWORD read_error,write_error,remove_error;int writes,removes,fail_write_call;
} TestCredentialVault;
static int test_vault_index(TestCredentialVault *vault,const wchar_t *target){for(int i=0;i<16;i++)if(vault->entries[i].used&&!wcscmp(vault->entries[i].target,target))return i;return -1;}
static BOOL test_vault_read(void *context,const wchar_t *target,BYTE *bytes,DWORD capacity,DWORD *length,DWORD *error){
    TestCredentialVault *vault=context;if(vault->read_error){*error=vault->read_error;return FALSE;}int i=test_vault_index(vault,target);
    if(i<0){*error=ERROR_NOT_FOUND;return FALSE;}if(vault->entries[i].data.length>capacity){*error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    *length=vault->entries[i].data.length;memcpy(bytes,vault->entries[i].data.bytes,*length);return TRUE;
}
static BOOL test_vault_write(void *context,const wchar_t *target,const BYTE *bytes,DWORD length,DWORD *error){
    TestCredentialVault *vault=context;vault->writes++;
    if(vault->write_error||vault->writes==vault->fail_write_call){*error=vault->write_error?vault->write_error:ERROR_ACCESS_DENIED;return FALSE;}
    int i=test_vault_index(vault,target);if(i<0)for(int n=0;n<16;n++)if(!vault->entries[n].used){i=n;break;}
    if(i<0||length>SSH_CREDENTIAL_BYTES){*error=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
    SecureZeroMemory(&vault->entries[i],sizeof(vault->entries[i]));vault->entries[i].used=TRUE;lstrcpyW(vault->entries[i].target,target);
    vault->entries[i].data.exists=TRUE;vault->entries[i].data.length=length;memcpy(vault->entries[i].data.bytes,bytes,length);return TRUE;
}
static BOOL test_vault_remove(void *context,const wchar_t *target,DWORD *error){
    TestCredentialVault *vault=context;vault->removes++;if(vault->remove_error){*error=vault->remove_error;return FALSE;}
    int i=test_vault_index(vault,target);if(i<0){*error=ERROR_NOT_FOUND;return FALSE;}SecureZeroMemory(&vault->entries[i],sizeof(vault->entries[i]));return TRUE;
}
static SshCredentialStore test_vault_store(TestCredentialVault *vault){SshCredentialStore store={vault,test_vault_read,test_vault_write,test_vault_remove};return store;}
#endif
