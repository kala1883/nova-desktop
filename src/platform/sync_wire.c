#include "sync_internal.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint32_t read32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
    }
static void put32(unsigned char *p,uint32_t v){p[0]=(unsigned char)(v>>24);
    p[1]=(unsigned char)(v>>16);
    p[2]=(unsigned char)(v>>8);
    p[3]=(unsigned char)v;
    }
BOOL sy_fail(SyncRun *r,SyncFailure code){
    if(r->result->failure==SYNC_OK||code==SYNC_CANCELLED){
        r->result->failure=code;
        r->result->native_error=code==SYNC_CANCELLED?ERROR_CANCELLED:GetLastError();
    }
    return FALSE;
}
BOOL sy_cancel(SyncRun *r){if(r->request->cancel&&WaitForSingleObject(r->request->cancel,0)==WAIT_OBJECT_0){sy_fail(r,SYNC_CANCELLED);
        return TRUE;
        }return FALSE;
    }
void sy_progress(SyncRun *r,SyncPhase phase,const wchar_t *path,uint64_t done,uint64_t total){
    ULONGLONG now=GetTickCount64();
    if(!r->request->progress||(now-r->progress_tick<150&&done!=total))return;
    r->progress_tick=now;
    SyncProgress p={0};
    p.phase=phase;
    p.done=done;
    p.total=total;
    p.stats=r->result->stats;
    if(path)lstrcpynW(p.path,path,SYNC_PATH_CAP);
    r->request->progress(r->request->context,&p);
}
void sy_reply_free(SyncReply *reply){free(reply->json);
    free(reply->data);
    memset(reply,0,sizeof(*reply));
    }
