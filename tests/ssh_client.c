#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform/ssh_client.h"
#include "../src/platform/app_log.h"
BOOL nova_english=FALSE;
typedef struct CancelRun {SshClient *client;SshResult result;const wchar_t *command;} CancelRun;
static DWORD WINAPI run_cancel(void *value){CancelRun *run=value;ssh_client_exec(run->client,L"/",run->command,&run->result);return 0;}
static void terminal_write(SshClient *client,const char *data){
    SshResult result={0};size_t length=strlen(data),offset=0;ULONGLONG until=GetTickCount64()+5000;
    while(offset<length&&GetTickCount64()<until){size_t n;assert(ssh_client_terminal_write(client,data+offset,length-offset,&n,&result));offset+=n;if(!n)Sleep(5);}assert(offset==length);
}
static void terminal_expect(SshClient *client,const char *expected){
    char output[16384]={0};size_t length=0;ULONGLONG until=GetTickCount64()+5000;SshResult result={0};
    while(GetTickCount64()<until){size_t received;BOOL ended;assert(ssh_client_terminal_read(client,output+length,sizeof(output)-length-1,&received,&ended,&result));length+=received;output[length]=0;if(strstr(output,expected))return;assert(!ended&&length<sizeof(output)-1);Sleep(5);}fprintf(stderr,"Missing terminal output: %s; received: %s\n",expected,output);assert(0);
}
int wmain(int argc,wchar_t **argv){
    assert(argc==5&&ssh_runtime_acquire());assert(nova_log_init(argv[2]));WSASetLastError(12345);nova_log_write(NOVA_LOG_INFO,"fixture","winsock_error_preserved",L"check=1");assert(WSAGetLastError()==12345);HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);assert(cancel);
    const char public_key[]="ssh-ed25519 AAAATEST fixture",putty_key[]="PuTTY-User-Key-File-3: ssh-rsa",private_header[]="-----BEGIN OPENSSH PRIVATE KEY-----";
    assert(ssh_key_kind(public_key,strlen(public_key))==SSH_KEY_PUBLIC);assert(ssh_key_kind(putty_key,strlen(putty_key))==SSH_KEY_PUTTY);assert(ssh_key_kind(private_header,strlen(private_header))==SSH_KEY_OPENSSH);assert(ssh_key_kind("\xff\xfe-\0",4)==SSH_KEY_UTF16);
    SshLogin *login=calloc(1,sizeof(*login));assert(login);login->server.id=1;login->server.port=_wtoi(argv[1]);wcscpy(login->server.host,L"127.0.0.1");wcscpy(login->server.user,L"fixture");wcscpy(login->secret,L"fixture-password");
    SshResult result={0};login->server.port=_wtoi(argv[4]);ULONGLONG refusal_started=GetTickCount64();assert(!ssh_client_connect(login,cancel,&result)&&result.code==1&&result.phase==SSH_PHASE_TCP&&result.native_error!=0&&GetTickCount64()-refusal_started<5000);ssh_result_dispose(&result);login->server.port=_wtoi(argv[1]);
    SshClient *client=ssh_client_connect(login,cancel,&result);if(result.code!=2)fwprintf(stderr,L"Host verification result %d: %ls\n",result.code,result.error);assert(!client&&result.code==2&&strlen(result.fingerprint)==64);strcpy(login->fingerprint,result.fingerprint);ssh_result_dispose(&result);
    wcscpy(login->secret,L"deliberately-incorrect-secret");assert(!ssh_client_connect(login,cancel,&result)&&result.code==1&&result.phase==SSH_PHASE_AUTH&&strstr(result.auth_methods,"password")&&!wcsstr(result.error,login->secret));ssh_result_dispose(&result);wcscpy(login->secret,L"fixture-password");
    wchar_t wrong_key[MAX_PATH];swprintf(wrong_key,MAX_PATH,L"%ls\\wrong.pub",argv[2]);HANDLE wrong_file=CreateFileW(wrong_key,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(wrong_file!=INVALID_HANDLE_VALUE);DWORD wrong_written;assert(WriteFile(wrong_file,public_key,(DWORD)strlen(public_key),&wrong_written,NULL));CloseHandle(wrong_file);wcscpy(login->key_file,wrong_key);assert(!ssh_client_connect(login,cancel,&result)&&result.phase==SSH_PHASE_KEY_PARSE&&wcsstr(result.error,L"id_ed25519.pub"));ssh_result_dispose(&result);DeleteFileW(wrong_key);
    wcscpy(login->key_file,wrong_key);assert(!ssh_client_connect(login,cancel,&result)&&result.phase==SSH_PHASE_KEY_FILE&&result.native_error==ERROR_FILE_NOT_FOUND);ssh_result_dispose(&result);login->key_file[0]=0;
    client=ssh_client_connect(login,cancel,&result);if(!client)fwprintf(stderr,L"Connect failed: %ls\n",result.error);assert(client);
    assert(ssh_client_list(client,L"/",&result));assert(result.entries&&result.count>=1);ssh_result_dispose(&result);
    assert(!ssh_client_list(client,L"/missing-fixture-directory",&result)&&result.phase==SSH_PHASE_DIRECTORY&&result.sftp_error);ssh_result_dispose(&result);
    assert(ssh_client_mkdir(client,L"/中文 ' $(x)",&result));assert(ssh_client_list(client,L"/中文 ' $(x)",&result));assert(!wcscmp(result.path,L"/中文 ' $(x)"));ssh_result_dispose(&result);
    assert(ssh_client_exec(client,L"/中文 ' $(x)",L"pwd",&result));assert(result.output&&strstr(result.output,"$(x)")&&result.exit_code==0);ssh_result_dispose(&result);
    wchar_t source[MAX_PATH],download[MAX_PATH];swprintf(source,MAX_PATH,L"%ls\\upload.txt",argv[2]);swprintf(download,MAX_PATH,L"%ls\\download.txt",argv[2]);HANDLE file=CreateFileW(source,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(file!=INVALID_HANDLE_VALUE);DWORD n;assert(WriteFile(file,"nova-ssh-content",16,&n,NULL)&&n==16);CloseHandle(file);
    assert(ssh_client_transfer(client,TRUE,source,L"/中文 ' $(x)/file.txt",&result));assert(!ssh_client_transfer(client,TRUE,source,L"/中文 ' $(x)/file.txt",&result));ssh_result_dispose(&result);
    assert(ssh_client_transfer(client,FALSE,download,L"/中文 ' $(x)/file.txt",&result));file=CreateFileW(download,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(file!=INVALID_HANDLE_VALUE);char bytes[20]={0};assert(ReadFile(file,bytes,16,&n,NULL)&&n==16&&!memcmp(bytes,"nova-ssh-content",16));CloseHandle(file);
    assert(ssh_client_copy(client,L"/中文 ' $(x)/file.txt",L"/copy.txt",&result));assert(ssh_client_rename(client,L"/copy.txt",L"/moved.txt",&result));assert(ssh_client_delete(client,L"/moved.txt",&result));
    wchar_t tree[MAX_PATH],deep[MAX_PATH],copied_tree[MAX_PATH];swprintf(tree,MAX_PATH,L"%ls\\tree",argv[2]);assert(CreateDirectoryW(tree,NULL));wcscpy(deep,tree);
    for(int depth=0;depth<30;depth++){wcscat(deep,L"\\n");assert(CreateDirectoryW(deep,NULL));}
    wcscat(deep,L"\\leaf.txt");file=CreateFileW(deep,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);assert(file!=INVALID_HANDLE_VALUE);assert(WriteFile(file,"deep",4,&n,NULL)&&n==4);CloseHandle(file);
    assert(ssh_client_transfer(client,TRUE,tree,L"/tree",&result));swprintf(copied_tree,MAX_PATH,L"%ls\\tree-copy",argv[2]);assert(ssh_client_transfer(client,FALSE,copied_tree,L"/tree",&result));wcscpy(deep,copied_tree);for(int depth=0;depth<30;depth++)wcscat(deep,L"\\n");wcscat(deep,L"\\leaf.txt");assert(GetFileAttributesW(deep)!=INVALID_FILE_ATTRIBUTES);assert(ssh_client_delete(client,L"/tree",&result));
    assert(ssh_client_exec(client,L"/",L"fail",&result)&&result.exit_code==7&&strstr(result.output,"fixture error"));ssh_result_dispose(&result);
    assert(ssh_client_exec(client,L"/",L"large",&result)&&result.output_truncated&&result.output_length==SSH_OUTPUT_CAP-1);ssh_result_dispose(&result);
    assert(ssh_client_terminal_open(client,L"/中文 ' $(x)",80,24,&result));terminal_expect(client,"fixture terminal");terminal_write(client,"pwd\r");terminal_expect(client,"/\xe4\xb8\xad\xe6\x96\x87 ' $(x)");
    terminal_write(client,"cd /persistent\r");terminal_expect(client,"$ ");terminal_write(client,"pwd\r");terminal_expect(client,"/persistent");
    assert(ssh_client_terminal_resize(client,101,37,&result));terminal_write(client,"size\r");terminal_expect(client,"101 37");terminal_write(client,"\x03");terminal_expect(client,"^C");terminal_write(client,"utf8\r");terminal_expect(client,"\xe4\xb8\xad\xe6\x96\x87");
    assert(ssh_client_list(client,L"/",&result));ssh_result_dispose(&result);terminal_write(client,"pwd\r");terminal_expect(client,"/persistent");
    terminal_write(client,"exit7\r");BOOL terminal_ended=FALSE;ULONGLONG terminal_until=GetTickCount64()+5000;while(!terminal_ended&&GetTickCount64()<terminal_until){char bytes[4096];size_t received;assert(ssh_client_terminal_read(client,bytes,sizeof(bytes),&received,&terminal_ended,&result));Sleep(5);}assert(terminal_ended&&result.exit_code==7);
    SshClient *denied=ssh_client_connect(login,cancel,&result);assert(denied);assert(!ssh_client_terminal_open(denied,L"/",499,24,&result)&&result.code==1&&result.phase==SSH_PHASE_COMMAND);ssh_client_close(denied);ssh_result_dispose(&result);
    assert(ssh_client_delete(client,L"/中文 ' $(x)",&result));assert(!ssh_client_delete(client,L"/",&result));ssh_result_dispose(&result);
    CancelRun run={client,{0},L"wait"};HANDLE thread=CreateThread(NULL,0,run_cancel,&run,0,NULL);assert(thread);Sleep(150);SetEvent(cancel);assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0&&run.result.code==3);CloseHandle(thread);ssh_result_dispose(&run.result);ssh_client_close(client);ResetEvent(cancel);
    login->fingerprint[0]=login->fingerprint[0]=='0'?'1':'0';assert(!ssh_client_connect(login,cancel,&result)&&result.code==1);ssh_result_dispose(&result);
    /* Restore the real pin and authenticate with an encrypted, Unicode-named Ed25519 key. */
    login->fingerprint[0]=0;assert(!ssh_client_connect(login,cancel,&result)&&result.code==2);strcpy(login->fingerprint,result.fingerprint);ssh_result_dispose(&result);
    wcscpy(login->key_file,argv[3]);wcscpy(login->secret,L"fixture-key-passphrase");client=ssh_client_connect(login,cancel,&result);if(!client)fwprintf(stderr,L"Key authentication: %ls\n",result.error);assert(client);
    CancelRun flood={client,{0},L"flood"};thread=CreateThread(NULL,0,run_cancel,&flood,0,NULL);assert(thread);Sleep(150);SetEvent(cancel);assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0&&flood.result.code==3&&flood.result.output_length<SSH_OUTPUT_CAP);CloseHandle(thread);ssh_result_dispose(&flood.result);ssh_client_close(client);
    SecureZeroMemory(login,sizeof(*login));free(login);CloseHandle(cancel);ssh_runtime_release();DeleteFileW(source);DeleteFileW(download);
    nova_log_close();puts("PASS real loopback SSH/SFTP: host verification, encrypted Ed25519 key, Unicode directories, current-directory command framing, upload/download integrity, no overwrite, copy/move/delete, exit status/stderr, bounded output, cancellation and host mismatch");return 0;
}
