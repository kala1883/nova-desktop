/* The backend is entirely in memory: never read/write the user's credentials. */
#include "../src/platform/ssh_credentials.c"
#include "fake_ssh_credentials.h"
#include <assert.h>
static SshLogin sample_login(int64_t id){
    SshLogin login={0};login.server.id=id;login.server.port=2222;lstrcpyW(login.server.host,L"fixture.example.invalid");lstrcpyW(login.server.user,L"Deploy");
    lstrcpyW(login.secret,L"synthetic-口令");lstrcpyW(login.key_file,L"C:\\fixture\\临时私钥");return login;
}
int wmain(void){
    TestCredentialVault *vault=calloc(1,sizeof(*vault));assert(vault);SshCredentialStore store=test_vault_store(vault);DWORD error=0;BOOL found=FALSE;
    SshLogin login=sample_login(123),loaded=login;assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found&&!loaded.secret[0]&&!loaded.key_file[0]);
    assert(ssh_credentials_save(&login,&error,&store));loaded=login;assert(ssh_credentials_load(&loaded,&found,&error,&store)&&found&&!wcscmp(login.secret,loaded.secret)&&!wcscmp(login.key_file,loaded.key_file));
    lstrcpyW(loaded.server.name,L"renamed");lstrcpyW(loaded.server.directory,L"/different");lstrcpyW(loaded.server.host,L"FIXTURE.EXAMPLE.INVALID");assert(ssh_credentials_load(&loaded,&found,&error,&store)&&found);
    loaded=login;loaded.server.port++;assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found&&!loaded.secret[0]&&!loaded.key_file[0]);
    loaded=login;lstrcpyW(loaded.server.user,L"deploy");assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found);
    loaded=login;lstrcpyW(loaded.server.host,L"other.example.invalid");assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found);
    loaded=sample_login(456);assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found);assert(ssh_credentials_save(&loaded,&error,&store));
    SshCredentialSnapshot original;assert(ssh_credentials_snapshot(123,&original,&error,&store));
    wmemset(login.secret,L'密',511);login.secret[511]=0;wmemset(login.key_file,L'钥',MAX_PATH-1);login.key_file[MAX_PATH-1]=0;
    assert(ssh_credentials_save(&login,&error,&store));loaded=login;assert(ssh_credentials_load(&loaded,&found,&error,&store)&&found&&wcslen(loaded.secret)==511&&wcslen(loaded.key_file)==MAX_PATH-1);
    wmemset(login.secret,L'x',512);assert(!ssh_credentials_save(&login,&error,&store)&&error==ERROR_INVALID_PARAMETER);assert(ssh_credentials_restore(123,&original,&error,&store));
    CredentialRecord malformed;memcpy(&malformed,original.bytes,sizeof(malformed));malformed.version=2;memcpy(vault->entries[0].data.bytes,&malformed,sizeof(malformed));
    loaded=sample_login(123);assert(!ssh_credentials_load(&loaded,&found,&error,&store)&&error==ERROR_REVISION_MISMATCH&&!found&&!loaded.secret[0]);
    SshCredentialSnapshot rejected;assert(!ssh_credentials_snapshot(123,&rejected,&error,&store)&&!rejected.exists&&!rejected.length);assert(ssh_credentials_restore(123,&original,&error,&store));
    memcpy(&malformed,original.bytes,sizeof(malformed));wmemset(malformed.key_file,L'x',MAX_PATH);memcpy(vault->entries[0].data.bytes,&malformed,sizeof(malformed));assert(!ssh_credentials_snapshot(123,&rejected,&error,&store)&&error==ERROR_INVALID_DATA);assert(ssh_credentials_restore(123,&original,&error,&store));
    vault->entries[0].data.length--;assert(!ssh_credentials_snapshot(123,&rejected,&error,&store)&&error==ERROR_INVALID_DATA);assert(ssh_credentials_restore(123,&original,&error,&store));
    vault->read_error=ERROR_NO_SUCH_LOGON_SESSION;loaded=sample_login(123);assert(!ssh_credentials_load(&loaded,&found,&error,&store)&&error==ERROR_NO_SUCH_LOGON_SESSION&&!loaded.secret[0]);vault->read_error=0;
    vault->write_error=ERROR_ACCESS_DENIED;login=sample_login(123);assert(!ssh_credentials_save(&login,&error,&store)&&error==ERROR_ACCESS_DENIED);vault->write_error=0;
    vault->remove_error=ERROR_ACCESS_DENIED;assert(!ssh_credentials_delete(123,&error,&store)&&error==ERROR_ACCESS_DENIED);vault->remove_error=0;
    assert(ssh_credentials_delete(123,&error,&store)&&ssh_credentials_delete(123,&error,&store));loaded=sample_login(123);assert(ssh_credentials_load(&loaded,&found,&error,&store)&&!found);
    login=sample_login(456);login.secret[0]=login.key_file[0]=0;assert(ssh_credentials_save(&login,&error,&store)&&ssh_credentials_load(&login,&found,&error,&store)&&!found);
    assert(!ssh_credentials_delete(0,&error,&store)&&error==ERROR_INVALID_PARAMETER);
    SecureZeroMemory(&original,sizeof(original));SecureZeroMemory(&malformed,sizeof(malformed));SecureZeroMemory(&login,sizeof(login));SecureZeroMemory(&loaded,sizeof(loaded));SecureZeroMemory(vault,sizeof(*vault));free(vault);
    puts("PASS saved SSH credentials: Unicode/maximum lengths, server and endpoint binding, rename retention, malformed/newer data refusal, rollback snapshots, read/write/delete failures and forgetting (fake vault only)");return 0;
}
