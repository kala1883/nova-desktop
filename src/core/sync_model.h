#ifndef NOVA_SYNC_MODEL_H
#define NOVA_SYNC_MODEL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <wchar.h>
#define SYNC_PAIR_LIMIT 16
#define SYNC_DEVICE_LIMIT 16
#define SYNC_PATH_CAP 2048
#define SYNC_ID_CAP 81
#define SYNC_NAME_CAP 128
#define SYNC_FILTER_CAP 1024
#define SYNC_CHUNK_MIN (128u*1024u)
#define SYNC_CHUNK_MAX (2u*1024u*1024u)
#define SYNC_PACKET_MAX (128u*1024u)
#define SYNC_JSON_MAX (16u*1024u*1024u)
typedef struct SyncPair {
    wchar_t id[SYNC_ID_CAP],dataset[SYNC_ID_CAP],name[SYNC_NAME_CAP],group[SYNC_NAME_CAP];
    wchar_t directory[SYNC_PATH_CAP],excludes[SYNC_FILTER_CAP];
    bool paused,sync_deletes;
} SyncPair;
typedef struct SyncSettings {
    int version,count,role,interval;
    int64_t server_id;
    bool registered;
    wchar_t server_instance[SYNC_ID_CAP];
    wchar_t node_id[SYNC_ID_CAP],name[SYNC_NAME_CAP],project[SYNC_PATH_CAP],broker[SYNC_PATH_CAP];
    SyncPair pairs[SYNC_PAIR_LIMIT];
} SyncSettings;
typedef struct SyncChunker { uint64_t hash; size_t size; } SyncChunker;
/* Relationship, automatic scheduling and file differences are independent facts.
   Server receipts never imply that another device's working tree was inspected. */
typedef enum SyncRelation { SYNC_REL_UNKNOWN,SYNC_REL_LOCAL,SYNC_REL_PUBLISHER,SYNC_REL_SUBSCRIBER,SYNC_REL_CONNECTED } SyncRelation;
typedef enum SyncFolderPhase { SYNC_FOLDER_UNKNOWN,SYNC_FOLDER_BIND,SYNC_FOLDER_CHECK,
    SYNC_FOLDER_WORKING,SYNC_FOLDER_ERROR,SYNC_FOLDER_CONFLICT,SYNC_FOLDER_WAIT,
    SYNC_FOLDER_CHANGES,SYNC_FOLDER_CURRENT } SyncFolderPhase;
typedef struct SyncFolderFacts {
    bool metadata_known,bound,publisher,inspected,busy,failed;
    int pushes,pulls,conflicts,waiting;
    bool publisher_known;
} SyncFolderFacts;
typedef struct SyncFolderState { SyncRelation relation; SyncFolderPhase phase; } SyncFolderState;
SyncFolderState sync_folder_state(const SyncFolderFacts *facts);
bool sync_identifier(const wchar_t *text);
bool sync_relative_path(const wchar_t *text);
bool sync_settings_valid(const SyncSettings *value);
void sync_settings_default(SyncSettings *value);
/* Returns the new index, or -1 without mutating the collection. */
int sync_pair_set(SyncSettings *value,int index,const SyncPair *pair);
bool sync_pair_remove(SyncSettings *value,int index);
bool sync_chunk_boundary(SyncChunker *value,unsigned char byte,const uint64_t gear[256]);
#endif
