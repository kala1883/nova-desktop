#define WIN32_LEAN_AND_MEAN
#include "sync_json.h"
#include "../../third_party/sqlite/sqlite3.h"
#include <openssl/evp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
BOOL sync_buffer_add(SyncBuffer *b,const char *value){
    size_t n=strlen(value);
    if(n>SYNC_JSON_MAX||b->size>SYNC_JSON_MAX-n)return FALSE;
    size_t need=b->size+n+1;
    if(need>b->capacity){size_t cap=b->capacity?b->capacity*2:1024;
        if(cap<need)cap=need;
        if(cap>SYNC_JSON_MAX+1)cap=SYNC_JSON_MAX+1;
        char *p=realloc(b->data,cap);
        if(!p)return FALSE;
        b->data=p;
        b->capacity=cap;
        }
    memcpy(b->data+b->size,value,n+1);
    b->size+=n;
    return TRUE;
}
BOOL sync_buffer_string(SyncBuffer *b,const char *s){
    if(!sync_buffer_add(b,"\""))return FALSE;
    for(const unsigned char *p=(const unsigned char*)s;*p;p++){char text[8]={0};
        if(*p=='"'||*p=='\\'){text[0]='\\';
            text[1]=(char)*p;
            }
        else if(*p<32){snprintf(text,sizeof(text),"\\u%04x",*p);
            }else text[0]=(char)*p;
        if(!sync_buffer_add(b,text))return FALSE;
    }
    return sync_buffer_add(b,"\"");
}
char *sync_utf8(const wchar_t *s){int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,NULL,0,NULL,NULL);
    char *p=n?malloc((size_t)n):NULL;
    if(p&&!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,p,n,NULL,NULL)){free(p);
        return NULL;
        }return p;
    }
BOOL sync_wide(const char *s,wchar_t *out,int cap){return s&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,out,cap)>0;
    }
BOOL sync_json_valid(sqlite3 *db,const char *text){
    if(!text||strlen(text)>SYNC_JSON_MAX)return FALSE;
    sqlite3_stmt *s=NULL;
    BOOL ok=sqlite3_prepare_v2(db,"SELECT json_valid(?1)",-1,&s,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(s,1,text,-1,SQLITE_STATIC);
        ok=sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_int(s,0)==1;
        }sqlite3_finalize(s);
    return ok;
}
char *sync_json_string(sqlite3 *db,const char *json,const char *path){
    sqlite3_stmt *s=NULL;
    char *out=NULL;
    if(sqlite3_prepare_v2(db,"SELECT json_extract(?1,?2)",-1,&s,NULL)==SQLITE_OK){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,path,-1,SQLITE_STATIC);
        if(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_TEXT){int n=sqlite3_column_bytes(s,0);
            const char *value=(const char*)sqlite3_column_text(s,0);
            if(!value||strlen(value)!=(size_t)n){sqlite3_finalize(s);
                return NULL;
                }
            out=malloc((size_t)n+1);
            if(out)memcpy(out,value,(size_t)n+1);
            }}sqlite3_finalize(s);
    return out;
}
int64_t sync_json_int(sqlite3 *db,const char *json,const char *path,int64_t fallback){
    sqlite3_stmt *s=NULL;
    int64_t out=fallback;
    if(sqlite3_prepare_v2(db,"SELECT json_extract(?1,?2)",-1,&s,NULL)==SQLITE_OK){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,path,-1,SQLITE_STATIC);
        if(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)==SQLITE_INTEGER)out=sqlite3_column_int64(s,0);
        }sqlite3_finalize(s);
    return out;
}
char *sync_json_patch(sqlite3 *db,const char *json,const char *path,const char *value,BOOL object){
    sqlite3_stmt *s=NULL;
    char *out=NULL;
    const char *sql=object?"SELECT json_set(?1,?2,json(?3))":"SELECT json_set(?1,?2,?3)";
    if(sqlite3_prepare_v2(db,sql,-1,&s,NULL)==SQLITE_OK){sqlite3_bind_text(s,1,json,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,2,path,-1,SQLITE_STATIC);
        sqlite3_bind_text(s,3,value,-1,SQLITE_STATIC);
        if(sqlite3_step(s)==SQLITE_ROW)out=strdup((const char*)sqlite3_column_text(s,0));
        }sqlite3_finalize(s);
    return out;
}
BOOL sync_sha(const void *data,size_t length,char out[65]){
    unsigned char digest[32];
    unsigned int n=0;
    BOOL ok=EVP_Digest(data,length,digest,&n,EVP_sha256(),NULL)==1&&n==32;
    if(ok)for(int i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);
    return ok;
}
