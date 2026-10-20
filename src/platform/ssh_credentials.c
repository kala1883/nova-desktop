#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <wincred.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "ssh_credentials.h"

#define SSH_CREDENTIAL_MAGIC 0x4e534331u
typedef struct CredentialRecord {
    DWORD magic,version;int64_t server_id;DWORD port;
    wchar_t host[256],user[128],key_file[MAX_PATH],secret[512];
} CredentialRecord;
_Static_assert(sizeof(CredentialRecord)<=SSH_CREDENTIAL_BYTES,"SSH credential exceeds Windows limit");

static BOOL native_read(void *context,const wchar_t *target,BYTE *bytes,DWORD capacity,DWORD *length,DWORD *error){
    (void)context;PCREDENTIALW value=NULL;
    if(!CredReadW(target,CRED_TYPE_GENERIC,0,&value)){*error=GetLastError();return FALSE;}
    BOOL ok=value->CredentialBlobSize<=capacity&&(!value->CredentialBlobSize||value->CredentialBlob);
    if(ok){*length=value->CredentialBlobSize;if(*length)memcpy(bytes,value->CredentialBlob,*length);}
    else *error=ERROR_INVALID_DATA;
    if(value->CredentialBlob)SecureZeroMemory(value->CredentialBlob,value->CredentialBlobSize);
    CredFree(value);return ok;
}
static BOOL native_write(void *context,const wchar_t *target,const BYTE *bytes,DWORD length,DWORD *error){
    (void)context;CREDENTIALW value={0};value.Type=CRED_TYPE_GENERIC;value.TargetName=(LPWSTR)target;
    value.CredentialBlob=(LPBYTE)bytes;value.CredentialBlobSize=length;value.Persist=CRED_PERSIST_LOCAL_MACHINE;
    if(CredWriteW(&value,0))return TRUE;
    *error=GetLastError();return FALSE;
}
static BOOL native_delete(void *context,const wchar_t *target,DWORD *error){
    (void)context;if(CredDeleteW(target,CRED_TYPE_GENERIC,0))return TRUE;
    *error=GetLastError();return FALSE;
}
static const SshCredentialStore native_store={NULL,native_read,native_write,native_delete};
static BOOL target_name(int64_t server_id,wchar_t *target,DWORD *error){
    if(server_id<=0){*error=ERROR_INVALID_PARAMETER;return FALSE;}
    swprintf(target,80,L"NOVA Desktop SSH login:%lld",(long long)server_id);return TRUE;
}
static BOOL terminated(const wchar_t *text,size_t capacity){return wmemchr(text,0,capacity)!=NULL;}
static BOOL valid_record(const SshCredentialSnapshot *snapshot,int64_t server_id,DWORD *error){
    if(snapshot->length!=sizeof(CredentialRecord)){*error=ERROR_INVALID_DATA;return FALSE;}
    CredentialRecord record;memcpy(&record,snapshot->bytes,sizeof(record));
    BOOL ok=record.magic==SSH_CREDENTIAL_MAGIC&&record.server_id==server_id&&record.port>0&&record.port<=65535&&
        terminated(record.host,256)&&record.host[0]&&terminated(record.user,128)&&record.user[0]&&
        terminated(record.key_file,MAX_PATH)&&terminated(record.secret,512);
    if(!ok)*error=ERROR_INVALID_DATA;
    else if(record.version!=1){*error=ERROR_REVISION_MISMATCH;ok=FALSE;}
    SecureZeroMemory(&record,sizeof(record));return ok;
}
BOOL ssh_credentials_snapshot(int64_t server_id,SshCredentialSnapshot *snapshot,DWORD *error,const SshCredentialStore *store){
    SecureZeroMemory(snapshot,sizeof(*snapshot));*error=0;if(!store)store=&native_store;
    wchar_t target[80];if(!target_name(server_id,target,error))return FALSE;
    if(!store->read(store->context,target,snapshot->bytes,sizeof(snapshot->bytes),&snapshot->length,error)){
        BOOL missing=*error==ERROR_NOT_FOUND;SecureZeroMemory(snapshot,sizeof(*snapshot));if(missing)*error=0;return missing;
    }
    snapshot->exists=TRUE;if(valid_record(snapshot,server_id,error))return TRUE;
    SecureZeroMemory(snapshot,sizeof(*snapshot));return FALSE;
}
BOOL ssh_credentials_restore(int64_t server_id,const SshCredentialSnapshot *snapshot,DWORD *error,const SshCredentialStore *store){
    *error=0;if(!store)store=&native_store;wchar_t target[80];if(!target_name(server_id,target,error))return FALSE;
    if(snapshot->exists)return valid_record(snapshot,server_id,error)&&store->write(store->context,target,snapshot->bytes,snapshot->length,error);
    return ssh_credentials_delete(server_id,error,store);
}
BOOL ssh_credentials_load(SshLogin *login,BOOL *found,DWORD *error,const SshCredentialStore *store){
    *found=FALSE;SecureZeroMemory(login->secret,sizeof(login->secret));SecureZeroMemory(login->key_file,sizeof(login->key_file));
    if(login->server.port<1||login->server.port>65535||!terminated(login->server.host,256)||!login->server.host[0]||!terminated(login->server.user,128)||!login->server.user[0]){*error=ERROR_INVALID_PARAMETER;return FALSE;}
    SshCredentialSnapshot snapshot;BOOL ok=ssh_credentials_snapshot(login->server.id,&snapshot,error,store);
    if(ok&&snapshot.exists){CredentialRecord record;memcpy(&record,snapshot.bytes,sizeof(record));
        /* Names and initial folders may change; credentials must never silently
           follow a changed connection endpoint or a case-sensitive SSH user. */
        if(record.port==(DWORD)login->server.port&&!lstrcmpiW(record.host,login->server.host)&&!lstrcmpW(record.user,login->server.user)){
            lstrcpyW(login->secret,record.secret);lstrcpyW(login->key_file,record.key_file);*found=record.secret[0]||record.key_file[0];
        }
        SecureZeroMemory(&record,sizeof(record));
    }
    SecureZeroMemory(&snapshot,sizeof(snapshot));return ok;
}
BOOL ssh_credentials_save(const SshLogin *login,DWORD *error,const SshCredentialStore *store){
    *error=0;if(!store)store=&native_store;wchar_t target[80];if(!target_name(login->server.id,target,error))return FALSE;
    if(login->server.port<1||login->server.port>65535||!terminated(login->server.host,256)||!login->server.host[0]||
       !terminated(login->server.user,128)||!login->server.user[0]||!terminated(login->secret,512)||!terminated(login->key_file,MAX_PATH)){
        *error=ERROR_INVALID_PARAMETER;return FALSE;
    }
    if(!login->secret[0]&&!login->key_file[0])return ssh_credentials_delete(login->server.id,error,store);
    CredentialRecord record;ZeroMemory(&record,sizeof(record));record.magic=SSH_CREDENTIAL_MAGIC;record.version=1;record.server_id=login->server.id;record.port=(DWORD)login->server.port;
    lstrcpyW(record.host,login->server.host);lstrcpyW(record.user,login->server.user);lstrcpyW(record.key_file,login->key_file);lstrcpyW(record.secret,login->secret);
    BOOL ok=store->write(store->context,target,(const BYTE*)&record,sizeof(record),error);SecureZeroMemory(&record,sizeof(record));return ok;
}
BOOL ssh_credentials_delete(int64_t server_id,DWORD *error,const SshCredentialStore *store){
    *error=0;if(!store)store=&native_store;wchar_t target[80];if(!target_name(server_id,target,error))return FALSE;
    BOOL ok=store->remove(store->context,target,error);if(!ok&&*error==ERROR_NOT_FOUND){*error=0;ok=TRUE;}return ok;
}
