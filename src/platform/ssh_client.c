#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#define LIBSSH2_STATIC
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../../third_party/libssh2/include/libssh2.h"
#include "../../third_party/libssh2/include/libssh2_sftp.h"
#include "ssh_client.h"
#include "../i18n.h"

struct SshClient { SOCKET socket; LIBSSH2_SESSION *session; LIBSSH2_SFTP *sftp; LIBSSH2_CHANNEL *terminal; WSAEVENT terminal_event; HANDLE cancel; ULONGLONG deadline,last_progress; int visited,native_error; SshPhase phase;SshProgress progress;void *progress_context; };
const wchar_t *ssh_phase_name(SshPhase phase){
    switch(phase){
    case SSH_PHASE_RESOLVE:return nova_text(L"解析主机地址",L"Resolve host address");
    case SSH_PHASE_TCP:return nova_text(L"连接 SSH 端口",L"Connect to SSH port");
    case SSH_PHASE_HANDSHAKE:return nova_text(L"SSH 握手与算法协商",L"SSH handshake and algorithm negotiation");
    case SSH_PHASE_HOST_KEY:return nova_text(L"核验主机指纹",L"Verify host fingerprint");
    case SSH_PHASE_KEY_FILE:return nova_text(L"读取私钥文件",L"Read private-key file");
    case SSH_PHASE_KEY_PARSE:return nova_text(L"解析或解密私钥",L"Parse or decrypt private key");
    case SSH_PHASE_AUTH:return nova_text(L"账号认证",L"Account authentication");
    case SSH_PHASE_SFTP:return nova_text(L"初始化 SFTP 子系统",L"Initialize SFTP subsystem");
    case SSH_PHASE_DIRECTORY:return nova_text(L"读取远程目录",L"Read remote directory");
    case SSH_PHASE_FILE:return nova_text(L"远程文件操作",L"Remote file operation");
    case SSH_PHASE_COMMAND:return nova_text(L"执行远程命令",L"Execute remote command");
    default:return nova_text(L"准备 SSH 操作",L"Prepare SSH operation");
    }
}
static BOOL key_prefix(const char *data,size_t length,const char *prefix){size_t n=strlen(prefix);return length>=n&&!memcmp(data,prefix,n);}
SshKeyKind ssh_key_kind(const char *data,size_t length){
    if(!data||!length)return SSH_KEY_OTHER;
    if(length>=2&&(((unsigned char)data[0]==0xff&&(unsigned char)data[1]==0xfe)||((unsigned char)data[0]==0xfe&&(unsigned char)data[1]==0xff)))return SSH_KEY_UTF16;
    if(length>=3&&!memcmp(data,"\xef\xbb\xbf",3)){data+=3;length-=3;}
    while(length&&(*data==' '||*data=='\t'||*data=='\r'||*data=='\n')){data++;length--;}
    if(key_prefix(data,length,"ssh-rsa ")||key_prefix(data,length,"ssh-ed25519 ")||key_prefix(data,length,"ecdsa-sha2-")||key_prefix(data,length,"sk-ssh-")||key_prefix(data,length,"sk-ecdsa-")||key_prefix(data,length,"-----BEGIN PUBLIC KEY-----")||key_prefix(data,length,"-----BEGIN RSA PUBLIC KEY-----")||key_prefix(data,length,"---- BEGIN SSH2 PUBLIC KEY ----"))return SSH_KEY_PUBLIC;
    if(key_prefix(data,length,"PuTTY-User-Key-File-"))return SSH_KEY_PUTTY;
    if(key_prefix(data,length,"-----BEGIN OPENSSH PRIVATE KEY-----"))return SSH_KEY_OPENSSH;
    if(key_prefix(data,length,"-----BEGIN RSA PRIVATE KEY-----")||key_prefix(data,length,"-----BEGIN EC PRIVATE KEY-----")||key_prefix(data,length,"-----BEGIN DSA PRIVATE KEY-----")||key_prefix(data,length,"-----BEGIN PRIVATE KEY-----")||key_prefix(data,length,"-----BEGIN ENCRYPTED PRIVATE KEY-----"))return SSH_KEY_PEM;
    return SSH_KEY_OTHER;
}
void ssh_client_set_progress(SshClient *c,SshProgress progress,void *context){c->progress=progress;c->progress_context=context;}
static void progress(SshClient *c,unsigned long long done,unsigned long long total){ULONGLONG now=GetTickCount64();if(c->progress&&(now-c->last_progress>=200||done==total)){c->last_progress=now;c->progress(c->progress_context,total?(int)(100.0*(double)done/(double)total):0);}}
static int runtime_users;
BOOL ssh_runtime_acquire(void){
    if(!runtime_users){WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return FALSE;if(libssh2_init(0)){WSACleanup();return FALSE;}}
    runtime_users++;return TRUE;
}
void ssh_runtime_release(void){if(runtime_users>0&&!--runtime_users){libssh2_exit();WSACleanup();}}
static BOOL utf8(const wchar_t *value,char *out,int cap){return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,out,cap,NULL,NULL)>0;}
static BOOL wide(const char *value,int length,wchar_t *out,int cap){
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,length,out,cap-1);if(n<=0)return FALSE;out[n]=0;return TRUE;
}
static BOOL waiting(SshClient *c){
    if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0||GetTickCount64()>=c->deadline)return FALSE;
    fd_set reads,writes;FD_ZERO(&reads);FD_ZERO(&writes);int directions=c->session?libssh2_session_block_directions(c->session):3;
    if(directions&LIBSSH2_SESSION_BLOCK_INBOUND)FD_SET(c->socket,&reads);
    if(directions&LIBSSH2_SESSION_BLOCK_OUTBOUND)FD_SET(c->socket,&writes);
    if(!directions){FD_SET(c->socket,&reads);FD_SET(c->socket,&writes);}
    struct timeval delay={0,100000};int rc=select(0,&reads,&writes,NULL,&delay);if(rc==SOCKET_ERROR)c->native_error=WSAGetLastError();return rc!=SOCKET_ERROR;
}
#define RETRY(c,rc,expr) do { (rc)=(expr); } while((rc)==LIBSSH2_ERROR_EAGAIN&&waiting(c))
static BOOL failed(SshClient *c,SshResult *r,const wchar_t *operation){
    r->cancelled=WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0;BOOL timeout=GetTickCount64()>=c->deadline;
    r->code=r->cancelled||timeout?3:1;r->phase=c->phase;r->native_error=timeout?WSAETIMEDOUT:c->native_error;
    char *detail=NULL;int length=0;wchar_t message[512]=L"",system[256]=L"";
    if(c->session){r->ssh_error=libssh2_session_last_errno(c->session);libssh2_session_last_error(c->session,&detail,&length,0);if(detail&&length>0){int n=MultiByteToWideChar(CP_UTF8,0,detail,min(length,511),message,511);if(n>0)message[n]=0;}}
    if(c->sftp)r->sftp_error=libssh2_sftp_last_error(c->sftp);
    if(r->native_error)FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,(DWORD)r->native_error,0,system,256,NULL);
    swprintf(r->error,1024,nova_text(L"%ls失败。%ls%ls%ls",L"%ls failed. %ls%ls%ls"),operation,message,system,
        c->sftp?nova_text(L" 请检查 SFTP 权限和目标是否已存在。",L" Check SFTP permissions and whether the destination already exists."):L"");
    if(r->cancelled)lstrcpyW(r->error,nova_text(L"已取消等待。远端操作的最终结果可能未知；请刷新确认，勿自动重试。",L"Waiting cancelled. The final remote result may be unknown; refresh to check and do not retry automatically."));
    else if(timeout)lstrcpyW(r->error,nova_text(L"SSH 操作超时；请查看失败阶段。已发出的远端操作结果可能未知。",L"SSH operation timed out; check the failed stage. Operations already sent may have an unknown remote result."));
    return FALSE;
}
static void begin(SshClient *c){c->deadline=GetTickCount64()+30000;c->visited=0;c->native_error=0;c->phase=SSH_PHASE_FILE;}
static BOOL path_bytes(SshClient *c,const wchar_t *path,char *out,SshResult *r){
    if(path&&path[0]==L'/'&&wcslen(path)<SSH_PATH_CAP-40&&utf8(path,out,SSH_PATH_CAP*4))return TRUE;
    return failed(c,r,nova_text(L"远程路径无效或过长",L"Invalid or oversized remote path"));
}
SshClient *ssh_client_connect(const SshLogin *login,HANDLE cancel,SshResult *r){
    SshClient *c=calloc(1,sizeof(*c));if(!c){r->code=1;lstrcpyW(r->error,nova_text(L"内存不足，无法创建 SSH 会话。",L"Not enough memory to create an SSH session."));return NULL;}
    c->socket=INVALID_SOCKET;c->cancel=cancel;begin(c);wchar_t port[16];swprintf(port,16,L"%d",login->server.port);
    ADDRINFOW hints={0},*addresses=NULL;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_UNSPEC;
    c->phase=SSH_PHASE_RESOLVE;int lookup=GetAddrInfoW(login->server.host,port,&hints,&addresses);if(lookup){c->native_error=lookup;goto fail;}
    c->phase=SSH_PHASE_TCP;
    for(ADDRINFOW *a=addresses;a;a=a->ai_next){
        if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)break;
        c->socket=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(c->socket==INVALID_SOCKET){c->native_error=WSAGetLastError();continue;}
        u_long nonblocking=1;if(ioctlsocket(c->socket,FIONBIO,&nonblocking)){c->native_error=WSAGetLastError();closesocket(c->socket);c->socket=INVALID_SOCKET;continue;}int rc=connect(c->socket,a->ai_addr,(int)a->ai_addrlen);
        if(rc==0){c->native_error=0;break;}
        c->native_error=WSAGetLastError();
        if(c->native_error==WSAEWOULDBLOCK){
            while(WaitForSingleObject(cancel,0)!=WAIT_OBJECT_0&&GetTickCount64()<c->deadline){fd_set writes,errors;FD_ZERO(&writes);FD_ZERO(&errors);FD_SET(c->socket,&writes);FD_SET(c->socket,&errors);struct timeval delay={0,100000};
                int ready=select(0,NULL,&writes,&errors,&delay);if(ready==SOCKET_ERROR){c->native_error=WSAGetLastError();break;}
                if(ready>0){int error=0,n=sizeof(error);if(getsockopt(c->socket,SOL_SOCKET,SO_ERROR,(char*)&error,&n))c->native_error=WSAGetLastError();else{c->native_error=error;if(!error&&FD_ISSET(c->socket,&writes))rc=0;}break;}
            }
        }
        if(!rc)break;
        closesocket(c->socket);c->socket=INVALID_SOCKET;
    }
    FreeAddrInfoW(addresses);addresses=NULL;if(c->socket==INVALID_SOCKET)goto fail;
    c->native_error=0;c->phase=SSH_PHASE_HANDSHAKE;c->session=libssh2_session_init();if(!c->session){c->native_error=ERROR_NOT_ENOUGH_MEMORY;goto fail;}libssh2_session_set_blocking(c->session,0);
    if(libssh2_session_method_pref(c->session,LIBSSH2_METHOD_KEX,"curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256,diffie-hellman-group16-sha512,diffie-hellman-group14-sha256")||
       libssh2_session_method_pref(c->session,LIBSSH2_METHOD_HOSTKEY,"ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256")||
       libssh2_session_method_pref(c->session,LIBSSH2_METHOD_MAC_CS,"hmac-sha2-256,hmac-sha2-256-etm@openssh.com,hmac-sha2-512,hmac-sha2-512-etm@openssh.com")||
       libssh2_session_method_pref(c->session,LIBSSH2_METHOD_MAC_SC,"hmac-sha2-256,hmac-sha2-256-etm@openssh.com,hmac-sha2-512,hmac-sha2-512-etm@openssh.com")||
       libssh2_session_method_pref(c->session,LIBSSH2_METHOD_SIGN_ALGO,"ssh-ed25519,ecdsa-sha2-nistp256,ecdsa-sha2-nistp384,ecdsa-sha2-nistp521,rsa-sha2-512,rsa-sha2-256"))goto fail;
    int rc;RETRY(c,rc,libssh2_session_handshake(c->session,c->socket));if(rc)goto fail;
    c->phase=SSH_PHASE_HOST_KEY;r->phase=SSH_PHASE_HOST_KEY;const unsigned char *hash=(const unsigned char*)libssh2_hostkey_hash(c->session,LIBSSH2_HOSTKEY_HASH_SHA256);if(!hash)goto fail;
    for(int i=0;i<32;i++)snprintf(r->fingerprint+i*2,3,"%02x",hash[i]);
    if(!login->fingerprint[0]){r->code=2;ssh_client_close(c);return NULL;}
    if(strcmp(login->fingerprint,r->fingerprint)){r->code=1;lstrcpyW(r->error,nova_text(L"服务器主机指纹已变化，连接已阻止。请独立核验服务器身份。",L"The server host fingerprint changed. Connection blocked; verify the server identity independently."));ssh_client_close(c);return NULL;}
    c->phase=SSH_PHASE_AUTH;char user[512],secret[2048]={0};if(!utf8(login->server.user,user,sizeof(user))||!utf8(login->secret,secret,sizeof(secret))){c->native_error=GetLastError();SecureZeroMemory(secret,sizeof(secret));goto fail;}
    char *methods=NULL;do{methods=libssh2_userauth_list(c->session,user,(unsigned)strlen(user));}while(!methods&&libssh2_session_last_errno(c->session)==LIBSSH2_ERROR_EAGAIN&&waiting(c));
    if(methods)snprintf(r->auth_methods,sizeof(r->auth_methods),"%s",methods);
    if(!methods&&!libssh2_userauth_authenticated(c->session)){SecureZeroMemory(secret,sizeof(secret));goto fail;}
    if(libssh2_userauth_authenticated(c->session))rc=0;
    else if(login->key_file[0]){
        c->phase=SSH_PHASE_KEY_FILE;
        HANDLE file=CreateFileW(login->key_file,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);char *key=NULL;DWORD read=0;LARGE_INTEGER size={0};
        if(file==INVALID_HANDLE_VALUE){c->native_error=GetLastError();SecureZeroMemory(secret,sizeof(secret));goto fail;}
        BOOL ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=1024*1024;
        if(ok)key=calloc((size_t)size.QuadPart+1,1);
        ok=ok&&key&&ReadFile(file,key,(DWORD)size.QuadPart,&read,NULL)&&read==(DWORD)size.QuadPart;if(!ok)c->native_error=GetLastError()?GetLastError():ERROR_INVALID_DATA;CloseHandle(file);
        SshKeyKind kind=ok?ssh_key_kind(key,read):SSH_KEY_OTHER;
        if(ok&&(kind==SSH_KEY_PUBLIC||kind==SSH_KEY_PUTTY||kind==SSH_KEY_UTF16)){
            r->code=1;r->phase=SSH_PHASE_KEY_PARSE;r->ssh_error=LIBSSH2_ERROR_FILE;
            lstrcpyW(r->error,kind==SSH_KEY_PUBLIC?nova_text(L"所选文件是公钥，不能作为私钥登录。公钥放在服务器；请在本机选择对应的私钥，例如 id_ed25519，而不是 id_ed25519.pub。",L"The selected file is a public key, not a login private key. Keep the public key on the server and select its matching local private key, such as id_ed25519 instead of id_ed25519.pub."):
                kind==SSH_KEY_PUTTY?nova_text(L"所选文件是 PuTTY PPK 私钥。请先在密钥工具中导出为 OpenSSH 私钥，再选择导出的私钥文件。原文件未修改。",L"The selected file is a PuTTY PPK private key. Export it as an OpenSSH private key with your key tool, then select the exported private key. The original file was not changed."):
                nova_text(L"所选密钥文件使用 UTF-16 编码。请选择密钥工具导出的 ASCII/UTF-8 OpenSSH 或 PEM 私钥。原文件未修改。",L"The selected key file uses UTF-16 encoding. Select an ASCII/UTF-8 OpenSSH or PEM private key exported by your key tool. The original file was not changed."));
            SecureZeroMemory(secret,sizeof(secret));SecureZeroMemory(key,(size_t)size.QuadPart+1);free(key);ssh_client_close(c);return NULL;
        }
        if(ok){const char *private_data=key;size_t private_size=read;if(private_size>=3&&!memcmp(private_data,"\xef\xbb\xbf",3)){private_data+=3;private_size-=3;}c->phase=SSH_PHASE_AUTH;RETRY(c,rc,libssh2_userauth_publickey_frommemory(c->session,user,strlen(user),NULL,0,private_data,private_size,secret));if(rc==LIBSSH2_ERROR_FILE||rc==LIBSSH2_ERROR_KEYFILE_AUTH_FAILED)c->phase=SSH_PHASE_KEY_PARSE;}
        else rc=-1;
        if(key){SecureZeroMemory(key,(size_t)size.QuadPart+1);free(key);}
    }else {RETRY(c,rc,libssh2_userauth_password(c->session,user,secret));}
    SecureZeroMemory(secret,sizeof(secret));if(rc)goto fail;
    c->phase=SSH_PHASE_SFTP;
    do{c->sftp=libssh2_sftp_init(c->session);}while(!c->sftp&&libssh2_session_last_errno(c->session)==LIBSSH2_ERROR_EAGAIN&&waiting(c));
    if(!c->sftp)goto fail;
    return c;