char *sy_fields(const char *action,const char *a,const char *av,const char *b,const char *bv){
    SyncBuffer out={0};
    BOOL ok=sync_buffer_add(&out,"{\"action\":")&&sync_buffer_string(&out,action);
    if(ok&&a)ok=sync_buffer_add(&out,",")&&sync_buffer_string(&out,a)&&sync_buffer_add(&out,":")&&sync_buffer_string(&out,av);
    if(ok&&b)ok=sync_buffer_add(&out,",")&&sync_buffer_string(&out,b)&&sync_buffer_add(&out,":")&&sync_buffer_string(&out,bv);
    ok=ok&&sync_buffer_add(&out,"}");
    if(!ok){free(out.data);
        return NULL;
        }return out.data;
}
BOOL sy_rpc(SyncRun *r,const char *request,const void *data,size_t size,SyncReply *reply){
    memset(reply,0,sizeof(*reply));
    if(sy_cancel(r)||size>SYNC_PACKET_MAX)return FALSE;
    char role[2]={(char)('A'+r->request->settings.role),0};
    char *node=sync_utf8(r->request->settings.node_id);
    char *header=node?sync_json_patch(r->db,request,"$.device",role,FALSE):NULL;
    free(node);
    node=sync_utf8(r->request->settings.node_id);
    char *next=header&&node?sync_json_patch(r->db,header,"$.node_id",node,FALSE):NULL;
    free(header);
    free(node);
    header=next;
    unsigned char *packed=NULL;
    const void *wire=data;
    size_t wire_size=size;
    if(header&&size){
        uLongf capacity=compressBound((uLong)size);
        packed=malloc(capacity);
        if(packed&&compress2(packed,&capacity,data,(uLong)size,Z_BEST_SPEED)==Z_OK&&capacity<size*95/100){wire=packed;
            wire_size=capacity;
            next=sync_json_patch(r->db,header,"$._encoding","zlib",FALSE);
            free(header);
            header=next;
            }
        char number[32];
        snprintf(number,sizeof(number),"%zu",size);
        next=header?sync_json_patch(r->db,header,"$._raw_size",number,TRUE):NULL;
        free(header);
        header=next;
    }
    if(!header){free(packed);
        return sy_fail(r,SYNC_STORAGE);
        }size_t length=strlen(header);
    unsigned char prefix[8];
    put32(prefix,(uint32_t)length);
    put32(prefix+4,(uint32_t)wire_size);
    SshResult *error=&r->result->ssh;
    BOOL ok=ssh_client_protocol_write(r->ssh,prefix,8,error)&&ssh_client_protocol_write(r->ssh,header,length,error)&&(!wire_size||ssh_client_protocol_write(r->ssh,wire,wire_size,error));
    free(header);
    free(packed);
    if(!ok)return sy_fail(r,error->cancelled?SYNC_CANCELLED:SYNC_NETWORK);
    r->result->stats.sent+=wire_size;
    if(!ssh_client_protocol_read(r->ssh,prefix,8,error))return sy_fail(r,error->cancelled?SYNC_CANCELLED:SYNC_NETWORK);
    uint32_t json_size=read32(prefix),binary_size=read32(prefix+4);
    if(!json_size||json_size>SYNC_JSON_MAX||binary_size>SYNC_PACKET_MAX)return sy_fail(r,SYNC_PROTOCOL);
    header=calloc((size_t)json_size+1,1);
    unsigned char *binary=malloc(binary_size?binary_size:1);
    if(!header||!binary){free(header);
        free(binary);
        return sy_fail(r,SYNC_STORAGE);
        }
    ok=ssh_client_protocol_read(r->ssh,header,json_size,error)&&(!binary_size||ssh_client_protocol_read(r->ssh,binary,binary_size,error));
    if(!ok){free(header);
        free(binary);
        return sy_fail(r,error->cancelled?SYNC_CANCELLED:SYNC_NETWORK);
        }
    if(strlen(header)!=json_size||!sync_json_valid(r->db,header)){free(header);
        free(binary);
        return sy_fail(r,SYNC_PROTOCOL);
        }
    r->result->stats.received+=binary_size;
    char *encoding=sync_json_string(r->db,header,"$._encoding");
    int64_t raw_size=sync_json_int(r->db,header,"$._raw_size",0);
    ok=raw_size>=0&&raw_size<=SYNC_PACKET_MAX;
    if(ok&&encoding){
        ok=!strcmp(encoding,"zlib");
        unsigned char *expanded=ok?malloc((size_t)raw_size+1):NULL;
        z_stream stream={0};
        if(expanded){stream.next_in=binary;
            stream.avail_in=binary_size;
            stream.next_out=expanded;
            stream.avail_out=(uInt)raw_size+1;
            if(inflateInit(&stream)!=Z_OK)ok=FALSE;
            else{int rc=inflate(&stream,Z_FINISH);
                ok=rc==Z_STREAM_END&&stream.total_out==(uLong)raw_size&&stream.total_in==binary_size;
                inflateEnd(&stream);
                }
        }else ok=FALSE;
        free(binary);
        binary=expanded;
    }else if(ok)ok=(uint32_t)raw_size==binary_size;
    free(encoding);
    if(ok&&sync_json_int(r->db,header,"$.ok",0)!=1){char *message=sync_json_string(r->db,header,"$.error");
        char *code=sync_json_string(r->db,header,"$.code");
        SyncFailure reason=code&&!strcmp(code,"moved")?SYNC_MOVED:code&&!strcmp(code,"frozen")?SYNC_FROZEN:SYNC_REMOTE;
        free(code);
        free(r->result->route);
        r->result->route=sync_json_string(r->db,header,"$.state");
        if(message){sync_wide(message,r->result->detail,1024);
            free(message);
            }free(header);
        free(binary);
        return sy_fail(r,reason);
        }
    if(!ok){free(header);
        free(binary);
        return sy_fail(r,SYNC_PROTOCOL);
        }
    reply->json=sync_json_string(r->db,header,"$.result");
    free(header);
    reply->data=binary;
    reply->size=(size_t)raw_size;
    if(!reply->json){sy_reply_free(reply);
        return sy_fail(r,SYNC_PROTOCOL);
        }return TRUE;
}
