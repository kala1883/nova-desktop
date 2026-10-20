#include "server_profile.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
static bool bounded(const wchar_t *s,size_t cap){return wmemchr(s,0,cap)!=NULL;}
bool ssh_servers_valid(const SshServers *servers){
    if(!servers||servers->count<0||servers->count>SSH_SERVER_LIMIT)return false;
    for(int i=0;i<servers->count;i++){
        const SshServer *s=&servers->items[i];
        if(s->id<=0||s->port<1||s->port>65535||!bounded(s->name,SSH_NAME_CAP)||!s->name[0]||
           !bounded(s->host,256)||!s->host[0]||!bounded(s->user,128)||!s->user[0]||
           !bounded(s->directory,SSH_PATH_CAP)||s->directory[0]!=L'/'||wcslen(s->directory)>=SSH_PATH_CAP-40)return false;
        for(const wchar_t *p=s->host;*p;p++)if(*p<=L' '||*p==L'/'||*p==L'\\'||*p==L'@')return false;
        for(int j=0;j<i;j++)if(s->id==servers->items[j].id||!wcscmp(s->name,servers->items[j].name))return false;
    }
    return true;
}
bool ssh_location_parse(const wchar_t *location,int64_t *id,const wchar_t **path){
    if(!location||wcsncmp(location,L"nova-ssh:",9))return false;
    const wchar_t *p=location+9;if(*p<L'1'||*p>L'9')return false;
    errno=0;wchar_t *end;long long value=wcstoll(p,&end,10);
    if(errno||value<=0||*end!=L':'||end[1]!=L'/')return false;
    if(id)*id=value;
    if(path)*path=end+1;
    return true;
}
bool ssh_location_format(wchar_t *out,size_t capacity,int64_t id,const wchar_t *path){
    if(!out||!path||path[0]!=L'/'||id<=0||wcslen(path)+40>=capacity)return false;
    int n=swprintf(out,capacity,L"nova-ssh:%lld:%ls",(long long)id,path);return n>0&&(size_t)n<capacity;
}
bool ssh_path_join(wchar_t *out,size_t capacity,const wchar_t *base,const wchar_t *name){
    if(!out||!base||base[0]!=L'/'||!name||!name[0]||wcschr(name,L'/')||!wcscmp(name,L".")||!wcscmp(name,L".."))return false;
    size_t n=wcslen(base);bool slash=n>1&&base[n-1]==L'/';if(slash)n--;
    bool root=n==1;if(n+wcslen(name)+(root?1:2)>capacity)return false;
    wmemcpy(out,base,n);out[n]=0;if(!root)wcscat(out,L"/");wcscat(out,name);return true;
}
bool ssh_directory_payload(const unsigned char *path,size_t length,char *out,size_t capacity){
    if(!path||!length||path[0]!='/'||!out||length>(SIZE_MAX-2)/5||capacity<length*5+2)return false;
    for(size_t i=0;i<length;i++){
        unsigned c=path[i];if(!c)return false;
        out[5*i]='\\';out[5*i+1]='0';out[5*i+2]=(char)('0'+((c>>6)&7));out[5*i+3]=(char)('0'+((c>>3)&7));out[5*i+4]=(char)('0'+(c&7));
    }
    out[length*5]='\n';out[length*5+1]=0;return true;
}