fail:
    if(addresses)FreeAddrInfoW(addresses);
    failed(c,r,ssh_phase_name(c->phase));ssh_client_close(c);return NULL;
}
void ssh_client_close(SshClient *c){
    if(!c)return;
    /* Closing the socket makes nonblocking cleanup finite, including error paths. */
    if(c->socket!=INVALID_SOCKET){shutdown(c->socket,SD_BOTH);closesocket(c->socket);c->socket=INVALID_SOCKET;}
    if(c->terminal_event)WSACloseEvent(c->terminal_event);
    if(c->terminal)libssh2_channel_free(c->terminal);
    if(c->sftp)libssh2_sftp_shutdown(c->sftp);
    if(c->session)libssh2_session_free(c->session);
    free(c);
}
BOOL ssh_client_healthy(SshClient *c){
    if(!c||c->socket==INVALID_SOCKET||!c->session)return FALSE;
    int error=libssh2_session_last_errno(c->session);
    return error!=LIBSSH2_ERROR_SOCKET_SEND&&error!=LIBSSH2_ERROR_SOCKET_RECV&&error!=LIBSSH2_ERROR_SOCKET_DISCONNECT&&error!=LIBSSH2_ERROR_BAD_SOCKET&&error!=LIBSSH2_ERROR_PROTO;
}
static LIBSSH2_SFTP_HANDLE *open_remote(SshClient *c,const char *path,unsigned long flags,long mode,int type){
    LIBSSH2_SFTP_HANDLE *h;
    do{h=libssh2_sftp_open_ex(c->sftp,path,(unsigned)strlen(path),flags,mode,type);}while(!h&&libssh2_session_last_errno(c->session)==LIBSSH2_ERROR_EAGAIN&&waiting(c));return h;
}
static BOOL close_remote(SshClient *c,LIBSSH2_SFTP_HANDLE *h){int rc;RETRY(c,rc,libssh2_sftp_close(h));return rc==0;}
BOOL ssh_client_list(SshClient *c,const wchar_t *path,SshResult *r){
    begin(c);c->phase=SSH_PHASE_DIRECTORY;char encoded[SSH_PATH_CAP*4],canonical[SSH_PATH_CAP*4];if(!path_bytes(c,path,encoded,r))return FALSE;
    int rc;RETRY(c,rc,libssh2_sftp_realpath(c->sftp,encoded,canonical,sizeof(canonical)-1));
    if(rc<=0||rc>=(int)sizeof(canonical)-1||!wide(canonical,rc,r->path,SSH_PATH_CAP-40))return failed(c,r,nova_text(L"解析远程目录",L"Resolve remote directory"));
    canonical[rc]=0;LIBSSH2_SFTP_HANDLE *h=open_remote(c,canonical,0,0,LIBSSH2_SFTP_OPENDIR);if(!h)return failed(c,r,nova_text(L"打开远程目录",L"Open remote directory"));
    int capacity=128;r->entries=calloc((size_t)capacity,sizeof(*r->entries));BOOL ok=r->entries!=NULL;
    while(ok){
        char name[2048];LIBSSH2_SFTP_ATTRIBUTES attr={0};RETRY(c,rc,libssh2_sftp_readdir_ex(h,name,sizeof(name)-1,NULL,0,&attr));
        if(!rc)break;
        if(rc<0||rc>=(int)sizeof(name)-1){ok=FALSE;break;}name[rc]=0;if(!strcmp(name,".")||!strcmp(name,".."))continue;
        if(r->count==SSH_ENTRY_LIMIT){ok=FALSE;break;}
        if(r->count==capacity){capacity=min(capacity*2,SSH_ENTRY_LIMIT);SshEntry *next=realloc(r->entries,(size_t)capacity*sizeof(*next));if(!next){ok=FALSE;break;}r->entries=next;}
        SshEntry *entry=&r->entries[r->count];ZeroMemory(entry,sizeof(*entry));
        if(!wide(name,rc,entry->name,512)){ok=FALSE;break;}
        entry->permissions=attr.permissions;entry->directory=LIBSSH2_SFTP_S_ISDIR(attr.permissions);entry->link=LIBSSH2_SFTP_S_ISLNK(attr.permissions);entry->size=attr.filesize;r->count++;
    }
    close_remote(c,h);if(!ok){free(r->entries);r->entries=NULL;r->count=0;return failed(c,r,nova_text(L"读取目录（最多 10000 项，名称必须为 UTF-8）",L"Read directory (at most 10000 entries, UTF-8 names required)"));}return TRUE;
}
BOOL ssh_client_mkdir(SshClient *c,const wchar_t *path,SshResult *r){begin(c);char p[SSH_PATH_CAP*4];if(!path_bytes(c,path,p,r))return FALSE;int rc;RETRY(c,rc,libssh2_sftp_mkdir(c->sftp,p,0755));return !rc||failed(c,r,nova_text(L"创建目录",L"Create directory"));}
BOOL ssh_client_rename(SshClient *c,const wchar_t *from,const wchar_t *to,SshResult *r){
    begin(c);char a[SSH_PATH_CAP*4],b[SSH_PATH_CAP*4];if(!path_bytes(c,from,a,r)||!path_bytes(c,to,b,r))return FALSE;
    LIBSSH2_SFTP_ATTRIBUTES attr;int rc;RETRY(c,rc,libssh2_sftp_lstat(c->sftp,b,&attr));
    if(rc==0){lstrcpyW(r->error,nova_text(L"目标已存在；未覆盖。",L"The destination exists; nothing was overwritten."));r->code=1;return FALSE;}
    RETRY(c,rc,libssh2_sftp_rename_ex(c->sftp,a,(unsigned)strlen(a),b,(unsigned)strlen(b),0));return !rc||failed(c,r,nova_text(L"重命名或移动",L"Rename or move"));
}
static BOOL delete_node(SshClient *c,const wchar_t *path,int depth,SshResult *r){
    char p[SSH_PATH_CAP*4];if(depth>32||++c->visited>SSH_ENTRY_LIMIT||WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0||GetTickCount64()>=c->deadline||!path_bytes(c,path,p,r))return failed(c,r,nova_text(L"删除范围超限或已取消",L"Deletion limit exceeded or cancelled"));
    LIBSSH2_SFTP_ATTRIBUTES attr;int rc;RETRY(c,rc,libssh2_sftp_lstat(c->sftp,p,&attr));if(rc)return failed(c,r,nova_text(L"读取属性",L"Read attributes"));
    if(LIBSSH2_SFTP_S_ISDIR(attr.permissions)){
        LIBSSH2_SFTP_HANDLE *h=open_remote(c,p,0,0,LIBSSH2_SFTP_OPENDIR);if(!h)return failed(c,r,nova_text(L"读取子目录",L"Read child directory"));BOOL ok=TRUE;
        for(;;){char name[2048];LIBSSH2_SFTP_ATTRIBUTES child;RETRY(c,rc,libssh2_sftp_readdir_ex(h,name,sizeof(name)-1,NULL,0,&child));if(!rc)break;if(rc<0||rc>=(int)sizeof(name)-1){ok=FALSE;break;}name[rc]=0;if(!strcmp(name,".")||!strcmp(name,".."))continue;
            wchar_t value[512],joined[SSH_PATH_CAP];if(!wide(name,rc,value,512)||!ssh_path_join(joined,SSH_PATH_CAP,path,value)||!delete_node(c,joined,depth+1,r)){ok=FALSE;break;}}
        close_remote(c,h);if(!ok)return r->code?FALSE:failed(c,r,nova_text(L"删除子目录",L"Delete child directory"));RETRY(c,rc,libssh2_sftp_rmdir(c->sftp,p));
    }else {RETRY(c,rc,libssh2_sftp_unlink(c->sftp,p));}
    return !rc||failed(c,r,nova_text(L"删除（可能已完成部分操作）",L"Delete (some operations may already have completed)"));
}
BOOL ssh_client_delete(SshClient *c,const wchar_t *path,SshResult *r){begin(c);if(!wcscmp(path,L"/"))return failed(c,r,nova_text(L"禁止删除根目录",L"Root deletion is forbidden"));return delete_node(c,path,0,r);}
static BOOL local_name_valid(const wchar_t *name){
    if(!name[0]||wcspbrk(name,L"\\/:*?\"<>|")||!wcscmp(name,L".")||!wcscmp(name,L"..")||name[wcslen(name)-1]==L'.'||name[wcslen(name)-1]==L' ')return FALSE;
    for(const wchar_t *p=name;*p;p++)if(*p<32)return FALSE;
    wchar_t stem[8]={0};size_t length=wcscspn(name,L".");if(length<8){wmemcpy(stem,name,length);CharUpperBuffW(stem,(DWORD)length);
        if(!wcscmp(stem,L"CON")||!wcscmp(stem,L"PRN")||!wcscmp(stem,L"AUX")||!wcscmp(stem,L"NUL")||!wcscmp(stem,L"CONIN$")||!wcscmp(stem,L"CONOUT$")||
           (length==4&&(!wcsncmp(stem,L"COM",3)||!wcsncmp(stem,L"LPT",3))&&stem[3]>=L'0'&&stem[3]<=L'9'))return FALSE;}
    return TRUE;
}
static BOOL local_join(wchar_t *out,const wchar_t *base,const wchar_t *name){
    if(!local_name_valid(name)||wcslen(base)+wcslen(name)+2>=MAX_PATH)return FALSE;
    swprintf(out,MAX_PATH,L"%ls\\%ls",base,name);return TRUE;
}
static BOOL transfer_node(SshClient *c,BOOL upload,const wchar_t *local,const wchar_t *remote,int depth,SshResult *r){
    if(!upload){const wchar_t *name=wcsrchr(local,L'\\');if(!name||!local_name_valid(name+1))return failed(c,r,nova_text(L"远程名称不能安全映射为 Windows 文件名",L"Remote name cannot safely map to a Windows filename"));}
    if(depth>32||++c->visited>SSH_ENTRY_LIMIT||WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0)return failed(c,r,nova_text(L"传输范围超限或已取消",L"Transfer limit exceeded or cancelled"));
    char p[SSH_PATH_CAP*4];if(!path_bytes(c,remote,p,r))return FALSE;
    DWORD attributes=upload?GetFileAttributesW(local):0;LIBSSH2_SFTP_ATTRIBUTES attr={0};int rc;
    if(upload){if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return failed(c,r,nova_text(L"读取本地文件（不跟随重解析点）",L"Read local file (reparse points are not followed)"));}
    else {RETRY(c,rc,libssh2_sftp_lstat(c->sftp,p,&attr));if(rc||LIBSSH2_SFTP_S_ISLNK(attr.permissions))return failed(c,r,nova_text(L"下载文件（不跟随符号链接）",L"Download file (symlinks are not followed)"));}
    BOOL directory=upload?(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0:LIBSSH2_SFTP_S_ISDIR(attr.permissions);
    if(directory){
        if(upload){RETRY(c,rc,libssh2_sftp_mkdir(c->sftp,p,0755));if(rc)return failed(c,r,nova_text(L"创建上传目录（目标必须不存在）",L"Create upload directory (destination must not exist)"));
            wchar_t pattern[MAX_PATH];if(!local_join(pattern,local,L"placeholder"))return failed(c,r,nova_text(L"本地路径过长",L"Local path too long"));swprintf(pattern,MAX_PATH,L"%ls\\*",local);
            WIN32_FIND_DATAW entry;HANDLE search=FindFirstFileW(pattern,&entry);if(search==INVALID_HANDLE_VALUE)return failed(c,r,nova_text(L"枚举本地目录",L"Enumerate local directory"));BOOL ok=TRUE;
            do{if(!wcscmp(entry.cFileName,L".")||!wcscmp(entry.cFileName,L".."))continue;wchar_t a[MAX_PATH],b[SSH_PATH_CAP];if(!local_join(a,local,entry.cFileName)||!ssh_path_join(b,SSH_PATH_CAP,remote,entry.cFileName)||!transfer_node(c,upload,a,b,depth+1,r)){ok=FALSE;break;}}while(FindNextFileW(search,&entry));
            if(ok&&GetLastError()!=ERROR_NO_MORE_FILES)ok=FALSE;
            FindClose(search);return ok||failed(c,r,nova_text(L"上传目录（可能已部分完成）",L"Upload directory (may be partially complete)"));
        }
        if(!CreateDirectoryW(local,NULL))return failed(c,r,nova_text(L"创建下载目录（目标必须不存在）",L"Create download directory (destination must not exist)"));
        LIBSSH2_SFTP_HANDLE *h=open_remote(c,p,0,0,LIBSSH2_SFTP_OPENDIR);if(!h)return failed(c,r,nova_text(L"枚举远程目录",L"Enumerate remote directory"));BOOL ok=TRUE;
        for(;;){char name[2048];RETRY(c,rc,libssh2_sftp_readdir_ex(h,name,sizeof(name)-1,NULL,0,&attr));if(!rc)break;if(rc<0||rc>=(int)sizeof(name)-1){ok=FALSE;break;}name[rc]=0;if(!strcmp(name,".")||!strcmp(name,".."))continue;
            wchar_t n[512],a[MAX_PATH],b[SSH_PATH_CAP];if(!wide(name,rc,n,512)||!local_join(a,local,n)||!ssh_path_join(b,SSH_PATH_CAP,remote,n)||!transfer_node(c,FALSE,a,b,depth+1,r)){ok=FALSE;break;}}
        close_remote(c,h);return ok||failed(c,r,nova_text(L"下载目录（可能已部分完成）",L"Download directory (may be partially complete)"));
    }
    HANDLE file=CreateFileW(local,upload?GENERIC_READ:GENERIC_WRITE,upload?FILE_SHARE_READ:0,NULL,upload?OPEN_EXISTING:CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return failed(c,r,nova_text(L"打开本地文件（下载不会覆盖已有文件）",L"Open local file (downloads never overwrite existing files)"));
    LIBSSH2_SFTP_HANDLE *h=open_remote(c,p,upload?(LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_EXCL):LIBSSH2_FXF_READ,0644,LIBSSH2_SFTP_OPENFILE);
    if(!h){CloseHandle(file);if(!upload)DeleteFileW(local);return failed(c,r,nova_text(L"打开远程文件（上传不会覆盖已有文件）",L"Open remote file (uploads never overwrite existing files)"));}
    /* Allocate only at the file leaf, rather than reserving 64 KiB per directory
       recursion frame on the bounded worker-thread stack. */
    char *buffer=malloc(65536);BOOL ok=buffer!=NULL;unsigned long long done=0,total=attr.filesize;LARGE_INTEGER local_size;
    if(upload&&GetFileSizeEx(file,&local_size))total=(unsigned long long)local_size.QuadPart;
    progress(c,0,total);
    while(ok){
        if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0){ok=FALSE;break;}
        if(upload){DWORD read=0;if(!ReadFile(file,buffer,65536,&read,NULL)){ok=FALSE;break;}if(!read)break;
            size_t offset=0;while(offset<read){ssize_t n;RETRY(c,n,libssh2_sftp_write(h,buffer+offset,read-offset));if(n<=0){ok=FALSE;break;}offset+=(size_t)n;c->deadline=GetTickCount64()+30000;}if(!ok)break;done+=read;
        }else{ssize_t n;RETRY(c,n,libssh2_sftp_read(h,buffer,65536));if(!n)break;if(n<0){ok=FALSE;break;}DWORD written=0;if(!WriteFile(file,buffer,(DWORD)n,&written,NULL)||written!=(DWORD)n){ok=FALSE;break;}c->deadline=GetTickCount64()+30000;done+=(unsigned long long)n;}
        progress(c,done,total);
    }
    free(buffer);
    if(!upload&&ok)ok=FlushFileBuffers(file);
    CloseHandle(file);if(!close_remote(c,h))ok=FALSE;
    if(!ok){if(!upload)DeleteFileW(local);else {RETRY(c,rc,libssh2_sftp_unlink(c->sftp,p));}return failed(c,r,nova_text(L"文件传输（目录中可能已有部分完成的文件）",L"File transfer (some directory files may already have completed)"));}return TRUE;
}
BOOL ssh_client_transfer(SshClient *c,BOOL upload,const wchar_t *local,const wchar_t *remote,SshResult *r){begin(c);return transfer_node(c,upload,local,remote,0,r);}
BOOL ssh_client_copy(SshClient *c,const wchar_t *from,const wchar_t *to,SshResult *r){
    /* First release copies a regular file through bounded memory, without shell commands. */
    begin(c);char a[SSH_PATH_CAP*4],b[SSH_PATH_CAP*4];if(!path_bytes(c,from,a,r)||!path_bytes(c,to,b,r))return FALSE;
    LIBSSH2_SFTP_ATTRIBUTES attr;int rc;RETRY(c,rc,libssh2_sftp_lstat(c->sftp,a,&attr));if(rc||!LIBSSH2_SFTP_S_ISREG(attr.permissions))return failed(c,r,nova_text(L"复制（首版仅支持普通文件；目录可下载后上传）",L"Copy (regular files only in this release; download and upload directories)"));
    LIBSSH2_SFTP_HANDLE *input=open_remote(c,a,LIBSSH2_FXF_READ,0,LIBSSH2_SFTP_OPENFILE);if(!input)return failed(c,r,nova_text(L"打开复制源",L"Open copy source"));
    LIBSSH2_SFTP_HANDLE *output=open_remote(c,b,LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_EXCL,0644,LIBSSH2_SFTP_OPENFILE);
    if(!output){close_remote(c,input);return failed(c,r,nova_text(L"创建复制目标（不覆盖）",L"Create copy destination (no overwrite)"));}
    BOOL ok=TRUE;char buffer[65536];for(;;){if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0||GetTickCount64()>=c->deadline){ok=FALSE;break;}ssize_t n;RETRY(c,n,libssh2_sftp_read(input,buffer,sizeof(buffer)));if(!n)break;if(n<0){ok=FALSE;break;}ssize_t offset=0;while(offset<n){ssize_t wrote;RETRY(c,wrote,libssh2_sftp_write(output,buffer+offset,(size_t)(n-offset)));if(wrote<=0){ok=FALSE;break;}offset+=wrote;c->deadline=GetTickCount64()+30000;}if(!ok)break;}
    if(!close_remote(c,input))ok=FALSE;
    if(!close_remote(c,output))ok=FALSE;
    if(!ok){RETRY(c,rc,libssh2_sftp_unlink(c->sftp,b));return failed(c,r,nova_text(L"复制文件",L"Copy file"));}return TRUE;
}
static BOOL channel_write(SshClient *c,LIBSSH2_CHANNEL *channel,const char *data,size_t length){size_t offset=0;while(offset<length){ssize_t rc;RETRY(c,rc,libssh2_channel_write(channel,data+offset,length-offset));if(rc<=0)return FALSE;offset+=(size_t)rc;}return TRUE;}
static void append_output(SshResult *r,const char *data,size_t length){
    if(length>=SSH_OUTPUT_CAP){data+=length-(SSH_OUTPUT_CAP-1);length=SSH_OUTPUT_CAP-1;r->output_length=0;r->output_truncated=TRUE;}
    if(r->output_length+length>=SSH_OUTPUT_CAP){size_t drop=r->output_length+length-(SSH_OUTPUT_CAP-1);memmove(r->output,r->output+drop,r->output_length-drop);r->output_length-=drop;r->output_truncated=TRUE;}
    memcpy(r->output+r->output_length,data,length);r->output_length+=length;r->output[r->output_length]=0;
}
BOOL ssh_client_exec(SshClient *c,const wchar_t *directory,const wchar_t *command,SshResult *r){
    begin(c);c->phase=SSH_PHASE_COMMAND;c->deadline=GetTickCount64()+30*60*1000;char path[SSH_PATH_CAP*4],cmd[SSH_PATH_CAP*4];if(!path_bytes(c,directory,path,r)||!utf8(command,cmd,sizeof(cmd)))return failed(c,r,nova_text(L"命令过长",L"Command too long"));
    if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0)return failed(c,r,nova_text(L"命令已取消",L"Command cancelled"));
    size_t cap=strlen(path)*5+2;char *payload=malloc(cap);if(!payload||!ssh_directory_payload((unsigned char*)path,strlen(path),payload,cap)){free(payload);return failed(c,r,nova_text(L"准备远程目录",L"Prepare remote directory"));}
    LIBSSH2_CHANNEL *channel;do{channel=libssh2_channel_open_session(c->session);}while(!channel&&libssh2_session_last_errno(c->session)==LIBSSH2_ERROR_EAGAIN&&waiting(c));
    if(!channel){free(payload);return failed(c,r,nova_text(L"打开命令通道",L"Open command channel"));}
    /* The exec request is constant. Neither paths nor user text enter its shell syntax.
       The first stdin line is octal data; the remainder is the explicitly requested script.
       A sentinel preserves paths ending in newlines across POSIX command substitution. */
    static const char bootstrap[]="sh -c 'IFS= read -r nova_dir || exit 125; nova_dir=$(printf \"%b_\" \"$nova_dir\") || exit 125; nova_dir=${nova_dir%_}; CDPATH= cd \"$nova_dir\" || exit 125; exec sh -s'";
    int rc;RETRY(c,rc,libssh2_channel_exec(channel,bootstrap));BOOL ok=!rc&&channel_write(c,channel,payload,strlen(payload))&&channel_write(c,channel,cmd,strlen(cmd))&&channel_write(c,channel,"\n",1);free(payload);
    if(ok){RETRY(c,rc,libssh2_channel_send_eof(channel));ok=rc==0;}
    r->output=calloc(SSH_OUTPUT_CAP,1);if(!r->output)ok=FALSE;
    while(ok){if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0||GetTickCount64()>=c->deadline){ok=FALSE;break;}BOOL received=FALSE;for(int stream=0;stream<2;stream++){char data[4096];ssize_t n=libssh2_channel_read_ex(channel,stream,data,sizeof(data));if(n>0){append_output(r,data,(size_t)n);received=TRUE;}else if(n<0&&n!=LIBSSH2_ERROR_EAGAIN){ok=FALSE;break;}}
        if(libssh2_channel_eof(channel)&&!received)break;
        if(!received&&!waiting(c)){ok=FALSE;break;}}
    if(ok){RETRY(c,rc,libssh2_channel_close(channel));ok=!rc;if(ok){r->exit_code=libssh2_channel_get_exit_status(channel);char *signal=NULL;if(!libssh2_channel_get_exit_signal(channel,&signal,NULL,NULL,NULL,NULL,NULL)&&signal){r->exit_code=255;libssh2_free(c->session,signal);}}}
    if(!ok){failed(c,r,nova_text(L"远程命令（不自动重试）",L"Remote command (never retried automatically)"));}
    RETRY(c,rc,libssh2_channel_free(channel));return ok;
}
BOOL ssh_client_terminal_open(SshClient *c,const wchar_t *directory,int columns,int rows,SshResult *r){
    begin(c);c->phase=SSH_PHASE_COMMAND;
    if(c->terminal||columns<2||columns>500||rows<2||rows>200)return failed(c,r,nova_text(L"终端尺寸或状态无效",L"Invalid terminal size or state"));
    char path[SSH_PATH_CAP*4];if(!path_bytes(c,directory,path,r))return FALSE;
    size_t capacity=strlen(path)*5+2;char *payload=malloc(capacity);
    if(!payload||!ssh_directory_payload((unsigned char*)path,strlen(path),payload,capacity)){free(payload);return failed(c,r,nova_text(L"准备终端目录",L"Prepare terminal directory"));}
    LIBSSH2_CHANNEL *channel;
    do{channel=libssh2_channel_open_session(c->session);}while(!channel&&libssh2_session_last_errno(c->session)==LIBSSH2_ERROR_EAGAIN&&waiting(c));
    if(!channel){free(payload);return failed(c,r,nova_text(L"打开终端通道",L"Open terminal channel"));}
    /* RFC 4254: ICANON=51, ECHO=53. Disable both until the data-only directory
       frame is consumed: long paths must not hit the PTY canonical line limit,
       and neither paths nor terminal input may be echoed during bootstrap. */
    static const char modes[]={51,0,0,0,0,53,0,0,0,0,0};
    int rc;RETRY(c,rc,libssh2_channel_request_pty_ex(channel,"xterm-256color",14,modes,sizeof(modes),columns,rows,0,0));
    static const char bootstrap[]="sh -c 'IFS= read -r nova_dir || exit 125; nova_dir=$(printf \"%b_\" \"$nova_dir\") || exit 125; nova_dir=${nova_dir%_}; CDPATH= cd \"$nova_dir\" || exit 125; stty sane || exit 125; exec \"${SHELL:-/bin/sh}\" -i'";
    BOOL ok=rc==0;
    if(ok){RETRY(c,rc,libssh2_channel_exec(channel,bootstrap));ok=rc==0;}
    if(ok)ok=channel_write(c,channel,payload,strlen(payload));
    free(payload);
    if(!ok){failed(c,r,nova_text(L"启动交互终端",L"Start interactive terminal"));/* Failed partial channels are freed with their session. */c->terminal=channel;return FALSE;}
    c->terminal=channel;c->terminal_event=WSACreateEvent();
    if(!c->terminal_event||WSAEventSelect(c->socket,c->terminal_event,FD_READ|FD_WRITE|FD_CLOSE)){c->native_error=WSAGetLastError();return failed(c,r,nova_text(L"监听终端连接",L"Watch terminal connection"));}
    return TRUE;
}
BOOL ssh_client_terminal_read(SshClient *c,char *data,size_t capacity,size_t *length,BOOL *ended,SshResult *r){
    *length=0;*ended=FALSE;begin(c);c->phase=SSH_PHASE_COMMAND;
    if(WaitForSingleObject(c->cancel,0)==WAIT_OBJECT_0)return failed(c,r,nova_text(L"终端已取消",L"Terminal cancelled"));
    if(!c->terminal)return failed(c,r,nova_text(L"终端未启动",L"Terminal is not running"));
    for(int stream=0;stream<2;stream++){
        ssize_t n=libssh2_channel_read_ex(c->terminal,stream,data,capacity);
        if(n>0){*length=(size_t)n;return TRUE;}
        if(n<0&&n!=LIBSSH2_ERROR_EAGAIN)return failed(c,r,nova_text(L"读取终端",L"Read terminal"));
    }
    if(libssh2_channel_eof(c->terminal)){
        int rc=libssh2_channel_wait_closed(c->terminal);if(rc==LIBSSH2_ERROR_EAGAIN)return TRUE;
        if(rc<0)return failed(c,r,nova_text(L"等待终端退出",L"Wait for terminal exit"));
        *ended=TRUE;r->exit_code=libssh2_channel_get_exit_status(c->terminal);
    }
    return TRUE;
}
BOOL ssh_client_terminal_write(SshClient *c,const char *data,size_t length,size_t *written,SshResult *r){
    *written=0;begin(c);c->phase=SSH_PHASE_COMMAND;
    if(!c->terminal)return failed(c,r,nova_text(L"终端未启动",L"Terminal is not running"));
    ssize_t n=libssh2_channel_write(c->terminal,data,length);
    if(n==LIBSSH2_ERROR_EAGAIN)return TRUE;
    if(n<0)return failed(c,r,nova_text(L"发送终端输入",L"Send terminal input"));
    *written=(size_t)n;return TRUE;
}
BOOL ssh_client_terminal_resize(SshClient *c,int columns,int rows,SshResult *r){
    begin(c);c->phase=SSH_PHASE_COMMAND;
    if(!c->terminal||columns<2||columns>500||rows<2||rows>200)return failed(c,r,nova_text(L"终端尺寸无效",L"Invalid terminal size"));
    int rc;RETRY(c,rc,libssh2_channel_request_pty_size(c->terminal,columns,rows));
    return rc==0||failed(c,r,nova_text(L"调整终端尺寸",L"Resize terminal"));
}
DWORD ssh_client_terminal_wait(SshClient *c,HANDLE request,HANDLE input,DWORD timeout){
    HANDLE events[]={request,input,c->cancel,c->terminal_event};DWORD wake=WaitForMultipleObjects(4,events,FALSE,timeout);
    if(wake==WAIT_OBJECT_0+3){WSANETWORKEVENTS network;WSAEnumNetworkEvents(c->socket,c->terminal_event,&network);}return wake;
}
void ssh_result_dispose(SshResult *r){if(!r)return;free(r->entries);free(r->output);ZeroMemory(r,sizeof(*r));}
