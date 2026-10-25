#include "sync_model.h"
#include <string.h>
#include <wctype.h>
static bool bounded(const wchar_t *s,size_t cap){return wmemchr(s,0,cap)!=NULL;
    }
static bool equal_path(const wchar_t *a,const wchar_t *b){
    while(*a&&*b){wchar_t x=*a++,y=*b++;
        if(x==L'/')x=L'\\';
        if(y==L'/')y=L'\\';
        if(towlower(x)!=towlower(y))return false;
        }
    return !*a&&!*b;
}
bool sync_identifier(const wchar_t *s){
    if(!s||!s[0])return false;
    size_t n=0;
    for(;s[n];n++)if(n>=80||!((s[n]>=L'a'&&s[n]<=L'z')||(s[n]>=L'A'&&s[n]<=L'Z')||(s[n]>=L'0'&&s[n]<=L'9')||s[n]==L'_'||s[n]==L'-'))return false;
    return true;
}
bool sync_relative_path(const wchar_t *s){
    if(!s||!*s||*s==L'/'||*s==L'\\'||wcspbrk(s,L"\\:*?\"<>|"))return false;
    const wchar_t *part=s;
    for(const wchar_t *p=s;;p++){
        if(*p&&*p<32)return false;
        if(*p==L'/'||!*p){size_t n=(size_t)(p-part);
            if(!n||n>255||part[n-1]==L'.'||part[n-1]==L' '||(n==1&&part[0]==L'.')||(n==2&&!wcsncmp(part,L"..",2))||(n==4&&!wcsncmp(part,L".git",4)))return false;
            if(!*p)break;
            part=p+1;
            }
    }
    return true;
}
void sync_settings_default(SyncSettings *s){
    memset(s,0,sizeof(*s));
    s->version=1;
    s->interval=60;
    wcscpy(s->project,L"/opt/nova-sync");
    wcscpy(s->broker,L"/opt/nova-sync/runtime/broker");
}
static bool pair_valid(const SyncPair *p){
    return bounded(p->id,SYNC_ID_CAP)&&sync_identifier(p->id)&&bounded(p->dataset,SYNC_ID_CAP)&&sync_identifier(p->dataset)&&
        bounded(p->name,SYNC_NAME_CAP)&&p->name[0]&&wcslen(p->name)<=120&&bounded(p->group,SYNC_NAME_CAP)&&p->group[0]&&wcslen(p->group)<=120&&
        bounded(p->directory,SYNC_PATH_CAP)&&p->directory[0]&&bounded(p->excludes,SYNC_FILTER_CAP);
}
bool sync_settings_valid(const SyncSettings *s){
    if(s&&(!bounded(s->server_instance,SYNC_ID_CAP)||(s->server_instance[0]&&!sync_identifier(s->server_instance))))return false;
    if(!s||s->version!=1||s->count<0||s->count>SYNC_PAIR_LIMIT||s->role<0||s->role>=SYNC_DEVICE_LIMIT||s->interval<10||s->interval>86400||s->server_id<0||
       !bounded(s->node_id,SYNC_ID_CAP)||(s->node_id[0]&&!sync_identifier(s->node_id))||!bounded(s->name,SYNC_NAME_CAP)||wcslen(s->name)>100||
       !bounded(s->project,SYNC_PATH_CAP)||s->project[0]!=L'/'||!bounded(s->broker,SYNC_PATH_CAP)||s->broker[0]!=L'/'||
       wcspbrk(s->project,L"\r\n")||wcspbrk(s->broker,L"\r\n")||(s->registered&&(!s->node_id[0]||!s->name[0])))return false;
    for(int i=0;i<s->count;i++){
        if(!pair_valid(&s->pairs[i]))return false;
        for(int j=0;j<i;j++)if(!wcscmp(s->pairs[i].id,s->pairs[j].id)||!wcscmp(s->pairs[i].dataset,s->pairs[j].dataset)||equal_path(s->pairs[i].directory,s->pairs[j].directory))return false;
    }
    return true;
}
int sync_pair_set(SyncSettings *s,int index,const SyncPair *p){
    if(!s||!p||index<0||index>s->count||index>=SYNC_PAIR_LIMIT)return -1;
    SyncSettings next=*s;
    next.pairs[index]=*p;
    if(index==next.count)next.count++;
    if(!sync_settings_valid(&next))return -1;
    *s=next;
    return index;
}
bool sync_pair_remove(SyncSettings *s,int index){
    if(!s||index<0||index>=s->count)return false;
    memmove(&s->pairs[index],&s->pairs[index+1],(size_t)(s->count-index-1)*sizeof(s->pairs[0]));
    memset(&s->pairs[--s->count],0,sizeof(s->pairs[0]));
    return true;
}
bool sync_chunk_boundary(SyncChunker *s,unsigned char b,const uint64_t gear[256]){
    s->hash=(s->hash<<1)+gear[b];
    s->size++;
    if(s->size>=SYNC_CHUNK_MAX||(s->size>=SYNC_CHUNK_MIN&&(s->hash&((1u<<19)-1u))==0)){s->hash=0;
        s->size=0;
        return true;
        }return false;
}
