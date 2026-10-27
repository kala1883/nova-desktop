#ifndef NOVA_SSH_SAVED_LOGIN_TEST_H
#define NOVA_SSH_SAVED_LOGIN_TEST_H
#include "fake_ssh_credentials.h"
static HWND saved_login_dialog(ServerDialog *d){
    EmptyDialog layout={0};layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;layout.dialog.cx=450;layout.dialog.cy=320;
    HWND dialog=CreateDialogIndirectParamW(GetModuleHandleW(NULL),&layout.dialog,NULL,server_proc,(LPARAM)d);assert(dialog);return dialog;
}
static void close_saved_login_dialog(HWND dialog,ServerDialog *d){DestroyWindow(dialog);DeleteObject(d->font);DeleteObject(d->brush);d->font=NULL;d->brush=NULL;}
static void assert_login_field(HWND dialog,int id,const wchar_t *expected){wchar_t value[512];GetDlgItemTextW(dialog,id,value,512);assert(!wcscmp(value,expected));SecureZeroMemory(value,sizeof(value));}
static void test_saved_login_dialog(const wchar_t *directory){
    assert(store_open(directory));TestCredentialVault *vault=calloc(1,sizeof(*vault));ServerDialog *d=calloc(1,sizeof(*d));assert(vault&&d);SshCredentialStore backend=test_vault_store(vault);SshLogin output={0};
    d->credential_store=&backend;d->out=&output;d->selected=0;d->servers.count=2;
    for(int i=0;i<2;i++){SshServer *s=&d->servers.items[i];s->id=100+i;s->port=2222;swprintf(s->name,SSH_NAME_CAP,L"Fixture %d",i);lstrcpyW(s->host,L"fixture.example.invalid");lstrcpyW(s->user,L"fixture");lstrcpyW(s->directory,L"/");}
    assert(store_save_ssh_servers(&d->servers));HWND dialog=saved_login_dialog(d);
    SetDlgItemTextW(dialog,S_KEY,L"C:\\fixture\\saved-one.key");SetDlgItemTextW(dialog,S_SECRET,L"example-passphrase-one");assert(save_server(dialog,d));
    d->selected=1;server_fields(dialog,d);assert_login_field(dialog,S_KEY,L"");assert_login_field(dialog,S_SECRET,L"");SetDlgItemTextW(dialog,S_KEY,L"C:\\fixture\\saved-two.key");assert(save_server(dialog,d));
    d->selected=0;server_fields(dialog,d);assert_login_field(dialog,S_KEY,L"C:\\fixture\\saved-one.key");assert_login_field(dialog,S_SECRET,L"example-passphrase-one");
    close_saved_login_dialog(dialog,d);assert(store_load_ssh_servers(&d->servers));dialog=saved_login_dialog(d);assert_login_field(dialog,S_KEY,L"C:\\fixture\\saved-one.key");assert_login_field(dialog,S_SECRET,L"example-passphrase-one");assert(GetWindowLongPtrW(GetDlgItem(dialog,S_SECRET),GWL_STYLE)&ES_PASSWORD);
    SetDlgItemTextW(dialog,S_NAME,L"Renamed fixture");SetDlgItemTextW(dialog,S_DIR,L"/different");assert_login_field(dialog,S_SECRET,L"example-passphrase-one");assert(save_server(dialog,d));
    SetDlgItemTextW(dialog,S_USER,L"other-user");assert_login_field(dialog,S_SECRET,L"");assert_login_field(dialog,S_KEY,L"");server_fields(dialog,d);assert_login_field(dialog,S_SECRET,L"example-passphrase-one");
    SendMessageW(dialog,WM_COMMAND,S_FORGET,0);assert_login_field(dialog,S_KEY,L"");assert_login_field(dialog,S_SECRET,L"");server_fields(dialog,d);assert_login_field(dialog,S_SECRET,L"");
    SetDlgItemTextW(dialog,S_SECRET,L"original-synthetic-secret");assert(save_server(dialog,d));
    SshServers *next=malloc(sizeof(*next));assert(next);*next=d->servers;lstrcpyW(next->items[0].name,L"Must roll back");SshLogin changed={0};changed.server=next->items[0];lstrcpyW(changed.secret,L"must-not-survive-failed-save");
    wchar_t file[MAX_PATH];swprintf(file,MAX_PATH,L"%ls\\runtime.json",directory);HANDLE locked=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(locked!=INVALID_HANDLE_VALUE);DWORD error=0;
    assert(persist_server_change(d,next,&changed,0,&error)==SERVER_SAVE_CONFIG_ERROR);assert(!wcscmp(d->servers.items[0].name,L"Renamed fixture"));SshLogin loaded={0};loaded.server=d->servers.items[0];BOOL found=FALSE;assert(ssh_credentials_load(&loaded,&found,&error,&backend)&&found&&!wcscmp(loaded.secret,L"original-synthetic-secret"));
    *next=d->servers;changed.server.id=999;lstrcpyW(changed.server.name,L"Fresh fixture");next->items[next->count++]=changed.server;
    assert(persist_server_change(d,next,&changed,0,&error)==SERVER_SAVE_CONFIG_ERROR);assert(ssh_credentials_load(&changed,&found,&error,&backend)&&!found);
    *next=d->servers;lstrcpyW(next->items[0].name,L"Must roll back");changed.server=next->items[0];lstrcpyW(changed.secret,L"rollback-error-is-reported");vault->fail_write_call=vault->writes+2;assert(persist_server_change(d,next,&changed,0,&error)==SERVER_SAVE_ROLLBACK_ERROR&&error==ERROR_ACCESS_DENIED);vault->fail_write_call=0;assert(ssh_credentials_save(&loaded,&error,&backend));CloseHandle(locked);
    vault->write_error=ERROR_ACCESS_DENIED;assert(persist_server_change(d,next,&changed,0,&error)==SERVER_SAVE_LOGIN_ERROR&&error==ERROR_ACCESS_DENIED&&!wcscmp(d->servers.items[0].name,L"Renamed fixture"));vault->write_error=0;
    *next=d->servers;next->items[0]=next->items[1];next->count=1;vault->remove_error=ERROR_ACCESS_DENIED;assert(persist_server_change(d,next,NULL,100,&error)==SERVER_SAVE_LOGIN_ERROR&&d->servers.count==2);vault->remove_error=0;
    assert(persist_server_change(d,next,NULL,100,&error)==SERVER_SAVE_OK&&d->servers.count==1);loaded.server.id=100;assert(ssh_credentials_load(&loaded,&found,&error,&backend)&&!found);loaded.server=d->servers.items[0];assert(ssh_credentials_load(&loaded,&found,&error,&backend)&&found&&!wcscmp(loaded.key_file,L"C:\\fixture\\saved-two.key"));
    HANDLE json=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(json!=INVALID_HANDLE_VALUE);char text[65536]={0};DWORD read=0;assert(ReadFile(json,text,sizeof(text)-1,&read,NULL));CloseHandle(json);assert(!strstr(text,"synthetic-secret")&&!strstr(text,"saved-two.key")&&!strstr(text,"example-passphrase"));
    close_saved_login_dialog(dialog,d);store_close();SecureZeroMemory(&output,sizeof(output));SecureZeroMemory(&loaded,sizeof(loaded));SecureZeroMemory(&changed,sizeof(changed));SecureZeroMemory(vault,sizeof(*vault));free(vault);SecureZeroMemory(d,sizeof(*d));free(d);free(next);
    puts("PASS saved SSH login UI: save/reopen, per-server refill, masked password, endpoint change clears, forget, profile removal, JSON exclusion, atomic rollback and error reporting (fake vault only)");
}
#endif
